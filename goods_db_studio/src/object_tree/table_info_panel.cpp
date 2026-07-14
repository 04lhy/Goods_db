#include "object_tree/table_info_panel.h"

#include <QHeaderView>
#include <QVBoxLayout>

namespace goods_db {
namespace studio {

TableInfoPanel::TableInfoPanel(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);

  title_label_ = new QLabel(tr("Select a table..."));
  title_label_->setStyleSheet(
      "font-weight: bold; font-size: 12pt; padding: 8px 4px;");
  layout->addWidget(title_label_);

  columns_table_ = new QTableWidget(this);
  columns_table_->setColumnCount(2);
  columns_table_->setHorizontalHeaderLabels({tr("Column"), tr("Type")});
  columns_table_->horizontalHeader()->setStretchLastSection(true);
  columns_table_->verticalHeader()->setVisible(false);
  columns_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  columns_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(columns_table_);
}

void TableInfoPanel::ShowTableInfo(const QString& db, const QString& table,
                                    const std::vector<ColumnInfo>& columns) {
  title_label_->setText(QString("%1.%2").arg(db).arg(table));

  columns_table_->setRowCount(static_cast<int>(columns.size()));
  for (int i = 0; i < static_cast<int>(columns.size()); i++) {
    columns_table_->setItem(i, 0,
                            new QTableWidgetItem(columns[i].name));
    QString type_str = columns[i].type_name;
    if (columns[i].length > 0) {
      type_str += QString("(%1)").arg(columns[i].length);
    }
    columns_table_->setItem(i, 1, new QTableWidgetItem(type_str));
  }

  columns_table_->resizeColumnsToContents();

  if (columns.empty()) {
    columns_table_->setRowCount(0);
  }
}

void TableInfoPanel::Clear() {
  title_label_->setText(tr("No table selected"));
  columns_table_->setRowCount(0);
}

}  // namespace studio
}  // namespace goods_db
