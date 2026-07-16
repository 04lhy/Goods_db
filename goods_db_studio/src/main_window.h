#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QSettings>
#include <QTabWidget>
#include <memory>

#include "ui/login_dialog.h"

namespace goods_db {
namespace studio {

// Forward declarations
class SqlEditorWidget;
class ResultTableView;
class ObjectTreeWidget;
class TableInfoPanel;
class ConnectionPool;
class QueryExecutor;
class QueryHistory;
class AdminPanel;
class BackupWizard;
class LogViewer;
class UserManager;
class WelcomeWidget;
class LoginDialog;
struct QueryResult;

// =============================================================================
// MainWindow — primary application window
//
// Layout:
//   ┌─────────────────────────────────────────────────────────┐
//   │ Menu Bar (File | Edit | Query | Tools | Settings | Help) │
//   │ Tool Bar (New Conn | Open | Execute | Stop | Cmt | Rbk) │
//   ├──────────┬──────────────────────┬────────────────────────┤
//   │ Object   │   SQL Editor         │   Table Info           │
//   │ Browser  │   (multi-tab)        │   Panel                │
//   │ (left)   │                      │   (right)              │
//   │          ├──────────────────────┤                        │
//   │          │   Result Browser     │                        │
//   │          │   (bottom)           │                        │
//   ├──────────┴──────────────────────┴────────────────────────┤
//   │ Status Bar (connection | db | txn)                        │
//   └─────────────────────────────────────────────────────────┘
// =============================================================================
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  /** Set up the initial connection from the login dialog. */
  void SetInitialConnection(const LoginDialog::LoginInfo& info);

 private slots:
  void OnNewConnection();
  void OnDisconnect();
  void OnExecuteQuery();
  void OnStopQuery();
  void OnCommit();
  void OnRollback();
  void OnOpenFile();
  void OnSaveFile();
  void OnToggleTheme();
  void OnAbout();
  void OnConnectionEstablished(const QString& name);
  void OnConnectionLost(const QString& name);
  void OnActiveConnectionChanged(const QString& name);
  void OnQueryComplete(const QueryResult& result);
  void OnExportResults();
  void OnTableDoubleClicked(const QString& db, const QString& table);

 private:
  void SetupMenuBar();
  void SetupToolBar();
  void SetupDockWidgets();
  void SetupStatusBar();
  void ApplyTheme(const QString& theme_path);
  void SaveSettings();
  void LoadSettings();
  void UpdateConnectionStatus();

  // Child widgets
  QTabWidget* central_tabs_ = nullptr;
  SqlEditorWidget* editor_widget_ = nullptr;
  ResultTableView* result_view_ = nullptr;
  ObjectTreeWidget* object_tree_ = nullptr;
  TableInfoPanel* table_info_panel_ = nullptr;
  ConnectionPool* connection_pool_ = nullptr;
  QueryExecutor* query_executor_ = nullptr;
  QueryHistory* query_history_ = nullptr;
  AdminPanel* admin_panel_ = nullptr;
  BackupWizard* backup_wizard_ = nullptr;
  LogViewer* log_viewer_ = nullptr;
  UserManager* user_manager_ = nullptr;
  WelcomeWidget* welcome_widget_ = nullptr;

  // Status bar labels
  QLabel* connection_status_ = nullptr;
  QLabel* db_label_ = nullptr;
  QLabel* txn_status_ = nullptr;

  // Actions that need enable/disable
  QAction* execute_action_ = nullptr;
  QAction* stop_action_ = nullptr;
  QAction* commit_action_ = nullptr;
  QAction* rollback_action_ = nullptr;
  QAction* disconnect_action_ = nullptr;

  bool dark_theme_ = true;
};

}  // namespace studio
}  // namespace goods_db
