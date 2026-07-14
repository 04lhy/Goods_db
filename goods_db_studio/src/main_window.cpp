#include "main_window.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include "connection/connection_dialog.h"
#include "connection/connection_pool.h"
#include "network/goods_db_client.h"
#include "object_tree/object_tree_widget.h"
#include "object_tree/table_info_panel.h"
#include "result_view/export_dialog.h"
#include "result_view/result_table_view.h"
#include "sql_editor/query_executor.h"
#include "sql_editor/query_history.h"
#include "sql_editor/sql_editor_widget.h"

namespace goods_db {
namespace studio {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("goods_db_studio");
  resize(1280, 800);
  setMinimumSize(800, 600);

  // Create core objects first
  connection_pool_ = new ConnectionPool(this);
  query_history_ = new QueryHistory(this);
  query_executor_ = new QueryExecutor(connection_pool_, this);

  // Setup UI (creates all widgets and actions)
  SetupMenuBar();
  SetupToolBar();
  SetupDockWidgets();
  SetupStatusBar();

  // Wire signals (all widgets exist now)
  connect(connection_pool_, &ConnectionPool::ConnectionEstablished,
          this, &MainWindow::OnConnectionEstablished);
  connect(connection_pool_, &ConnectionPool::ConnectionLost,
          this, &MainWindow::OnConnectionLost);
  connect(connection_pool_, &ConnectionPool::ActiveConnectionChanged,
          this, &MainWindow::OnActiveConnectionChanged);
  connect(query_executor_, &QueryExecutor::ExecutionComplete,
          this, &MainWindow::OnQueryComplete);
  connect(object_tree_, &ObjectTreeWidget::TableDoubleClicked,
          this, &MainWindow::OnTableDoubleClicked);

  // Load saved settings last (after all widgets are created)
  LoadSettings();

  UpdateConnectionStatus();
}

MainWindow::~MainWindow() {
  SaveSettings();
}

// ---- Menu Bar ---------------------------------------------------------------

void MainWindow::SetupMenuBar() {
  // File menu
  QMenu* file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->addAction(tr("&New Connection..."), this,
                       &MainWindow::OnNewConnection, QKeySequence("Ctrl+N"));
  file_menu->addAction(tr("&Open SQL File..."), this,
                       &MainWindow::OnOpenFile, QKeySequence("Ctrl+O"));
  file_menu->addAction(tr("&Save SQL File..."), this,
                       &MainWindow::OnSaveFile, QKeySequence("Ctrl+S"));
  file_menu->addSeparator();
  disconnect_action_ = file_menu->addAction(tr("&Disconnect"), this,
                                            &MainWindow::OnDisconnect);
  disconnect_action_->setEnabled(false);
  file_menu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence("Ctrl+Q"));

  // Edit menu (simplified — actions connect via MainWindow slots)
  QMenu* edit_menu = menuBar()->addMenu(tr("&Edit"));
  QAction* undo_action = edit_menu->addAction(tr("&Undo"));
  undo_action->setShortcut(QKeySequence("Ctrl+Z"));
  connect(undo_action, &QAction::triggered, this, [this]() {
    if (auto* ed = editor_widget_->GetCurrentEditor()) ed->undo();
  });
  QAction* redo_action = edit_menu->addAction(tr("&Redo"));
  redo_action->setShortcut(QKeySequence("Ctrl+Y"));
  connect(redo_action, &QAction::triggered, this, [this]() {
    if (auto* ed = editor_widget_->GetCurrentEditor()) ed->redo();
  });
  edit_menu->addSeparator();
  QAction* cut_action = edit_menu->addAction(tr("Cu&t"));
  cut_action->setShortcut(QKeySequence("Ctrl+X"));
  connect(cut_action, &QAction::triggered, this, [this]() {
    if (auto* ed = editor_widget_->GetCurrentEditor()) ed->cut();
  });
  QAction* copy_action = edit_menu->addAction(tr("&Copy"));
  copy_action->setShortcut(QKeySequence("Ctrl+C"));
  connect(copy_action, &QAction::triggered, this, [this]() {
    if (auto* ed = editor_widget_->GetCurrentEditor()) ed->copy();
  });
  QAction* paste_action = edit_menu->addAction(tr("&Paste"));
  paste_action->setShortcut(QKeySequence("Ctrl+V"));
  connect(paste_action, &QAction::triggered, this, [this]() {
    if (auto* ed = editor_widget_->GetCurrentEditor()) ed->paste();
  });

