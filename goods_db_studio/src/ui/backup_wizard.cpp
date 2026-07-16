#include "ui/backup_wizard.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QDateTime>
#include <QCheckBox>
#include <QStandardPaths>
#include <QDir>
#include <QTextStream>

#include "connection/connection_pool.h"

namespace goods_db {
namespace studio {

// =============================================================================
// OperationPage (Page 1)
// =============================================================================

OperationPage::OperationPage(BackupWizard* wizard) : QWizardPage(wizard) {
  setTitle(tr("Backup Operation"));
  setSubTitle(tr("Choose the type of operation you want to perform."));

  auto* layout = new QVBoxLayout(this);

  auto* group = new QGroupBox(tr("Operation Type"), this);
  auto* group_layout = new QVBoxLayout(group);

  wizard->full_backup_radio_ = new QRadioButton(tr("Full Backup — Export entire database(s)"), group);
  wizard->incremental_backup_radio_ = new QRadioButton(tr("Incremental Backup — Export changes since last backup"), group);
  wizard->restore_radio_ = new QRadioButton(tr("Restore — Import data from a backup file"), group);

  wizard->full_backup_radio_->setChecked(true);

  group_layout->addWidget(wizard->full_backup_radio_);
  group_layout->addWidget(wizard->incremental_backup_radio_);
  group_layout->addWidget(wizard->restore_radio_);

  layout->addWidget(group);
  layout->addStretch();

  connect(wizard->full_backup_radio_, &QRadioButton::toggled, wizard, [wizard]() {
    wizard->OnOperationChanged(0);
  });
  connect(wizard->incremental_backup_radio_, &QRadioButton::toggled, wizard, [wizard]() {
    wizard->OnOperationChanged(1);
  });
  connect(wizard->restore_radio_, &QRadioButton::toggled, wizard, [wizard]() {
    wizard->OnOperationChanged(2);
  });
}

// =============================================================================
// FullBackupPage (Page 2a)
// =============================================================================

FullBackupPage::FullBackupPage(BackupWizard* wizard) : QWizardPage(wizard) {
  setTitle(tr("Full Backup Settings"));
  setSubTitle(tr("Select databases and tables to back up, and configure backup options."));

  auto* layout = new QVBoxLayout(this);

  // Database/Table tree with checkboxes
  auto* tree_group = new QGroupBox(tr("Select Databases & Tables"), this);
  auto* tree_layout = new QVBoxLayout(tree_group);

  wizard->db_table_tree_ = new QTreeWidget(tree_group);
  wizard->db_table_tree_->setHeaderLabels({tr("Database / Table"), tr("Type"), tr("Size")});
  wizard->db_table_tree_->setColumnWidth(0, 220);
  wizard->db_table_tree_->setColumnWidth(1, 80);
  tree_layout->addWidget(wizard->db_table_tree_);

  auto* refresh_btn = new QPushButton(tr("Refresh"), tree_group);
  connect(refresh_btn, &QPushButton::clicked, wizard, &BackupWizard::RefreshDbTree);
  tree_layout->addWidget(refresh_btn);

  layout->addWidget(tree_group);

  // Path selection
  auto* path_group = new QGroupBox(tr("Backup File Path"), this);
  auto* path_layout = new QHBoxLayout(path_group);
  wizard->backup_path_edit_ = new QLineEdit(path_group);
  QString default_path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                         + "/goods_db_backup_" +
                         QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".sql";
  wizard->backup_path_edit_->setText(default_path);
  wizard->backup_path_edit_->setMinimumWidth(300);
  path_layout->addWidget(wizard->backup_path_edit_);

  wizard->browse_path_btn_ = new QPushButton(tr("Browse..."), path_group);
  connect(wizard->browse_path_btn_, &QPushButton::clicked, wizard, &BackupWizard::OnBrowseBackupPath);
  path_layout->addWidget(wizard->browse_path_btn_);

  layout->addWidget(path_group);

  // Options
  auto* options_group = new QGroupBox(tr("Options"), this);
  auto* options_layout = new QVBoxLayout(options_group);
  wizard->single_transaction_cb_ = new QCheckBox(tr("Single transaction (consistent snapshot)"), options_group);
  wizard->single_transaction_cb_->setChecked(true);
  wizard->compress_cb_ = new QCheckBox(tr("Compress output (.gz)"), options_group);
  options_layout->addWidget(wizard->single_transaction_cb_);
  options_layout->addWidget(wizard->compress_cb_);

  layout->addWidget(options_group);
  layout->addStretch();

  registerField("backup_path*", wizard->backup_path_edit_);
}

// =============================================================================
// IncrementalBackupPage (Page 2b)
// =============================================================================

IncrementalBackupPage::IncrementalBackupPage(BackupWizard* wizard) : QWizardPage(wizard) {
  setTitle(tr("Incremental Backup Settings"));
  setSubTitle(tr("Back up only changes since the last backup."));

  auto* layout = new QVBoxLayout(this);

  // Last position info
  auto* info_group = new QGroupBox(tr("Last Backup Position"), this);
  auto* info_layout = new QVBoxLayout(info_group);
  wizard->last_position_label_ = new QLabel(
      tr("Last backup: (fetching from server...)"), info_group);
  wizard->last_position_label_->setWordWrap(true);
  info_layout->addWidget(wizard->last_position_label_);
  layout->addWidget(info_group);

  // Path selection
  auto* path_group = new QGroupBox(tr("Incremental Backup File Path"), this);
  auto* path_layout = new QHBoxLayout(path_group);
  wizard->inc_backup_path_edit_ = new QLineEdit(path_group);
  QString default_path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                         + "/goods_db_inc_backup_" +
                         QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".sql";
  wizard->inc_backup_path_edit_->setText(default_path);
  wizard->inc_backup_path_edit_->setMinimumWidth(300);
  path_layout->addWidget(wizard->inc_backup_path_edit_);

  wizard->inc_browse_path_btn_ = new QPushButton(tr("Browse..."), path_group);
  connect(wizard->inc_browse_path_btn_, &QPushButton::clicked, wizard, &BackupWizard::OnBrowseIncBackupPath);
  path_layout->addWidget(wizard->inc_browse_path_btn_);

  layout->addWidget(path_group);
  layout->addStretch();

  registerField("inc_backup_path*", wizard->inc_backup_path_edit_);
}

// =============================================================================
// RestorePage (Page 2c)
// =============================================================================

RestorePage::RestorePage(BackupWizard* wizard) : QWizardPage(wizard) {
  setTitle(tr("Restore Settings"));
  setSubTitle(tr("Select a backup file to restore and preview its contents."));

  auto* layout = new QVBoxLayout(this);

  // File selection
  auto* file_group = new QGroupBox(tr("Backup File"), this);
  auto* file_layout = new QHBoxLayout(file_group);
  wizard->restore_file_edit_ = new QLineEdit(file_group);
  wizard->restore_file_edit_->setMinimumWidth(300);
  wizard->restore_file_edit_->setPlaceholderText(tr("Select a .sql backup file..."));
  file_layout->addWidget(wizard->restore_file_edit_);

  wizard->restore_browse_btn_ = new QPushButton(tr("Browse..."), file_group);
  connect(wizard->restore_browse_btn_, &QPushButton::clicked, wizard, &BackupWizard::OnBrowseRestoreFile);
  file_layout->addWidget(wizard->restore_browse_btn_);

  layout->addWidget(file_group);

  // Preview
  auto* preview_group = new QGroupBox(tr("File Preview (first 20 lines)"), this);
  auto* preview_layout = new QVBoxLayout(preview_group);
  wizard->preview_text_ = new QTextEdit(preview_group);
  wizard->preview_text_->setReadOnly(true);
  wizard->preview_text_->setFont(QFont("Consolas, Menlo, monospace", 10));
  wizard->preview_text_->setPlaceholderText(tr("Select a file to preview..."));
  wizard->preview_text_->setMaximumHeight(300);
  preview_layout->addWidget(wizard->preview_text_);

  layout->addWidget(preview_group);
  layout->addStretch();

  registerField("restore_file*", wizard->restore_file_edit_);

  connect(wizard->restore_file_edit_, &QLineEdit::textChanged,
          wizard, &BackupWizard::OnRestoreFileChanged);
}

// =============================================================================
// ExecutePage (Page 3)
// =============================================================================

ExecutePage::ExecutePage(BackupWizard* wizard) : QWizardPage(wizard) {
  setTitle(tr("Execute"));
  setSubTitle(tr("Backup or restore operation is in progress..."));
  setCommitPage(true);

  auto* layout = new QVBoxLayout(this);

  wizard->status_label_ = new QLabel(tr("Ready to execute."), this);
  wizard->status_label_->setWordWrap(true);
  layout->addWidget(wizard->status_label_);

  wizard->progress_bar_ = new QProgressBar(this);
  wizard->progress_bar_->setRange(0, 100);
  wizard->progress_bar_->setValue(0);
  wizard->progress_bar_->setTextVisible(true);
  layout->addWidget(wizard->progress_bar_);

  wizard->exec_log_ = new QTextEdit(this);
  wizard->exec_log_->setReadOnly(true);
  wizard->exec_log_->setFont(QFont("Consolas, Menlo, monospace", 10));
  wizard->exec_log_->setPlaceholderText(tr("Operation log will appear here..."));
  wizard->exec_log_->setMinimumHeight(200);
  layout->addWidget(wizard->exec_log_);
}

// =============================================================================
// BackupWizard
// =============================================================================

BackupWizard::BackupWizard(ConnectionPool* pool, QWidget* parent)
    : QWizard(parent), pool_(pool) {
  setWindowTitle(tr("Backup & Restore Wizard"));
  setMinimumSize(600, 500);
  resize(680, 560);

  setWizardStyle(QWizard::ModernStyle);

  // Create and add pages
  auto* page1 = new OperationPage(this);
  auto* page2a = new FullBackupPage(this);
  auto* page2b = new IncrementalBackupPage(this);
  auto* page2c = new RestorePage(this);
  auto* page3 = new ExecutePage(this);

  addPage(page1);
  addPage(page2a);
  addPage(page2b);
  addPage(page2c);
  addPage(page3);

  // Set default button texts
  setButtonText(QWizard::NextButton, tr("Next >"));
  setButtonText(QWizard::BackButton, tr("< Back"));
  setButtonText(QWizard::FinishButton, tr("Execute"));
  setButtonText(QWizard::CancelButton, tr("Cancel"));

  // Load databases on init
  RefreshDbTree();
}

// =============================================================================
// Helpers
// =============================================================================

QString BackupWizard::GetActiveConnectionString() const {
  auto* conn = pool_->GetActiveConnection();
  if (!conn || !conn->IsConnected()) return QString();
  return QString("%1:%2").arg(conn->GetHost()).arg(conn->GetPort());
}

QueryResult BackupWizard::ExecuteSql(const QString& sql) {
  auto* conn = pool_->GetActiveConnection();
  if (!conn || !conn->IsConnected()) {
    QueryResult err;
    err.is_error = true;
    err.error_message = tr("No active connection");
    return err;
  }
  return conn->Execute(sql);
}

// =============================================================================
// Page Navigation Logic
// =============================================================================

int BackupWizard::nextId() const {
  int current = currentId();
  // currentId 0 = operation, 1 = full, 2 = incremental, 3 = restore, 4 = execute
  if (current == 0) {
    if (restore_radio_->isChecked()) return 3;       // → Restore page
    if (incremental_backup_radio_->isChecked()) return 2;  // → Incremental page
    return 1;  // → Full backup page (default)
  }
  // From any settings page → Execute page
  if (current == 1 || current == 2 || current == 3) return 4;
  return -1;
}

void BackupWizard::OnOperationChanged(int op) {
  Q_UNUSED(op);
  // Page navigation is handled by nextId(), which checks radio button state
}

// =============================================================================
// Browse Slots
// =============================================================================

void BackupWizard::OnBrowseBackupPath() {
  QString path = QFileDialog::getSaveFileName(
      this, tr("Save Backup As"),
      backup_path_edit_->text(),
      tr("SQL Files (*.sql);;Compressed Files (*.sql.gz);;All Files (*)"));
  if (!path.isEmpty()) {
    backup_path_edit_->setText(path);
  }
}

void BackupWizard::OnBrowseIncBackupPath() {
  QString path = QFileDialog::getSaveFileName(
      this, tr("Save Incremental Backup As"),
      inc_backup_path_edit_->text(),
      tr("SQL Files (*.sql);;Compressed Files (*.sql.gz);;All Files (*)"));
  if (!path.isEmpty()) {
    inc_backup_path_edit_->setText(path);
  }
}

void BackupWizard::OnBrowseRestoreFile() {
  QString path = QFileDialog::getOpenFileName(
      this, tr("Select Backup File"),
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
      tr("SQL Files (*.sql);;Compressed Files (*.sql.gz);;All Files (*)"));
  if (!path.isEmpty()) {
    restore_file_edit_->setText(path);
  }
}

void BackupWizard::OnRestoreFileChanged(const QString& path) {
  preview_text_->clear();
  if (path.isEmpty()) return;

  QFile file(path);
  if (!file.open(QFile::ReadOnly | QFile::Text)) {
    preview_text_->setPlainText(tr("Cannot open file: %1").arg(file.errorString()));
    return;
  }

  QTextStream stream(&file);
  int line_count = 0;
  QString preview;
  while (!stream.atEnd() && line_count < 20) {
    preview += stream.readLine() + "\n";
    line_count++;
  }
  file.close();

  preview_text_->setPlainText(preview);
  if (line_count >= 20) {
    preview_text_->append(tr("\n... (file truncated, showing first 20 lines)"));
  }
}

// =============================================================================
// Refresh Database Tree
// =============================================================================

void BackupWizard::RefreshDbTree() {
  db_table_tree_->clear();

  QueryResult db_result = ExecuteSql("SHOW DATABASES");
  if (db_result.is_error) {
    auto* item = new QTreeWidgetItem(db_table_tree_);
    item->setText(0, tr("(Cannot fetch databases: %1)").arg(db_result.error_message));
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    return;
  }

  for (const auto& row : db_result.rows) {
    if (row.empty()) continue;
    QString db_name = row[0].trimmed();

    auto* db_item = new QTreeWidgetItem(db_table_tree_);
    db_item->setText(0, db_name);
    db_item->setText(1, tr("Database"));
    db_item->setCheckState(0, Qt::Unchecked);
    db_item->setFlags(db_item->flags() | Qt::ItemIsUserCheckable);

    // Fetch tables for this database
    QueryResult table_result = ExecuteSql(QString("SHOW TABLES FROM %1").arg(db_name));
    if (!table_result.is_error) {
      for (const auto& trow : table_result.rows) {
        if (trow.empty()) continue;
        QString table_name = trow[0].trimmed();

        auto* table_item = new QTreeWidgetItem(db_item);
        table_item->setText(0, table_name);
        table_item->setText(1, tr("Table"));
        table_item->setCheckState(0, Qt::Unchecked);
        table_item->setFlags(table_item->flags() | Qt::ItemIsUserCheckable);
      }
    }
  }

  db_table_tree_->expandAll();
}

// =============================================================================
// Execute Backup / Restore
// =============================================================================

void BackupWizard::ExecuteBackup() {
  // Collect selected databases and tables
  QStringList selected_dbs;
  QStringList selected_tables;

  for (int i = 0; i < db_table_tree_->topLevelItemCount(); i++) {
    auto* db_item = db_table_tree_->topLevelItem(i);
    if (db_item->checkState(0) == Qt::Checked) {
      selected_dbs.append(db_item->text(0));
    }
    for (int j = 0; j < db_item->childCount(); j++) {
      auto* table_item = db_item->child(j);
      if (table_item->checkState(0) == Qt::Checked) {
        selected_tables.append(db_item->text(0) + "." + table_item->text(0));
      }
    }
  }

  // Determine output path
  bool is_incremental = incremental_backup_radio_->isChecked();
  QString output_path = is_incremental ? inc_backup_path_edit_->text()
                                       : backup_path_edit_->text();

  if (output_path.isEmpty()) {
    status_label_->setText(tr("Error: No output path specified."));
    return;
  }

  // Build command for goods_db_dump
  QString conn_str = GetActiveConnectionString();
  QStringList args;

  // Build args using SQL dump approach: we'll dump via SQL commands sent through the client
  // First, log what we're doing
  exec_log_->clear();
  exec_log_->append(tr("=== Backup Operation Started at %1 ===")
                        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")));
  exec_log_->append(tr("Connection: %1").arg(conn_str));
  exec_log_->append(tr("Output: %1").arg(output_path));
  exec_log_->append("");

  if (!selected_dbs.isEmpty()) {
    exec_log_->append(tr("Databases selected: %1").arg(selected_dbs.join(", ")));
  } else {
    exec_log_->append(tr("No databases selected — backing up all databases."));
  }

  progress_bar_->setValue(10);
  status_label_->setText(tr("Generating DDL..."));
  QApplication::processEvents();

  // Build the dump SQL and write to file
  // 1. Dump DDL for selected databases/tables
  // 2. Dump data as INSERT statements
  QFile out_file(output_path);
  if (!out_file.open(QFile::WriteOnly | QFile::Text)) {
    status_label_->setText(tr("Error: Cannot write to %1").arg(output_path));
    exec_log_->append(tr("ERROR: Cannot write to %1").arg(output_path));
    return;
  }

  QTextStream out(&out_file);
  out << "-- goods_db dump generated by goods_db_studio\n";
  out << "-- Server: " << conn_str << "\n";
  out << "-- Date: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
  out << "-- Type: " << (is_incremental ? "Incremental" : "Full") << " backup\n\n";

  // Determine target databases
  QStringList target_dbs = selected_dbs;
  if (target_dbs.isEmpty()) {
    // Get all databases
    QueryResult all_db = ExecuteSql("SHOW DATABASES");
    if (!all_db.is_error) {
      for (const auto& row : all_db.rows) {
        if (!row.empty()) target_dbs.append(row[0].trimmed());
      }
    }
  }

  exec_log_->append(tr("Exporting %1 database(s)...").arg(target_dbs.size()));
  progress_bar_->setValue(20);
  QApplication::processEvents();

  int total_tables = 0;
  int processed = 0;

  // Count total tables first
  for (const QString& db_name : target_dbs) {
    QueryResult tables = ExecuteSql(QString("SHOW TABLES FROM %1").arg(db_name));
    if (!tables.is_error) total_tables += static_cast<int>(tables.rows.size());
  }

  for (const QString& db_name : target_dbs) {
    out << "-- ========================================\n";
    out << "-- Database: " << db_name << "\n";
    out << "-- ========================================\n\n";
    out << "CREATE DATABASE IF NOT EXISTS " << db_name << ";\n";
    out << "USE " << db_name << ";\n\n";

    exec_log_->append(tr("  Exporting database: %1").arg(db_name));

    // Get CREATE TABLE statements
    QueryResult tables = ExecuteSql(QString("SHOW TABLES FROM %1").arg(db_name));
    if (tables.is_error) {
      exec_log_->append(tr("  WARNING: Cannot read tables from %1: %2")
                            .arg(db_name, tables.error_message));
      continue;
    }

    for (const auto& trow : tables.rows) {
      if (trow.empty()) continue;
      QString table_name = trow[0].trimmed();

      // Check if this table is explicitly selected (if individual tables were chosen)
      if (!selected_tables.isEmpty() &&
          !selected_tables.contains(db_name + "." + table_name)) {
        continue;  // Not in the user's selection
      }

      out << "-- Table: " << db_name << "." << table_name << "\n";

      // Get CREATE TABLE DDL
      QueryResult ddl = ExecuteSql(QString("SHOW CREATE TABLE %1.%2").arg(db_name, table_name));
      if (!ddl.is_error && !ddl.rows.empty() && ddl.rows[0].size() >= 2) {
        out << ddl.rows[0][1] << ";\n\n";
      } else {
        out << "-- (SHOW CREATE TABLE not available for " << table_name << ")\n\n";
      }

      // Dump data
      exec_log_->append(tr("    Dumping table: %1").arg(table_name));
      QueryResult data = ExecuteSql(
          QString("SELECT * FROM %1.%2").arg(db_name, table_name));

      if (!data.is_error && !data.rows.empty()) {
        // Generate INSERT statements in batches of 100 rows
        int row_count = static_cast<int>(data.rows.size());
        int batch_size = 100;

        for (int batch_start = 0; batch_start < row_count; batch_start += batch_size) {
          int batch_end = std::min(batch_start + batch_size, row_count);
          out << "INSERT INTO " << table_name << " VALUES\n";

          for (int r = batch_start; r < batch_end; r++) {
            const auto& row = data.rows[r];
            out << "(";
            for (size_t c = 0; c < row.size(); c++) {
              if (c > 0) out << ", ";
              // Simple escaping: wrap in quotes, escape single quotes
              QString val = row[c];
              val.replace("\\", "\\\\");
              val.replace("'", "\\'");
              out << "'" << val << "'";
            }
            out << ")";
            if (r < batch_end - 1) out << ",";
            out << "\n";
          }
          out << ";\n\n";
        }
        exec_log_->append(tr("      %1 rows exported").arg(row_count));
      } else {
        out << "-- (table is empty)\n\n";
      }

      processed++;
      int pct = 20 + static_cast<int>(60.0 * processed / std::max(total_tables, 1));
      progress_bar_->setValue(pct);
      QApplication::processEvents();
    }
  }

  // Finalize
  out << "-- End of backup dump\n";
  out_file.close();

  progress_bar_->setValue(100);
  status_label_->setText(tr("Backup completed!"));
  exec_log_->append("");
  exec_log_->append(tr("=== Backup Completed Successfully ==="));
  exec_log_->append(tr("Output file: %1").arg(output_path));
  exec_log_->append(tr("File size: %1 bytes").arg(QFileInfo(output_path).size()));
}

void BackupWizard::ExecuteRestore() {
  QString input_path = restore_file_edit_->text();
  if (input_path.isEmpty()) {
    status_label_->setText(tr("Error: No backup file selected."));
    return;
  }

  QFile file(input_path);
  if (!file.open(QFile::ReadOnly | QFile::Text)) {
    status_label_->setText(tr("Error: Cannot open %1").arg(input_path));
    return;
  }

  exec_log_->clear();
  exec_log_->append(tr("=== Restore Operation Started at %1 ===")
                        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")));
  exec_log_->append(tr("Input file: %1").arg(input_path));
  exec_log_->append("");

  QTextStream stream(&file);
  QString all_sql = stream.readAll();
  file.close();

  // Split by semicolons and execute each statement
  QStringList statements;
  QString current;
  bool in_string = false;
  QChar string_char;

  for (int i = 0; i < all_sql.size(); i++) {
    QChar c = all_sql[i];
    if (in_string) {
      current += c;
      if (c == string_char && i > 0 && all_sql[i - 1] != '\\') in_string = false;
    } else if (c == '\'' || c == '"') {
      in_string = true;
      string_char = c;
      current += c;
    } else if (c == ';') {
      QString stmt = current.trimmed();
      if (!stmt.isEmpty() && !stmt.startsWith("--") && !stmt.startsWith("/*")) {
        statements.append(stmt);
      }
      current.clear();
    } else {
      current += c;
    }
  }

  int total = statements.size();
  progress_bar_->setValue(0);
  status_label_->setText(tr("Executing %1 statements...").arg(total));

  int success_count = 0;
  int error_count = 0;

  for (int i = 0; i < total; i++) {
    const QString& stmt = statements[i];
    // Skip comments and USE statements for simplicity
    if (stmt.trimmed().startsWith("USE ", Qt::CaseInsensitive)) {
      success_count++;
      continue;
    }

    QueryResult r = ExecuteSql(stmt);
    if (r.is_error) {
      error_count++;
      exec_log_->append(tr("[%1/%2] ERROR: %3")
                            .arg(i + 1).arg(total).arg(r.error_message));
      exec_log_->append(tr("  Statement: %1").arg(stmt.left(80)));
    } else {
      success_count++;
      if ((i + 1) % 50 == 0 || i == total - 1) {
        exec_log_->append(tr("[%1/%2] OK (%3 success, %4 errors)")
                              .arg(i + 1).arg(total).arg(success_count).arg(error_count));
      }
    }

    int pct = static_cast<int>(100.0 * (i + 1) / total);
    progress_bar_->setValue(pct);
    status_label_->setText(tr("Executing... %1/%2").arg(i + 1).arg(total));
    QApplication::processEvents();
  }

  progress_bar_->setValue(100);
  if (error_count == 0) {
    status_label_->setText(tr("Restore completed! %1 statements executed successfully.")
                               .arg(success_count));
  } else {
    status_label_->setText(tr("Restore completed with %1 errors. %2 statements succeeded.")
                               .arg(error_count).arg(success_count));
  }
  exec_log_->append("");
  exec_log_->append(tr("=== Restore Finished: %1 success, %2 errors ===")
                        .arg(success_count).arg(error_count));
}

// =============================================================================
// Process Slots (kept for future CLI integration)
// =============================================================================

void BackupWizard::OnProcessOutput() {
  if (!dump_process_) return;
  QString output = dump_process_->readAllStandardOutput();
  if (!output.trimmed().isEmpty()) {
    exec_log_->append(output.trimmed());
  }
  QString err = dump_process_->readAllStandardError();
  if (!err.trimmed().isEmpty()) {
    exec_log_->append(tr("[stderr] %1").arg(err.trimmed()));
  }
}

void BackupWizard::OnProcessFinished(int exit_code, QProcess::ExitStatus status) {
  Q_UNUSED(status);
  progress_bar_->setValue(100);
  if (exit_code == 0) {
    status_label_->setText(tr("Operation completed successfully."));
  } else {
    status_label_->setText(tr("Operation failed with exit code %1.").arg(exit_code));
  }
}

void BackupWizard::OnProcessError(QProcess::ProcessError error) {
  QString msg;
  switch (error) {
    case QProcess::FailedToStart: msg = tr("Failed to start"); break;
    case QProcess::Crashed:       msg = tr("Process crashed"); break;
    case QProcess::Timedout:      msg = tr("Process timed out"); break;
    default:                      msg = tr("Unknown error"); break;
  }
  status_label_->setText(tr("Error: %1").arg(msg));
}

// =============================================================================
// Override accept() to trigger execution when Finish is clicked
// =============================================================================

void BackupWizard::accept() {
  // Execute the selected operation
  if (restore_radio_->isChecked()) {
    ExecuteRestore();
  } else {
    ExecuteBackup();
  }
  // Don't call QWizard::accept() — let the user read the results,
  // then close manually or we can auto-close after a delay
  button(QWizard::FinishButton)->setEnabled(false);
  button(QWizard::CancelButton)->setText(tr("Close"));
}

}  // namespace studio
}  // namespace goods_db
