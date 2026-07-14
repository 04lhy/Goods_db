#include "sql_editor/query_history.h"

#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

namespace goods_db {
namespace studio {

QueryHistory::QueryHistory(QObject* parent) : QObject(parent) {
  QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(data_dir);
  db_path_ = data_dir + "/query_history.db";
  InitializeDatabase();
}

QueryHistory::~QueryHistory() {
  QSqlDatabase::database("query_history").close();
  QSqlDatabase::removeDatabase("query_history");
}

void QueryHistory::InitializeDatabase() {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "query_history");
  db.setDatabaseName(db_path_);

  if (!db.open()) {
    return;
  }

  QSqlQuery query(db);
  query.exec(
      "CREATE TABLE IF NOT EXISTS query_history ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  sql TEXT NOT NULL,"
      "  connection TEXT,"
      "  timestamp TEXT NOT NULL,"
      "  exec_time_ms INTEGER DEFAULT 0,"
      "  success INTEGER DEFAULT 1"
      ")");

  // Keep only the most recent 1000 entries
  query.exec(
      "DELETE FROM query_history WHERE id NOT IN "
      "(SELECT id FROM query_history ORDER BY id DESC LIMIT 1000)");
}

void QueryHistory::AddEntry(const QString& sql, const QString& connection,
                             uint64_t exec_time_ms, bool success) {
  QSqlDatabase db = QSqlDatabase::database("query_history");
  if (!db.isOpen()) return;

  QSqlQuery query(db);
  query.prepare(
      "INSERT INTO query_history (sql, connection, timestamp, exec_time_ms, success) "
      "VALUES (:sql, :conn, :ts, :time, :ok)");
  query.bindValue(":sql", sql);
  query.bindValue(":conn", connection);
  query.bindValue(":ts", QDateTime::currentDateTime().toString(Qt::ISODate));
  query.bindValue(":time", static_cast<qint64>(exec_time_ms));
  query.bindValue(":ok", success ? 1 : 0);
  query.exec();

  // Enforce 1000-entry limit
  query.exec(
      "DELETE FROM query_history WHERE id NOT IN "
      "(SELECT id FROM query_history ORDER BY id DESC LIMIT 1000)");
}

std::vector<HistoryEntry> QueryHistory::GetRecentEntries(int limit) const {
  std::vector<HistoryEntry> entries;
  QSqlDatabase db = QSqlDatabase::database("query_history");
  if (!db.isOpen()) return entries;

  QSqlQuery query(db);
  query.prepare(
      "SELECT id, sql, connection, timestamp, exec_time_ms, success "
      "FROM query_history ORDER BY id DESC LIMIT :lim");
  query.bindValue(":lim", limit);
  query.exec();

  while (query.next()) {
    HistoryEntry entry;
    entry.id = query.value(0).toInt();
    entry.sql = query.value(1).toString();
    entry.connection = query.value(2).toString();
    entry.timestamp = query.value(3).toString();
    entry.exec_time_ms = query.value(4).toULongLong();
    entry.success = query.value(5).toBool();
    entries.push_back(entry);
  }

  return entries;
}

std::vector<HistoryEntry> QueryHistory::Search(const QString& keyword) const {
  std::vector<HistoryEntry> entries;
  QSqlDatabase db = QSqlDatabase::database("query_history");
  if (!db.isOpen()) return entries;

  QSqlQuery query(db);
  query.prepare(
      "SELECT id, sql, connection, timestamp, exec_time_ms, success "
      "FROM query_history WHERE sql LIKE :kw ORDER BY id DESC LIMIT 200");
  query.bindValue(":kw", "%" + keyword + "%");
  query.exec();

  while (query.next()) {
    HistoryEntry entry;
    entry.id = query.value(0).toInt();
    entry.sql = query.value(1).toString();
    entry.connection = query.value(2).toString();
    entry.timestamp = query.value(3).toString();
    entry.exec_time_ms = query.value(4).toULongLong();
    entry.success = query.value(5).toBool();
    entries.push_back(entry);
  }

  return entries;
}

void QueryHistory::Clear() {
  QSqlDatabase db = QSqlDatabase::database("query_history");
  if (!db.isOpen()) return;
  QSqlQuery query(db);
  query.exec("DELETE FROM query_history");
}

}  // namespace studio
}  // namespace goods_db
