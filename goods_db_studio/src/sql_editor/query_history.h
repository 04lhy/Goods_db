#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <cstdint>

namespace goods_db {
namespace studio {

struct HistoryEntry {
  int id = 0;
  QString sql;
  QString connection;
  QString timestamp;
  uint64_t exec_time_ms = 0;
  bool success = true;
};

// =============================================================================
// QueryHistory — persists executed SQL queries in local SQLite
//
// Stores up to the 1000 most recent queries with timestamp, connection info,
// execution time, and success status. Supports search by keyword.
// =============================================================================
class QueryHistory : public QObject {
  Q_OBJECT

 public:
  explicit QueryHistory(QObject* parent = nullptr);
  ~QueryHistory() override;

  void AddEntry(const QString& sql, const QString& connection,
                uint64_t exec_time_ms, bool success);
  std::vector<HistoryEntry> GetRecentEntries(int limit = 100) const;
  std::vector<HistoryEntry> Search(const QString& keyword) const;
  void Clear();

 private:
  QString db_path_;
  void InitializeDatabase();
};

}  // namespace studio
}  // namespace goods_db
