#include "result_view/result_table_view.h"
#include "result_view/result_table_model.h"
#include "result_view/result_pagination.h"

#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QTextStream>
#include <QVBoxLayout>

namespace goods_db {
namespace studio {

ResultTableView::ResultTableView(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Table view
  table_view_ = new QTableView(this);
  model_ = new ResultTableModel(this);
  table_view_->setModel(model_);
  table_view_->setAlternatingRowColors(true);
  table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_view_->horizontalHeader()->setStretchLastSection(true);
  table_view_->verticalHeader()->setVisible(true);
  table_view_->setSortingEnabled(true);
  table_view_->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(table_view_, 1);

  connect(table_view_, &QTableView::customContextMenuRequested,
          [this](const QPoint&) { SetupContextMenu(); });

  // Pagination
  pagination_ = new ResultPagination(this);
  pagination_->setVisible(false);
  layout->addWidget(pagination_);

  connect(pagination_, &ResultPagination::PageChanged,
          [this](int page, int offset) {
    // Re-display with page offset
    (void)page;
    (void)offset;
  });

  // Status label
  status_label_ = new QLabel(this);
  status_label_->setStyleSheet("padding: 4px 8px; color: #888;");
  layout->addWidget(status_label_);
}

void ResultTableView::SetResult(const QueryResult& result) {
  if (result.rows.size() > 1000) {
    // Use pagination
    pagination_->SetTotalRows(static_cast<int64_t>(result.rows.size()));
    pagination_->setVisible(true);

    QueryResult page_data = GetPageData(result, 1, 1000);
    model_->SetResult(page_data);
  } else {
    pagination_->setVisible(false);
    model_->SetResult(result);
  }

  table_view_->resizeColumnsToContents();
  status_label_->setText(
      tr("%1 rows × %2 columns")
          .arg(result.rows.size())
          .arg(result.columns.size()));
}

void ResultTableView::Clear() {
  model_->Clear();
  pagination_->setVisible(false);
  status_label_->clear();
}

void ResultTableView::SetupContextMenu() {
  QMenu menu(this);
  menu.addAction(tr("Export as CSV..."), this, [this]() { emit ExportRequested(); });
  menu.addAction(tr("Export as JSON..."), this, [this]() { emit ExportRequested(); });
  menu.addAction(tr("Export as SQL INSERT..."), this, [this]() { emit ExportRequested(); });
  menu.addSeparator();
  menu.addAction(tr("Copy Selected Rows"), table_view_, [this]() {
    auto selection = table_view_->selectionModel()->selectedRows();
    if (selection.isEmpty()) return;
    // Copy to clipboard (simplified)
  });
  menu.exec(QCursor::pos());
}

void ResultTableView::ExportToCsv(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QFile::WriteOnly | QFile::Text)) return;
  QTextStream out(&file);

  const auto& result = model_->GetResult();

  // Header
  for (size_t i = 0; i < result.columns.size(); i++) {
    if (i > 0) out << ",";
    out << "\"" << result.columns[i].name << "\"";
  }
  out << "\n";

  // Data
  for (const auto& row : result.rows) {
    for (size_t i = 0; i < row.size(); i++) {
      if (i > 0) out << ",";
      if (row[i].isEmpty()) {
        out << "NULL";
      } else {
        QString escaped = row[i];
        escaped.replace("\"", "\"\"");
        out << "\"" << escaped << "\"";
      }
    }
    out << "\n";
  }
  file.close();
}

void ResultTableView::ExportToJson(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QFile::WriteOnly | QFile::Text)) return;

  const auto& result = model_->GetResult();
  QJsonArray array;

  for (const auto& row : result.rows) {
    QJsonObject obj;
    for (size_t i = 0; i < row.size() && i < result.columns.size(); i++) {
      if (row[i].isEmpty()) {
        obj[result.columns[i].name] = QJsonValue::Null;
      } else {
        obj[result.columns[i].name] = row[i];
      }
    }
    array.append(obj);
  }

  QJsonDocument doc(array);
  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();
}

void ResultTableView::ExportToSqlInsert(const QString& table_name,
                                         const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QFile::WriteOnly | QFile::Text)) return;
  QTextStream out(&file);

  const auto& result = model_->GetResult();

  for (const auto& row : result.rows) {
    out << "INSERT INTO " << table_name << " VALUES (";
    for (size_t i = 0; i < row.size(); i++) {
      if (i > 0) out << ", ";
      if (row[i].isEmpty()) {
        out << "NULL";
      } else {
        // Simple escaping
        QString escaped = row[i];
        escaped.replace("'", "\\'");
        out << "'" << escaped << "'";
      }
    }
    out << ");\n";
  }
  file.close();
}

QueryResult ResultTableView::GetPageData(const QueryResult& full_result,
                                          int page, int page_size) const {
  QueryResult page_result;
  page_result.columns = full_result.columns;

  int start = (page - 1) * page_size;
  int end = std::min(start + page_size, static_cast<int>(full_result.rows.size()));

  for (int i = start; i < end; i++) {
    page_result.rows.push_back(full_result.rows[i]);
  }

  page_result.affected_rows = full_result.rows.size();
  return page_result;
}

}  // namespace studio
}  // namespace goods_db
