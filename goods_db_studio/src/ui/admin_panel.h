#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QGridLayout>
#include <QLabel>
#include <QTableView>
#include <QStandardItemModel>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QTimer>
#include <QPlainTextEdit>

#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

class ConnectionPool;

// =============================================================================
// AdminPanel — server administration dashboard
//
// Features:
//   - Status dashboard: QPS / connections / buffer-pool hit-rate / dirty-pages / uptime
//   - Process list: QTableView with sortable columns
//   - Action buttons: Flush Hosts/Logs/Tables, Reload, Ping, Kill, Shutdown
//   - Auto-refresh via QTimer (configurable interval)
//
// Communication: sends SQL commands via ConnectionPool::GetActiveConnection()->Execute()
//   SHOW STATUS       → dashboard metrics
//   SHOW PROCESSLIST  → process list
//   FLUSH HOSTS/LOGS/TABLES → maintenance
//   KILL <id>         → terminate connection
//   SHUTDOWN          → graceful shutdown
// =============================================================================
class AdminPanel : public QWidget {
  Q_OBJECT

 public:
  explicit AdminPanel(ConnectionPool* pool, QWidget* parent = nullptr);

 public slots:
  void Refresh();

 private slots:
  void OnFlushHosts();
  void OnFlushLogs();
  void OnFlushTables();
  void OnReload();
  void OnPing();
  void OnShutdown();
  void OnKillProcess();
  void OnAutoRefreshToggled(bool enabled);
  void OnRefreshIntervalChanged(int seconds);

 private:
  void SetupUi();
  void SetupDashboard(QGridLayout* layout, int& row);
  void SetupProcessList(QGridLayout* layout, int& row);
  void SetupActionButtons(QGridLayout* layout, int& row);

  // Execute SQL and handle errors
  QueryResult ExecuteSql(const QString& sql);
  void LogMessage(const QString& msg, bool is_error = false);

  ConnectionPool* pool_;
  QTimer* refresh_timer_;

  // ---- Dashboard labels ----
  QLabel* qps_value_;
  QLabel* connections_value_;
  QLabel* hit_rate_value_;
  QLabel* dirty_pages_value_;
  QLabel* uptime_value_;

  // ---- Process list ----
  QTableView* process_view_;
  QStandardItemModel* process_model_;

  // ---- Action buttons ----
  QPushButton* flush_hosts_btn_;
  QPushButton* flush_logs_btn_;
  QPushButton* flush_tables_btn_;
  QPushButton* reload_btn_;
  QPushButton* ping_btn_;
  QPushButton* kill_btn_;
  QPushButton* shutdown_btn_;

  // ---- Refresh controls ----
  QCheckBox* auto_refresh_cb_;
  QSpinBox* refresh_interval_spin_;
  QPushButton* manual_refresh_btn_;

  // ---- Event log ----
  QPlainTextEdit* event_log_;
};

}  // namespace studio
}  // namespace goods_db
