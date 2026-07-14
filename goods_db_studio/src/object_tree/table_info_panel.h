#pragma once

#include <QWidget>
#include <QLabel>
#include <QTableWidget>
#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

// =============================================================================
// TableInfoPanel — shows column definitions for the selected table
// =============================================================================
class TableInfoPanel : public QWidget {
  Q_OBJECT

 public:
  explicit TableInfoPanel(QWidget* parent = nullptr);

  void ShowTableInfo(const QString& db, const QString& table,
                     const std::vector<ColumnInfo>& columns);
  void Clear();

 private:
  QLabel* title_label_;
  QTableWidget* columns_table_;
};

}  // namespace studio
}  // namespace goods_db
