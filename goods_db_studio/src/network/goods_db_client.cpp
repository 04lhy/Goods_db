#include "network/goods_db_client.h"
#include "network/database_worker.h"

#include <QMetaObject>

namespace goods_db {
namespace studio {

// Register QueryResult with Qt meta-type system so it can be passed across
// queued connections and BlockingQueuedConnection invocations.
// Must be done before any signal/slot connections are made.
static const int kQueryResultMetaTypeId = []() {
  qRegisterMetaType<QueryResult>("QueryResult");
  return 0;
}();

GoodsDbClient::GoodsDbClient(QObject* parent) : QObject(parent) {
  // Create worker and its thread
  worker_ = new DatabaseWorker();  // No parent — will be moved to thread
  worker_thread_ = new QThread(this);

  // Move worker to the thread
  worker_->moveToThread(worker_thread_);

  // Connect signals from worker to client (queued connections since
  // worker is in a different thread)
  connect(worker_, &DatabaseWorker::Connected,
          this, &GoodsDbClient::OnWorkerConnected,
          Qt::QueuedConnection);
  connect(worker_, &DatabaseWorker::Disconnected,
          this, &GoodsDbClient::OnWorkerDisconnected,
          Qt::QueuedConnection);
  connect(worker_, &DatabaseWorker::ErrorOccurred,
          this, &GoodsDbClient::OnWorkerError,
          Qt::QueuedConnection);
  connect(worker_, &DatabaseWorker::QueryResultReady,
          this, &GoodsDbClient::OnWorkerQueryResult,
          Qt::QueuedConnection);

  worker_thread_->start();
}

GoodsDbClient::~GoodsDbClient() {
  // Disconnect socket on worker thread first (blocking, ensures clean shutdown)
  if (worker_) {
    QMetaObject::invokeMethod(worker_, "DoDisconnect",
                              Qt::BlockingQueuedConnection);
  }

  // Stop the worker thread event loop
  worker_thread_->quit();
  worker_thread_->wait(5000);

  // Manually delete the worker now that the thread is stopped.
  // deleteLater() would post an event to the worker thread's event loop, but
  // after quit() + wait() the loop may already be stopped, so we delete directly.
  delete worker_;
  worker_ = nullptr;
}

// ---- Connection management ------------------------------------------------

void GoodsDbClient::Connect(const QString& host, uint16_t port) {
  host_ = host;
  port_ = port;

  bool ok = false;
  QString error;
  QMetaObject::invokeMethod(worker_, "DoConnect",
                            Qt::BlockingQueuedConnection,
                            Q_ARG(const QString&, host),
                            Q_ARG(uint16_t, port),
                            Q_ARG(bool*, &ok),
                            Q_ARG(QString*, &error));

  if (ok) {
    connected_ = true;
  } else {
    connected_ = false;
    last_error_ = error;
    emit ErrorOccurred(error);
  }
}

void GoodsDbClient::Disconnect() {
  QMetaObject::invokeMethod(worker_, "DoDisconnect",
                            Qt::BlockingQueuedConnection);
  connected_ = false;
}

bool GoodsDbClient::IsConnected() const {
  // Read from the worker thread safely
  bool result = false;
  QMetaObject::invokeMethod(
      const_cast<DatabaseWorker*>(worker_), "DoIsConnected",
      Qt::BlockingQueuedConnection,
      Q_ARG(bool*, &result));
  return result;
}

// ---- Authentication -------------------------------------------------------

bool GoodsDbClient::Authenticate(const QString& user, const QString& password,
                                  const QString& db) {
  bool ok = false;
  QString error;
  QMetaObject::invokeMethod(worker_, "DoAuthenticate",
                            Qt::BlockingQueuedConnection,
                            Q_ARG(const QString&, user),
                            Q_ARG(const QString&, password),
                            Q_ARG(const QString&, db),
                            Q_ARG(bool*, &ok),
                            Q_ARG(QString*, &error));

  if (!ok) {
    last_error_ = error;
  }
  return ok;
}

// ---- Commands -------------------------------------------------------------

bool GoodsDbClient::Ping() {
  bool ok = false;
  QMetaObject::invokeMethod(worker_, "DoPing",
                            Qt::BlockingQueuedConnection,
                            Q_ARG(bool*, &ok));
  return ok;
}

QueryResult GoodsDbClient::Execute(const QString& sql) {
  QueryResult result;
  QMetaObject::invokeMethod(worker_, "DoExecute",
                            Qt::BlockingQueuedConnection,
                            Q_ARG(const QString&, sql),
                            Q_ARG(QueryResult*, &result));
  if (result.is_error) {
    last_error_ = result.error_message;
  }
  emit QueryComplete(result);
  return result;
}

// ---- Private slots --------------------------------------------------------

void GoodsDbClient::OnWorkerConnected() {
  connected_ = true;
  emit Connected();
}

void GoodsDbClient::OnWorkerDisconnected() {
  connected_ = false;
  emit Disconnected();
}

void GoodsDbClient::OnWorkerError(const QString& error) {
  last_error_ = error;
  emit ErrorOccurred(error);
}

void GoodsDbClient::OnWorkerQueryResult(const QueryResult& result) {
  // Forward the result — QueryComplete is also emitted directly in Execute()
  // for the synchronous path, so this slot handles the async path if used.
  if (result.is_error) {
    last_error_ = result.error_message;
  }
  emit QueryComplete(result);
}

}  // namespace studio
}  // namespace goods_db
