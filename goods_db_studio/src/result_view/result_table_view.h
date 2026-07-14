#pragma once

#include <QWidget>
#include <QTableView>
#include <QLabel>
#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

class ResultTableModel;
class ResultPagination;

// =============================================================================
// ResultTableView — table view + pagination for query results
//
// Features:
//   - Auto-resizing columns
//   - Alternate row colors
//   - Pagination for large result sets (> 1000 rows)
//   - Right-click context menu for export
//   - Row count status label
// =============================================================================
class ResultTableView : public QWidget {
  Q_OBJECT

 public:
  explicit ResultTableView(QWidget* parent = nullptr);

  void SetResult(const QueryResult& result);
  void Clear();

  void ExportToCsv(const QString& file_path);
  void ExportToJson(const QString& file_path);
  void ExportToSqlInsert(const QString& table_name, const QString& file_path);

 signals:
  void ExportRequested();

 private:
  QTableView* table_view_;
  ResultTableModel* model_;
  ResultPagination* pagination_;
  QLabel* status_label_;

  void SetupContextMenu();
  QueryResult GetPageData(const QueryResult& full_result,
                          int page, int page_size) const;
};

}  // namespace studio
}  // namespace goods_db
