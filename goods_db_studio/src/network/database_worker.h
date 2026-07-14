#pragma once

#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QByteArray>
#include <memory>
#include <vector>

#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

// =============================================================================
// DatabaseWorker — performs all blocking TCP I/O on a dedicated worker thread
//
// This class owns the QTcpSocket and executes Connect/Auth/Execute synchronously
// on its own thread. The main thread communicates via QMetaObject::invokeMethod
// with Qt::BlockingQueuedConnection, which is safe because the worker thread
// never blocks waiting for the main thread.
//
// In Qt6, QTcpSocket::waitForReadyRead() is unreliable on the main thread
// because it creates a nested QEventLoop that can interfere with the main
// event dispatcher. Moving blocking I/O to a worker thread fixes this.
// =============================================================================
class DatabaseWorker : public QObject {
  Q_OBJECT

 public:
  explicit DatabaseWorker(QObject* parent = nullptr);
  ~DatabaseWorker() override;

  // ---- Thread-safe I/O operations (called from main thread) ----------------
  // These are invoked via QMetaObject::invokeMethod with BlockingQueuedConnection.
  // They execute on the worker thread and return results via output parameters.
  // Marked Q_INVOKABLE so Qt6's meta-object system can discover them.

  Q_INVOKABLE void DoConnect(const QString& host, uint16_t port,
                              bool* ok, QString* error);
  Q_INVOKABLE void DoDisconnect();
  Q_INVOKABLE void DoAuthenticate(const QString& user, const QString& password,
                                   const QString& db, bool* ok, QString* error);
  Q_INVOKABLE void DoPing(bool* ok);
  Q_INVOKABLE void DoExecute(const QString& sql, QueryResult* result);
  Q_INVOKABLE void DoIsConnected(bool* connected);

  // ---- Slot-based operations (async, emit signals on completion) -----------
  // These are invoked via QMetaObject::invokeMethod with QueuedConnection.

 public slots:
  void ConnectAsync(const QString& host, uint16_t port);
  void DisconnectAsync();
  void AuthenticateAsync(const QString& user, const QString& password,
                         const QString& db);
  void PingAsync();
  void ExecuteAsync(const QString& sql);

 signals:
  void Connected();
  void Disconnected();
  void ErrorOccurred(const QString& error);
  void AuthResult(bool success, const QString& error);
  void PingResult(bool success);
  void QueryResultReady(const QueryResult& result);

 private:
  QTcpSocket* socket_ = nullptr;

  // Protocol helpers (run on worker thread)
  bool WritePacket(const QByteArray& data);
  bool ReadPacket(QByteArray& payload, int timeout_ms = 5000);
  bool WriteAllToSocket(const QByteArray& data);
  bool ReadExact(char* buf, qint64 count, int timeout_ms);

  QueryResult ParseResponse(const QByteArray& data);
  QueryResult ParseResultSet(const QByteArray& data);
  QueryResult ParseOkError(const QString& line);

  void SetLastError(const QString& err);

  QString last_error_;
  QString host_;
  uint16_t port_ = 0;
};

}  // namespace studio
}  // namespace goods_db
