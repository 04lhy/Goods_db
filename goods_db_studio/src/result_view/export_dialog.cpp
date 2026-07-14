#include "result_view/export_dialog.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

namespace goods_db {
namespace studio {

ExportDialog::ExportDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Export Results"));
  setMinimumWidth(450);

  auto* layout = new QVBoxLayout(this);

  // Format selection
  auto* form = new QFormLayout();

  format_combo_ = new QComboBox();
  format_combo_->addItem(tr("CSV (Comma-Separated Values)"), CSV);
  format_combo_->addItem(tr("JSON (JavaScript Object Notation)"), JSON);
  format_combo_->addItem(tr("SQL INSERT Statements"), SQL_INSERT);
  form->addRow(tr("Format:"), format_combo_);

  table_name_edit_ = new QLineEdit();
  table_name_edit_->setPlaceholderText(tr("table_name"));
  table_name_edit_->setEnabled(false);
  form->addRow(tr("Table Name:"), table_name_edit_);

  layout->addLayout(form);

  // File path
  auto* path_layout = new QHBoxLayout();
  path_edit_ = new QLineEdit();
  path_edit_->setPlaceholderText(tr("Select output file..."));
  path_layout->addWidget(path_edit_);

  auto* browse_btn = new QPushButton(tr("Browse..."));
  path_layout->addWidget(browse_btn);
  layout->addLayout(path_layout);

  // Enable table_name only for SQL INSERT
  connect(format_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this](int idx) {
    table_name_edit_->setEnabled(
        format_combo_->itemData(idx).toInt() == SQL_INSERT);
  });

  connect(browse_btn, &QPushButton::clicked, this, &ExportDialog::OnBrowse);

  // OK/Cancel
  auto* btn_layout = new QHBoxLayout();
  auto* ok_btn = new QPushButton(tr("Export"));
  auto* cancel_btn = new QPushButton(tr("Cancel"));
  btn_layout->addStretch();
  btn_layout->addWidget(ok_btn);
  btn_layout->addWidget(cancel_btn);
  layout->addLayout(btn_layout);

  connect(ok_btn, &QPushButton::clicked, this, &QDialog::accept);
  connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
}

QString ExportDialog::GetFilePath() const {
  return path_edit_->text();
}

ExportDialog::ExportFormat ExportDialog::GetFormat() const {
  return static_cast<ExportFormat>(format_combo_->currentData().toInt());
}

QString ExportDialog::GetTableName() const {
  return table_name_edit_->text();
}

void ExportDialog::OnBrowse() {
  QString filter;
  switch (GetFormat()) {
    case CSV: filter = tr("CSV Files (*.csv);;All Files (*)"); break;
    case JSON: filter = tr("JSON Files (*.json);;All Files (*)"); break;
    case SQL_INSERT: filter = tr("SQL Files (*.sql);;All Files (*)"); break;
  }

  QString path = QFileDialog::getSaveFileName(this, tr("Export To"),
                                               QString(), filter);
  if (!path.isEmpty()) {
    path_edit_->setText(path);
  }
}

}  // namespace studio
}  // namespace goods_db
