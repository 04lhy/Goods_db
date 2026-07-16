#pragma once

#include <QWizard>
#include <QWizardPage>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QProgressBar>
#include <QRadioButton>
#include <QCheckBox>
#include <QProcess>
#include <QLabel>

#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

class ConnectionPool;

// =============================================================================
// BackupWizard — step-by-step backup and restore wizard
//
// Pages:
//   Page 1: Select operation (Full / Incremental / Restore)
//   Page 2: Operation-specific settings (DB/table selection, file path, options)
//   Page 3: Execution (progress bar + real-time log)
//
// Uses QProcess to invoke goods_db_dump CLI tool for backup/restore operations,
// or falls back to SQL commands via GoodsDbClient if the CLI is unavailable.
// =============================================================================
class BackupWizard : public QWizard {
  Q_OBJECT

  friend class OperationPage;
  friend class FullBackupPage;
  friend class IncrementalBackupPage;
  friend class RestorePage;
  friend class ExecutePage;

 public:
  explicit BackupWizard(ConnectionPool* pool, QWidget* parent = nullptr);

 private:
  ConnectionPool* pool_;

  // Helpers
  QString GetActiveConnectionString() const;
  QueryResult ExecuteSql(const QString& sql);

  // Pages
  QWizardPage* CreateOperationPage();
  QWizardPage* CreateFullBackupPage();
  QWizardPage* CreateIncrementalBackupPage();
  QWizardPage* CreateRestorePage();
  QWizardPage* CreateExecutePage();

  // ---- Page 1 widgets ----
  QRadioButton* full_backup_radio_;
  QRadioButton* incremental_backup_radio_;
  QRadioButton* restore_radio_;

  // ---- Page 2a: Full backup ----
  QTreeWidget* db_table_tree_;
  QLineEdit* backup_path_edit_;
  QPushButton* browse_path_btn_;
  QCheckBox* compress_cb_;
  QCheckBox* single_transaction_cb_;

  // ---- Page 2b: Incremental backup ----
  QLabel* last_position_label_;
  QLineEdit* inc_backup_path_edit_;
  QPushButton* inc_browse_path_btn_;

  // ---- Page 2c: Restore ----
  QLineEdit* restore_file_edit_;
  QPushButton* restore_browse_btn_;
  QTextEdit* preview_text_;

  // ---- Page 3: Execute ----
  QProgressBar* progress_bar_;
  QTextEdit* exec_log_;
  QLabel* status_label_;
  QProcess* dump_process_ = nullptr;

 private slots:
  void OnOperationChanged(int id);
  void OnBrowseBackupPath();
  void OnBrowseRestoreFile();
  void OnRestoreFileChanged(const QString& path);
  void OnBrowseIncBackupPath();
  void ExecuteBackup();
  void ExecuteRestore();
  void OnProcessOutput();
  void OnProcessFinished(int exit_code, QProcess::ExitStatus status);
  void OnProcessError(QProcess::ProcessError error);
  void RefreshDbTree();

 public:
  void accept() override;  // Triggers execution when Finish is clicked
  int nextId() const override;  // Dynamic page routing
};

// =============================================================================
// Individual wizard page classes
// =============================================================================

class OperationPage : public QWizardPage {
  Q_OBJECT
 public:
  explicit OperationPage(BackupWizard* wizard);
};

class FullBackupPage : public QWizardPage {
  Q_OBJECT
 public:
  explicit FullBackupPage(BackupWizard* wizard);
};

class IncrementalBackupPage : public QWizardPage {
  Q_OBJECT
 public:
  explicit IncrementalBackupPage(BackupWizard* wizard);
};

class RestorePage : public QWizardPage {
  Q_OBJECT
 public:
  explicit RestorePage(BackupWizard* wizard);
};

class ExecutePage : public QWizardPage {
  Q_OBJECT
 public:
  explicit ExecutePage(BackupWizard* wizard);
};

}  // namespace studio
}  // namespace goods_db