  // Query menu
  QMenu* query_menu = menuBar()->addMenu(tr("&Query"));
  execute_action_ = query_menu->addAction(tr("&Execute All"), this,
                                          &MainWindow::OnExecuteQuery,
                                          QKeySequence("F5"));
  stop_action_ = query_menu->addAction(tr("&Stop"), this,
                                       &MainWindow::OnStopQuery,
                                       QKeySequence("Esc"));
  stop_action_->setEnabled(false);
  query_menu->addSeparator();
  commit_action_ = query_menu->addAction(tr("&Commit"), this,
                                         &MainWindow::OnCommit,
                                         QKeySequence("Ctrl+Shift+C"));
  commit_action_->setEnabled(false);
  rollback_action_ = query_menu->addAction(tr("&Rollback"), this,
                                           &MainWindow::OnRollback,
                                           QKeySequence("Ctrl+Shift+R"));
  rollback_action_->setEnabled(false);

  // Tools menu
  QMenu* tools_menu = menuBar()->addMenu(tr("&Tools"));
  tools_menu->addAction(tr("&Export Results..."), this,
                        &MainWindow::OnExportResults);

  // Settings menu
  QMenu* settings_menu = menuBar()->addMenu(tr("&Settings"));
  settings_menu->addAction(tr("Toggle &Theme"), this,
                           &MainWindow::OnToggleTheme, QKeySequence("Ctrl+T"));

  // Help menu
  QMenu* help_menu = menuBar()->addMenu(tr("&Help"));
  help_menu->addAction(tr("&About"), this, &MainWindow::OnAbout);
}

// ---- Tool Bar ---------------------------------------------------------------

void MainWindow::SetupToolBar() {
  QToolBar* toolbar = addToolBar(tr("Main"));
  toolbar->setObjectName("main_toolbar");
  toolbar->setMovable(false);

  QAction* a;
  a = toolbar->addAction(tr("New Connection"));
  connect(a, &QAction::triggered, this, &MainWindow::OnNewConnection);
  a = toolbar->addAction(tr("Open File"));
  connect(a, &QAction::triggered, this, &MainWindow::OnOpenFile);
  toolbar->addSeparator();
  a = toolbar->addAction(tr("Execute (F5)"));
  connect(a, &QAction::triggered, this, &MainWindow::OnExecuteQuery);
  a = toolbar->addAction(tr("Stop"));
  connect(a, &QAction::triggered, this, &MainWindow::OnStopQuery);
  toolbar->addSeparator();
  a = toolbar->addAction(tr("Commit"));
  connect(a, &QAction::triggered, this, &MainWindow::OnCommit);
  a = toolbar->addAction(tr("Rollback"));
  connect(a, &QAction::triggered, this, &MainWindow::OnRollback);
}

// ---- Dock Widgets -----------------------------------------------------------

void MainWindow::SetupDockWidgets() {
  // Central: SQL Editor
  editor_widget_ = new SqlEditorWidget(this);
  setCentralWidget(editor_widget_);

  // Left dock: Object Browser
  QDockWidget* left_dock = new QDockWidget(tr("Object Browser"), this);
  left_dock->setObjectName("object_browser_dock");
  object_tree_ = new ObjectTreeWidget(left_dock);
  left_dock->setWidget(object_tree_);
  addDockWidget(Qt::LeftDockWidgetArea, left_dock);

  // Right dock: Table Info Panel
  QDockWidget* right_dock = new QDockWidget(tr("Table Info"), this);
  right_dock->setObjectName("table_info_dock");
  table_info_panel_ = new TableInfoPanel(right_dock);
  right_dock->setWidget(table_info_panel_);
  addDockWidget(Qt::RightDockWidgetArea, right_dock);

  // Bottom dock: Result Browser
  QDockWidget* bottom_dock = new QDockWidget(tr("Results"), this);
  bottom_dock->setObjectName("result_view_dock");
  result_view_ = new ResultTableView(bottom_dock);
  bottom_dock->setWidget(result_view_);
  addDockWidget(Qt::BottomDockWidgetArea, bottom_dock);

  // Wire object tree selection → table info panel
  connect(object_tree_, &ObjectTreeWidget::TableSelected,
          this, [this](const QString& db, const QString& table) {
    table_info_panel_->ShowTableInfo(db, table, {});
  });
}

