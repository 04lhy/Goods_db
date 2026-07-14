#include "network/database_worker.h"

#include <QElapsedTimer>
#include <QHostAddress>

namespace goods_db {
namespace studio {

DatabaseWorker::DatabaseWorker(QObject* parent) : QObject(parent) {
  socket_ = new QTcpSocket(this);
}

DatabaseWorker::~DatabaseWorker() {
  if (socket_->state() != QAbstractSocket::UnconnectedState) {
    socket_->disconnectFromHost();
  }
}

// ---- Protocol packet helpers -----------------------------------------------

static QByteArray BuildPacket(const QByteArray& data) {
  // 4-byte header: 3 bytes little-endian length, 1 byte sequence_id
  uint32_t len = static_cast<uint32_t>(data.size());
  QByteArray pkt;
  pkt.reserve(4 + static_cast<int>(len));
  pkt.append(static_cast<char>(len & 0xFF));
  pkt.append(static_cast<char>((len >> 8) & 0xFF));
  pkt.append(static_cast<char>((len >> 16) & 0xFF));
  pkt.append(static_cast<char>(0));  // sequence_id = 0
  pkt.append(data);
  return pkt;
}

// ---- Thread-safe blocking I/O operations -----------------------------------

void DatabaseWorker::DoConnect(const QString& host, uint16_t port,
                                bool* ok, QString* error) {
  host_ = host;
  port_ = port;

  socket_->connectToHost(host, port);

  // In Qt6, waitForConnected on a worker thread is reliable because the event
  // loop is not processing other GUI events that could interfere.
  if (!socket_->waitForConnected(5000)) {
    last_error_ = socket_->errorString();
    *ok = false;
    *error = last_error_;
    emit ErrorOccurred(last_error_);
    return;
  }

  *ok = true;
  *error = QString();
  emit Connected();
}

void DatabaseWorker::DoDisconnect() {
  if (socket_->state() != QAbstractSocket::UnconnectedState) {
    socket_->disconnectFromHost();
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
      socket_->waitForDisconnected(2000);
    }
  }
  emit Disconnected();
}

void DatabaseWorker::DoIsConnected(bool* connected) {
  *connected = (socket_->state() == QAbstractSocket::ConnectedState);
}

void DatabaseWorker::DoAuthenticate(const QString& user,
                                     const QString& password,
                                     const QString& db,
                                     bool* ok, QString* error) {
  if (socket_->state() != QAbstractSocket::ConnectedState) {
    *ok = false;
    *error = QStringLiteral("Not connected");
    return;
  }

  // Step 1: Read server greeting
  QByteArray greeting;
  if (!ReadPacket(greeting, 5000)) {
    *ok = false;
    *error = QStringLiteral("No server greeting");
    last_error_ = *error;
    return;
  }

  // Step 2: Send auth packet: "AUTH\0user\0password\0db\0"
  QByteArray auth;
  auth.reserve(5 + user.size() + password.size() + db.size() + 4);
  auth.append("AUTH");
  auth.append('\0');
  auth.append(user.toUtf8());
  auth.append('\0');
  auth.append(password.toUtf8());
  auth.append('\0');
  auth.append(db.toUtf8());
  auth.append('\0');

  if (!WritePacket(auth)) {
    *ok = false;
    *error = QStringLiteral("Failed to send auth");
    last_error_ = *error;
    return;
  }

  // Step 3: Read auth response
  QByteArray resp;
  if (!ReadPacket(resp, 5000)) {
    *ok = false;
    *error = QStringLiteral("No auth response");
    last_error_ = *error;
    return;
  }

  if (resp.startsWith("OK ")) {
    *ok = true;
    *error = QString();
    return;
  }

  if (resp.startsWith("ERR ")) {
    *ok = false;
    *error = QString::fromUtf8(resp.mid(4));
    last_error_ = *error;
    return;
  }

  *ok = false;
  *error = QStringLiteral("Unexpected auth response: ") + QString::fromUtf8(resp);
  last_error_ = *error;
}

void DatabaseWorker::DoPing(bool* ok) {
  if (socket_->state() != QAbstractSocket::ConnectedState) {
    *ok = false;
    return;
  }

  if (!WritePacket(QByteArrayLiteral("PING"))) {
    *ok = false;
    return;
  }

  QByteArray resp;
  if (!ReadPacket(resp, 3000)) {
    *ok = false;
    return;
  }

  *ok = resp.startsWith("OK");
}

void DatabaseWorker::DoExecute(const QString& sql, QueryResult* result) {
  QElapsedTimer timer;
  timer.start();

  if (socket_->state() != QAbstractSocket::ConnectedState) {
    result->is_error = true;
    result->error_message = QStringLiteral("Not connected");
    result->exec_time_ms = timer.elapsed();
    return;
  }

  // Build query packet: "QUERY\0<sql>"
  QByteArray query;
  query.reserve(6 + sql.size());
  query.append("QUERY");
  query.append('\0');
  query.append(sql.toUtf8());

  if (!WritePacket(query)) {
    result->is_error = true;
    result->error_message = QStringLiteral("Failed to send query");
    result->exec_time_ms = timer.elapsed();
    return;
  }

  // Read response
  QByteArray response;
  if (!ReadPacket(response, 30000)) {
    result->is_error = true;
    result->error_message = QStringLiteral("No response from server");
    result->exec_time_ms = timer.elapsed();
    return;
  }

  *result = ParseResponse(response);
  result->exec_time_ms = timer.elapsed();
  emit QueryResultReady(*result);
}

// ---- Async slot-based operations -------------------------------------------

void DatabaseWorker::ConnectAsync(const QString& host, uint16_t port) {
  bool ok;
  QString error;
  DoConnect(host, port, &ok, &error);
  if (!ok) {
    emit ErrorOccurred(error);
  }
}

void DatabaseWorker::DisconnectAsync() {
  DoDisconnect();
}

void DatabaseWorker::AuthenticateAsync(const QString& user,
                                        const QString& password,
                                        const QString& db) {
  bool ok;
  QString error;
  DoAuthenticate(user, password, db, &ok, &error);
  emit AuthResult(ok, error);
}

void DatabaseWorker::PingAsync() {
  bool ok;
  DoPing(&ok);
  emit PingResult(ok);
}

void DatabaseWorker::ExecuteAsync(const QString& sql) {
  QueryResult result;
  DoExecute(sql, &result);
  emit QueryResultReady(result);
}

// ---- Low-level I/O helpers (all run on worker thread) -----------------------

bool DatabaseWorker::WritePacket(const QByteArray& data) {
  return WriteAllToSocket(BuildPacket(data));
}

bool DatabaseWorker::WriteAllToSocket(const QByteArray& data) {
  qint64 total = data.size();
  qint64 written = 0;
  while (written < total) {
    qint64 n = socket_->write(data.constData() + written, total - written);
    if (n < 0) return false;
    written += n;
  }
  // In Qt6, flush() on a socket is important — it pushes data to the OS buffer.
  // Without this, data may sit in Qt's internal write buffer.
  if (!socket_->flush()) {
    // flush() can fail in Qt6 if the socket isn't fully ready yet.
    // Wait a bit and retry once.
    if (!socket_->waitForBytesWritten(1000)) return false;
  }
  return true;
}

bool DatabaseWorker::ReadExact(char* buf, qint64 count, int timeout_ms) {
  qint64 got = 0;
  while (got < count) {
    if (socket_->bytesAvailable() < count - got) {
      if (!socket_->waitForReadyRead(timeout_ms)) return false;
    }
    qint64 n = socket_->read(buf + got, count - got);
    if (n <= 0) return false;
    got += n;
  }
  return true;
}

bool DatabaseWorker::ReadPacket(QByteArray& payload, int timeout_ms) {
  // Read 4-byte header
  char hdr[4];
  if (!ReadExact(hdr, 4, timeout_ms)) return false;

  uint32_t plen = static_cast<uint8_t>(hdr[0]) |
                  (static_cast<uint32_t>(static_cast<uint8_t>(hdr[1])) << 8) |
                  (static_cast<uint32_t>(static_cast<uint8_t>(hdr[2])) << 16);

  if (plen > 16 * 1024 * 1024) return false;  // 16 MB limit
  if (plen == 0) {
    payload.clear();
    return true;
  }

  payload.resize(static_cast<int>(plen));
  return ReadExact(payload.data(), plen, timeout_ms);
}

// ---- Response parsing ------------------------------------------------------

QueryResult DatabaseWorker::ParseResponse(const QByteArray& data) {
  if (data.startsWith("OK ") || data.startsWith("ERR ")) {
    return ParseOkError(QString::fromUtf8(data));
  }
  return ParseResultSet(data);
}

QueryResult DatabaseWorker::ParseResultSet(const QByteArray& data) {
  QueryResult result;
  QString text = QString::fromUtf8(data);
  QStringList lines = text.split('\n', Qt::SkipEmptyParts);

  for (const QString& line : lines) {
    if (line.startsWith("Columns: ")) {
      continue;
    }
    if (line.startsWith("ColDef: ")) {
      QStringList parts = line.mid(8).split(' ');
      if (parts.size() >= 2) {
        ColumnInfo col;
        col.name = parts[0];
        col.type_name = parts[1];
        col.length = parts.size() >= 3 ? parts[2].toInt() : 0;
        result.columns.push_back(col);
      }
      continue;
    }
    if (line == "#ROWS" || line == "EOF") {
      continue;
    }
    if (line.startsWith("Row: ")) {
      QString row_text = line.mid(5);
      QStringList values = row_text.split('\t');
      std::vector<QString> row;
      for (const QString& val : values) {
        if (val == "\\N") {
          row.push_back(QString());
        } else {
          row.push_back(val);
        }
      }
      result.rows.push_back(row);
      result.affected_rows++;
      continue;
    }
  }

  return result;
}

QueryResult DatabaseWorker::ParseOkError(const QString& line) {
  QueryResult result;
  if (line.startsWith("ERR ")) {
    result.is_error = true;
    result.error_message = line.mid(4);
  } else if (line.startsWith("OK ")) {
    QStringList parts = line.mid(3).split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
      result.affected_rows = parts[0].toULongLong();
      result.last_insert_id = parts[1].toULongLong();
    }
  }
  return result;
}

void DatabaseWorker::SetLastError(const QString& err) {
  last_error_ = err;
}

}  // namespace studio
}  // namespace goods_db
