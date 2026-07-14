#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

class ConnectionPool;

// =============================================================================
// QueryExecutor — executes SQL statements against the active connection
//
// Uses QTimer to execute asynchronously without blocking the UI.
// Splits multi-statement SQL by semicolons and executes each in order.
// =============================================================================
class QueryExecutor : public QObject {
  Q_OBJECT

 public:
  explicit QueryExecutor(ConnectionPool* pool, QObject* parent = nullptr);

  void Execute(const QString& sql);
  void Stop();

  bool IsRunning() const { return running_; }

 signals:
  void ExecutionStarted(const QString& sql);
  void ExecutionComplete(const QueryResult& result);
  void ExecutionError(const QString& error);

 private:
  ConnectionPool* pool_;
  bool running_ = false;

  static std::vector<QString> SplitSql(const QString& sql);
};

}  // namespace studio
}  // namespace goods_db