// ---- Status Bar -------------------------------------------------------------

void MainWindow::SetupStatusBar() {
  connection_status_ = new QLabel(tr("Disconnected"));
  connection_status_->setStyleSheet(
      "color: #e74c3c; font-weight: bold; padding: 0 8px;");
  statusBar()->addPermanentWidget(connection_status_);

  db_label_ = new QLabel("");
  db_label_->setStyleSheet("padding: 0 8px;");
  statusBar()->addPermanentWidget(db_label_);

  txn_status_ = new QLabel(tr("Auto-commit ON"));
  txn_status_->setStyleSheet("padding: 0 8px; color: #27ae60;");
  statusBar()->addPermanentWidget(txn_status_);

  statusBar()->showMessage(tr("Ready"), 3000);
}

// ---- Theme ------------------------------------------------------------------

void MainWindow::ApplyTheme(const QString& theme_path) {
  QFile themeFile(theme_path);
  if (themeFile.open(QFile::ReadOnly | QFile::Text)) {
    qApp->setStyleSheet(themeFile.readAll());
    themeFile.close();
  }
}

// ---- Settings ---------------------------------------------------------------

void MainWindow::SaveSettings() {
  QSettings settings("goods_db", "goods_db_studio");
  settings.setValue("window/geometry", saveGeometry());
  settings.setValue("window/state", saveState());
  settings.setValue("theme/dark", dark_theme_);
  connection_pool_->SaveToSettings();
}

void MainWindow::LoadSettings() {
  QSettings settings("goods_db", "goods_db_studio");
  // Only restore geometry if there's saved data
  if (settings.contains("window/geometry")) {
    restoreGeometry(settings.value("window/geometry").toByteArray());
  }
  dark_theme_ = settings.value("theme/dark", true).toBool();
}

void MainWindow::UpdateConnectionStatus() {
  auto* conn = connection_pool_->GetActiveConnection();
  if (conn && conn->IsConnected()) {
    connection_status_->setText(
        tr("Connected: %1:%2").arg(conn->GetHost()).arg(conn->GetPort()));
    connection_status_->setStyleSheet(
        "color: #27ae60; font-weight: bold; padding: 0 8px;");
    if (disconnect_action_) disconnect_action_->setEnabled(true);
    if (execute_action_) execute_action_->setEnabled(true);
  } else {
    connection_status_->setText(tr("Disconnected"));
    connection_status_->setStyleSheet(
        "color: #e74c3c; font-weight: bold; padding: 0 8px;");
    db_label_->setText("");
    if (disconnect_action_) disconnect_action_->setEnabled(false);
    if (execute_action_) execute_action_->setEnabled(false);
  }
}

// ---- Slots ------------------------------------------------------------------

void MainWindow::OnNewConnection() {
  ConnectionDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted) {
    auto info = dialog.GetConnectionInfo();
    connection_pool_->AddConnection(info.name, info.host, info.port,
                                    info.user, info.password);
    object_tree_->SetConnection(info.name);
    UpdateConnectionStatus();
  }
}

void MainWindow::OnDisconnect() {
  auto* conn = connection_pool_->GetActiveConnection();
  if (conn) {
    conn->Disconnect();
  }
  UpdateConnectionStatus();
}

void MainWindow::OnExecuteQuery() {
  QString sql = editor_widget_->GetSelectedOrAllSql();
  if (sql.trimmed().isEmpty()) {
    statusBar()->showMessage(tr("No SQL to execute"), 3000);
    return;
  }
  statusBar()->showMessage(tr("Executing query..."));
  query_executor_->Execute(sql);
}

void MainWindow::OnStopQuery() {
  query_executor_->Stop();
  statusBar()->showMessage(tr("Query stopped"), 3000);
}

void MainWindow::OnCommit() {
  query_executor_->Execute("COMMIT");
}

