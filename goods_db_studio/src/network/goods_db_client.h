#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <memory>
#include <vector>

namespace goods_db {
namespace studio {

class DatabaseWorker;

// =============================================================================
// Query result structures
// =============================================================================

struct ColumnInfo {
  QString name;
  QString type_name;
  int length = 0;
};

struct QueryResult {
  std::vector<ColumnInfo> columns;
  std::vector<std::vector<QString>> rows;
  uint64_t affected_rows = 0;
  uint64_t last_insert_id = 0;
  bool is_error = false;
  QString error_message;
  uint64_t exec_time_ms = 0;
};

// =============================================================================
// GoodsDbClient — TCP client for goods_db server protocol
//
// Communicates with goods_db server over TCP using a text-based protocol.
// All blocking I/O is delegated to a DatabaseWorker on a dedicated QThread,
// which is the Qt6-safe pattern — QTcpSocket::waitForReadyRead() on the main
// thread creates nested event loops that can interfere with the dispatcher.
//
// The public API (Connect, Authenticate, Execute) remains synchronous from the
// caller's perspective via Qt::BlockingQueuedConnection, which blocks the
// calling thread until the worker completes.
// =============================================================================
class GoodsDbClient : public QObject {
  Q_OBJECT

 public:
  explicit GoodsDbClient(QObject* parent = nullptr);
  ~GoodsDbClient() override;

  // ---- Connection management ------------------------------------------------
  void Connect(const QString& host, uint16_t port);
  void Disconnect();
  bool IsConnected() const;

  // ---- Authentication -------------------------------------------------------
  bool Authenticate(const QString& user, const QString& password,
                    const QString& db = QString());

  // ---- Commands -------------------------------------------------------------
  bool Ping();
  QueryResult Execute(const QString& sql);

  // ---- Properties -----------------------------------------------------------
  QString GetHost() const { return host_; }
  uint16_t GetPort() const { return port_; }
  QString GetLastError() const { return last_error_; }

 signals:
  void Connected();
  void Disconnected();
  void QueryComplete(const QueryResult& result);
  void ErrorOccurred(const QString& error);

 private slots:
  void OnWorkerConnected();
  void OnWorkerDisconnected();
  void OnWorkerError(const QString& error);
  void OnWorkerQueryResult(const QueryResult& result);

 private:
  DatabaseWorker* worker_ = nullptr;
  QThread* worker_thread_ = nullptr;

  QString host_;
  uint16_t port_ = 0;
  QString last_error_;
  bool connected_ = false;
};

}  // namespace studio
}  // namespace goods_db
