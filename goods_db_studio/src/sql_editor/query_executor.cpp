#include "sql_editor/query_executor.h"

#include "connection/connection_pool.h"

namespace goods_db {
namespace studio {

QueryExecutor::QueryExecutor(ConnectionPool* pool, QObject* parent)
    : QObject(parent), pool_(pool) {}

void QueryExecutor::Execute(const QString& sql) {
  auto* conn = pool_->GetActiveConnection();
  if (!conn || !conn->IsConnected()) {
    QueryResult error_result;
    error_result.is_error = true;
    error_result.error_message = "No active connection";
    emit ExecutionComplete(error_result);
    return;
  }

  // Split into individual statements
  std::vector<QString> statements = SplitSql(sql);
  if (statements.empty()) {
    QueryResult empty;
    emit ExecutionComplete(empty);
    return;
  }

  running_ = true;
  emit ExecutionStarted(sql);

  // Execute all statements synchronously, showing the last result
  QueryResult last_result;
  for (size_t i = 0; i < statements.size(); i++) {
    if (!running_) break;

    const QString& stmt = statements[i];
    QueryResult result = conn->Execute(stmt);

    if (result.is_error) {
      emit ExecutionComplete(result);
      running_ = false;
      return;
    }

    last_result = result;

    // If this is a result-set query (SELECT), show it and continue
    // For last statement or SELECT, this is what we show
  }

  emit ExecutionComplete(last_result);
  running_ = false;
}

void QueryExecutor::Stop() {
  running_ = false;
}

std::vector<QString> QueryExecutor::SplitSql(const QString& sql) {
  std::vector<QString> statements;
  QString current;
  bool in_string = false;
  QChar string_char;

  for (int i = 0; i < sql.size(); i++) {
    QChar c = sql[i];
    if (in_string) {
      current += c;
      if (c == string_char && i > 0 && sql[i - 1] != '\\') {
        in_string = false;
      }
    } else if (c == '\'' || c == '"') {
      in_string = true;
      string_char = c;
      current += c;
    } else if (c == ';') {
      QString stmt = current.trimmed();
      if (!stmt.isEmpty()) {
        statements.push_back(stmt);
      }
      current.clear();
    } else {
      current += c;
    }
  }

  // Last statement without trailing semicolon
  QString stmt = current.trimmed();
  if (!stmt.isEmpty()) {
    statements.push_back(stmt);
  }

  return statements;
}

}  // namespace studio
}  // namespace goods_db