void MainWindow::OnRollback() {
  query_executor_->Execute("ROLLBACK");
}

void MainWindow::OnOpenFile() {
  QString file_path = QFileDialog::getOpenFileName(
      this, tr("Open SQL File"), QString(),
      tr("SQL Files (*.sql);;All Files (*)"));
  if (!file_path.isEmpty()) {
    QFile file(file_path);
    if (file.open(QFile::ReadOnly | QFile::Text)) {
      editor_widget_->SetSql(file.readAll());
      file.close();
      statusBar()->showMessage(tr("Loaded: %1").arg(file_path), 3000);
    }
  }
}

void MainWindow::OnSaveFile() {
  QString file_path = QFileDialog::getSaveFileName(
      this, tr("Save SQL File"), QString(),
      tr("SQL Files (*.sql);;All Files (*)"));
  if (!file_path.isEmpty()) {
    QFile file(file_path);
    if (file.open(QFile::WriteOnly | QFile::Text)) {
      file.write(editor_widget_->GetCurrentSql().toUtf8());
      file.close();
      statusBar()->showMessage(tr("Saved: %1").arg(file_path), 3000);
    }
  }
}

void MainWindow::OnToggleTheme() {
  dark_theme_ = !dark_theme_;
  ApplyTheme(dark_theme_ ? ":/themes/dark.qss" : ":/themes/light.qss");
  statusBar()->showMessage(
      tr("Theme: %1").arg(dark_theme_ ? "Dark" : "Light"), 3000);
}

void MainWindow::OnAbout() {
  QMessageBox::about(this, tr("About goods_db_studio"),
                     tr("goods_db_studio v0.1.0\n\n"
                        "Desktop client for goods_db database system.\n\n"
                        "Database System Design Course Project\n"
                        "Hunan University"));
}

void MainWindow::OnConnectionEstablished(const QString& name) {
  statusBar()->showMessage(tr("Connected: %1").arg(name), 3000);
  UpdateConnectionStatus();
}

void MainWindow::OnConnectionLost(const QString& name) {
  statusBar()->showMessage(tr("Disconnected: %1").arg(name), 3000);
  UpdateConnectionStatus();
}

void MainWindow::OnActiveConnectionChanged(const QString& name) {
  UpdateConnectionStatus();
  if (object_tree_) object_tree_->SetConnection(name);
}

void MainWindow::OnQueryComplete(const QueryResult& result) {
  if (result.is_error) {
    statusBar()->showMessage(
        tr("Error: %1").arg(result.error_message), 5000);
    result_view_->Clear();
  } else {
    result_view_->SetResult(result);
    QString msg;
    if (!result.columns.empty()) {
      msg = tr("Query OK, %1 rows returned (%2 ms)")
                .arg(result.rows.size())
                .arg(result.exec_time_ms);
    } else {
      msg = tr("Query OK, %1 rows affected (%2 ms)")
                .arg(result.affected_rows)
                .arg(result.exec_time_ms);
    }
    statusBar()->showMessage(msg, 5000);

    // Save to history
    if (query_history_) {
      auto* conn = connection_pool_->GetActiveConnection();
      query_history_->AddEntry(
          editor_widget_->GetCurrentSql(),
          conn ? conn->GetHost() : "",
          result.exec_time_ms, true);
    }
  }
}

void MainWindow::OnExportResults() {
  ExportDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted) {
    QString path = dialog.GetFilePath();
    if (path.isEmpty()) return;
    auto format = dialog.GetFormat();

    switch (format) {
      case ExportDialog::CSV:
        result_view_->ExportToCsv(path);
        break;
      case ExportDialog::JSON:
        result_view_->ExportToJson(path);
        break;
      case ExportDialog::SQL_INSERT:
        result_view_->ExportToSqlInsert(dialog.GetTableName(), path);
        break;
    }
    statusBar()->showMessage(tr("Exported to: %1").arg(path), 3000);
  }
}

void MainWindow::OnTableDoubleClicked(const QString& db, const QString& table) {
  QString sql = QString("SELECT * FROM %1.%2 LIMIT 100;").arg(db).arg(table);
  editor_widget_->SetSql(sql);
  OnExecuteQuery();
}

}  // namespace studio
}  // namespace goods_db
