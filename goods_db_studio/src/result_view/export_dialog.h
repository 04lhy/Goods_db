#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>

namespace goods_db {
namespace studio {

// =============================================================================
// ExportDialog — dialog for exporting query results
// =============================================================================
class ExportDialog : public QDialog {
  Q_OBJECT

 public:
  enum ExportFormat { CSV, JSON, SQL_INSERT };

  explicit ExportDialog(QWidget* parent = nullptr);

  QString GetFilePath() const;
  ExportFormat GetFormat() const;
  QString GetTableName() const;

 private slots:
  void OnBrowse();

 private:
  QLineEdit* path_edit_;
  QComboBox* format_combo_;
  QLineEdit* table_name_edit_;
};

}  // namespace studio
}  // namespace goods_db
