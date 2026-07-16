#include "ui/admin_panel.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QDateTime>
#include <QMap>

#include "connection/connection_pool.h"
#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

// =============================================================================
// Helpers
// =============================================================================

namespace {

QString FormatUptime(int64_t seconds) {
  int64_t d = seconds / 86400;
  int64_t h = (seconds % 86400) / 3600;
  int64_t m = (seconds % 3600) / 60;
  int64_t s = seconds % 60;
  if (d > 0) {
    return QString("%1d %2:%3:%4")
        .arg(d)
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
  }
  return QString("%1:%2:%3")
      .arg(h, 2, 10, QChar('0'))
      .arg(m, 2, 10, QChar('0'))
      .arg(s, 2, 10, QChar('0'));
}

// Parse SHOW STATUS result: columns => [Variable_name, Value]
// Build a map from variable name → value string.
QMap<QString, QString> ParseStatusResult(const QueryResult& result) {
  QMap<QString, QString> map;
  if (result.columns.size() < 2) return map;

  // Find column indices (case-insensitive)
  int name_col = -1, val_col = -1;
  for (size_t i = 0; i < result.columns.size(); i++) {
    QString col = result.columns[i].name.toLower();
    if (col == "variable_name" || col == "name") name_col = static_cast<int>(i);
    if (col == "value" || col == "val") val_col = static_cast<int>(i);
  }
  if (name_col < 0 || val_col < 0) return map;

  for (const auto& row : result.rows) {
    if (static_cast<int>(row.size()) > std::max(name_col, val_col)) {
      map[row[name_col].toLower()] = row[val_col];
    }
  }
  return map;
}

}  // anonymous namespace

// =============================================================================
// AdminPanel
// =============================================================================

AdminPanel::AdminPanel(ConnectionPool* pool, QWidget* parent)
    : QWidget(parent), pool_(pool) {
  // Auto-refresh timer (must be created before SetupUi which calls start())
  refresh_timer_ = new QTimer(this);
  connect(refresh_timer_, &QTimer::timeout, this, &AdminPanel::Refresh);

  SetupUi();
}

void AdminPanel::SetupUi() {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(8, 8, 8, 8);
  main_layout->setSpacing(8);

  // ---- Top: Dashboard + Process List (splitter) ----
  auto* splitter = new QSplitter(Qt::Vertical, this);

  // --- Dashboard ---
  auto* dash_group = new QGroupBox(tr("Status Dashboard"), this);
  auto* dash_layout = new QGridLayout(dash_group);
  dash_layout->setSpacing(12);
  int row = 0;
  SetupDashboard(dash_layout, row);
  splitter->addWidget(dash_group);

  // --- Process List ---
  auto* proc_group = new QGroupBox(tr("Process List"), this);
  auto* proc_layout = new QGridLayout(proc_group);
  proc_layout->setSpacing(6);
  row = 0;
  SetupProcessList(proc_layout, row);
  splitter->addWidget(proc_group);

  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 3);
  main_layout->addWidget(splitter, 1);

  // ---- Middle: Action Buttons ----
  auto* action_group = new QGroupBox(tr("Server Actions"), this);
  auto* action_layout = new QGridLayout(action_group);
  action_layout->setSpacing(8);
  row = 0;
  SetupActionButtons(action_layout, row);
  main_layout->addWidget(action_group, 0);

  // ---- Bottom: Event Log ----
  auto* log_group = new QGroupBox(tr("Event Log"), this);
  auto* log_layout = new QVBoxLayout(log_group);
  event_log_ = new QPlainTextEdit(this);
  event_log_->setReadOnly(true);
  event_log_->setMaximumBlockCount(500);
  event_log_->setFont(QFont("Consolas, Menlo, monospace", 10));
  event_log_->setPlaceholderText(tr("Server events and action results will appear here..."));
  log_layout->addWidget(event_log_);
  main_layout->addWidget(log_group, 0);

  // Set reasonable default sizes for the splitter
  splitter->setSizes({200, 500});
}

void AdminPanel::SetupDashboard(QGridLayout* layout, int& row) {
  // Header row with metric labels
  auto makeLabel = [this](const QString& text, bool bold = false) {
    auto* label = new QLabel(text, this);
    label->setAlignment(Qt::AlignCenter);
    if (bold) {
      QFont f = label->font();
      f.setBold(true);
      label->setFont(f);
    }
    return label;
  };

  layout->addWidget(makeLabel(tr("QPS"), true), row, 0);
  layout->addWidget(makeLabel(tr("Connections"), true), row, 1);
  layout->addWidget(makeLabel(tr("Buffer Pool Hit Rate"), true), row, 2);
  layout->addWidget(makeLabel(tr("Dirty Pages"), true), row, 3);
  layout->addWidget(makeLabel(tr("Uptime"), true), row, 4);
  row++;

  // Value row
  auto makeValueLabel = [this]() {
    auto* label = new QLabel(tr("--"), this);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("font-size: 16px; font-weight: bold; padding: 8px;");
    return label;
  };

  qps_value_ = makeValueLabel();
  connections_value_ = makeValueLabel();
  hit_rate_value_ = makeValueLabel();
  dirty_pages_value_ = makeValueLabel();
  uptime_value_ = makeValueLabel();

  layout->addWidget(qps_value_, row, 0);
  layout->addWidget(connections_value_, row, 1);
  layout->addWidget(hit_rate_value_, row, 2);
  layout->addWidget(dirty_pages_value_, row, 3);
  layout->addWidget(uptime_value_, row, 4);
  row++;
}

void AdminPanel::SetupProcessList(QGridLayout* layout, int& row) {
  // Table view
  process_model_ = new QStandardItemModel(0, 8, this);
  process_model_->setHorizontalHeaderLabels({
      tr("ID"), tr("User"), tr("Host"), tr("Database"),
      tr("Command"), tr("Time"), tr("State"), tr("Info")
  });

  process_view_ = new QTableView(this);
  process_view_->setModel(process_model_);
  process_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  process_view_->setSelectionMode(QAbstractItemView::SingleSelection);
  process_view_->setAlternatingRowColors(true);
  process_view_->setSortingEnabled(true);
  process_view_->verticalHeader()->setVisible(false);
  process_view_->horizontalHeader()->setStretchLastSection(true);
  process_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);

  // Column widths
  process_view_->setColumnWidth(0, 60);   // ID
  process_view_->setColumnWidth(1, 80);   // User
  process_view_->setColumnWidth(2, 120);  // Host
  process_view_->setColumnWidth(3, 80);   // Database
  process_view_->setColumnWidth(4, 70);   // Command
  process_view_->setColumnWidth(5, 60);   // Time
  process_view_->setColumnWidth(6, 80);   // State

  layout->addWidget(process_view_, row, 0);
  row++;

  // Kill button under the process list
  auto* kill_layout = new QHBoxLayout();
  kill_btn_ = new QPushButton(tr("Kill Selected Connection"), this);
  kill_btn_->setIcon(QIcon(":/icons/kill_connection.svg"));
  kill_btn_->setToolTip(tr("Terminate the selected connection"));
  kill_btn_->setStyleSheet("QPushButton { color: #e74c3c; font-weight: bold; }");
  kill_btn_->setEnabled(false);
  connect(kill_btn_, &QPushButton::clicked, this, &AdminPanel::OnKillProcess);
  kill_layout->addWidget(kill_btn_);
  kill_layout->addStretch();

  layout->addLayout(kill_layout, row, 0);
  row++;

  // Enable kill button when a row is selected
  connect(process_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, [this](const QItemSelection&, const QItemSelection&) {
    kill_btn_->setEnabled(process_view_->selectionModel()->hasSelection());
  });
}

void AdminPanel::SetupActionButtons(QGridLayout* layout, int& row) {
  auto makeBtn = [this](const QString& text, const QString& tooltip,
                        const QString& style = QString()) {
    auto* btn = new QPushButton(text, this);
    btn->setToolTip(tooltip);
    if (!style.isEmpty()) btn->setStyleSheet(style);
    btn->setMinimumHeight(32);
    return btn;
  };

  // Row 0: Flush operations
  flush_hosts_btn_ = makeBtn(tr("Flush Hosts"), tr("Flush host cache"));
  flush_hosts_btn_->setIcon(QIcon(":/icons/flush.svg"));
  connect(flush_hosts_btn_, &QPushButton::clicked, this, &AdminPanel::OnFlushHosts);
  layout->addWidget(flush_hosts_btn_, row, 0);

  flush_logs_btn_ = makeBtn(tr("Flush Logs"), tr("Flush all log buffers to disk"));
  flush_logs_btn_->setIcon(QIcon(":/icons/flush.svg"));
  connect(flush_logs_btn_, &QPushButton::clicked, this, &AdminPanel::OnFlushLogs);
  layout->addWidget(flush_logs_btn_, row, 1);

  flush_tables_btn_ = makeBtn(tr("Flush Tables"), tr("Close and reopen all table files"));
  flush_tables_btn_->setIcon(QIcon(":/icons/flush.svg"));
  connect(flush_tables_btn_, &QPushButton::clicked, this, &AdminPanel::OnFlushTables);
  layout->addWidget(flush_tables_btn_, row, 2);
  row++;

  // Row 1: Reload / Ping / Shutdown
  reload_btn_ = makeBtn(tr("Reload"), tr("Reload grant tables and configuration"));
  reload_btn_->setIcon(QIcon(":/icons/reload.svg"));
  connect(reload_btn_, &QPushButton::clicked, this, &AdminPanel::OnReload);
  layout->addWidget(reload_btn_, row, 0);

  ping_btn_ = makeBtn(tr("Ping"), tr("Check if the server is alive"));
  ping_btn_->setIcon(QIcon(":/icons/ping.svg"));
  connect(ping_btn_, &QPushButton::clicked, this, &AdminPanel::OnPing);
  layout->addWidget(ping_btn_, row, 1);

  shutdown_btn_ = makeBtn(tr("Shutdown"),
                          tr("Gracefully shutdown the server"),
                          "QPushButton { color: #e74c3c; font-weight: bold; }");
  shutdown_btn_->setIcon(QIcon(":/icons/shutdown.svg"));
  connect(shutdown_btn_, &QPushButton::clicked, this, &AdminPanel::OnShutdown);
  layout->addWidget(shutdown_btn_, row, 2);
  row++;

  // Row 2: Auto-refresh controls + manual refresh
  auto* refresh_layout = new QHBoxLayout();
  auto_refresh_cb_ = new QCheckBox(tr("Auto-refresh"), this);
  auto_refresh_cb_->setChecked(true);
  connect(auto_refresh_cb_, &QCheckBox::toggled,
          this, &AdminPanel::OnAutoRefreshToggled);

  refresh_interval_spin_ = new QSpinBox(this);
  refresh_interval_spin_->setRange(1, 300);
  refresh_interval_spin_->setValue(5);
  refresh_interval_spin_->setSuffix(tr(" s"));
  refresh_interval_spin_->setToolTip(tr("Refresh interval in seconds"));
  connect(refresh_interval_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, &AdminPanel::OnRefreshIntervalChanged);

  manual_refresh_btn_ = new QPushButton(tr("Refresh Now"), this);
  manual_refresh_btn_->setIcon(QIcon(":/icons/refresh.svg"));
  manual_refresh_btn_->setToolTip(tr("Manually refresh status and process list"));
  connect(manual_refresh_btn_, &QPushButton::clicked, this, &AdminPanel::Refresh);

  refresh_layout->addWidget(auto_refresh_cb_);
  refresh_layout->addWidget(refresh_interval_spin_);
  refresh_layout->addStretch();
  refresh_layout->addWidget(manual_refresh_btn_);

  layout->addLayout(refresh_layout, row, 0, 1, 3);
  row++;

  // Start auto-refresh
  refresh_timer_->start(refresh_interval_spin_->value() * 1000);
}

// =============================================================================
// SQL Execution
// =============================================================================

QueryResult AdminPanel::ExecuteSql(const QString& sql) {
  auto* conn = pool_->GetActiveConnection();
  if (!conn || !conn->IsConnected()) {
    QueryResult err;
    err.is_error = true;
    err.error_message = tr("No active connection");
    return err;
  }
  return conn->Execute(sql);
}

void AdminPanel::LogMessage(const QString& msg, bool is_error) {
  QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
  QString prefix = is_error ? "[ERROR]" : "[INFO]";
  QString line = QString("%1 %2 %3").arg(timestamp, prefix, msg);
  event_log_->appendPlainText(line);

  // Auto-scroll to bottom
  QTextCursor cursor = event_log_->textCursor();
  cursor.movePosition(QTextCursor::End);
  event_log_->setTextCursor(cursor);
}

// =============================================================================
// Public Slots
// =============================================================================

void AdminPanel::Refresh() {
  auto* conn = pool_->GetActiveConnection();
  if (!conn || !conn->IsConnected()) {
    qps_value_->setText(tr("--"));
    connections_value_->setText(tr("--"));
    hit_rate_value_->setText(tr("--"));
    dirty_pages_value_->setText(tr("--"));
    uptime_value_->setText(tr("--"));
    process_model_->removeRows(0, process_model_->rowCount());
    return;
  }

  // ---- SHOW STATUS ----
  QueryResult status_result = conn->Execute("SHOW STATUS");
  if (status_result.is_error) {
    LogMessage(tr("SHOW STATUS failed: %1").arg(status_result.error_message), true);
  } else {
    auto map = ParseStatusResult(status_result);

    // QPS — try several possible variable names
    auto getVal = [&map](const QString& key, const QString& def = "--") {
      QString lower = key.toLower();
      if (map.contains(lower)) return map[lower];
      // Also try with underscores replaced
      QString alt = lower;
      alt.replace('_', ' ');
      // Sometimes variable names use spaces in the result
      return map.value(lower, def);
    };

    // Parse numeric values
    bool ok = false;
    double qps = getVal("queries", "0").toDouble(&ok);
    if (!ok) qps = getVal("questions", "0").toDouble();

    int64_t uptime_sec = getVal("uptime", "0").toLongLong();

    // If we have uptime, compute QPS = queries / uptime
    if (uptime_sec > 0 && qps > 0) {
      qps_value_->setText(QString::number(qps / uptime_sec, 'f', 1));
    } else if (qps > 0) {
      qps_value_->setText(QString::number(qps, 'f', 0));
    } else {
      qps_value_->setText(getVal("queries_per_second", getVal("qps", "0.0")));
    }

    // Connections
    QString threads_connected = getVal("threads_connected", "0");
    QString max_connections = getVal("max_connections",
                                     getVal("max_used_connections", "0"));
    connections_value_->setText(
        QString("%1/%2").arg(threads_connected, max_connections));

    // Buffer pool hit rate
    QString hit_rate = getVal("buffer_pool_hit_rate",
                              getVal("innodb_buffer_pool_reads", "0"));
    // Try to format as percentage
    double hr = hit_rate.toDouble(&ok);
    if (ok && hr >= 0 && hr <= 1) {
      hit_rate_value_->setText(QString::number(hr * 100, 'f', 1) + "%");
    } else if (ok) {
      hit_rate_value_->setText(QString::number(hr, 'f', 1) + "%");
    } else {
      // Try computing from reads vs read_requests
      double reads = getVal("buffer_pool_reads", "0").toDouble();
      double requests = getVal("buffer_pool_read_requests", "1").toDouble();
      if (requests > 0) {
        double rate = (1.0 - reads / requests) * 100;
        hit_rate_value_->setText(QString::number(rate, 'f', 1) + "%");
      } else {
        hit_rate_value_->setText(hit_rate);
      }
    }

    // Dirty pages
    dirty_pages_value_->setText(
        getVal("buffer_pool_dirty_pages",
               getVal("innodb_buffer_pool_pages_dirty", "0")));

    // Uptime
    uptime_value_->setText(FormatUptime(uptime_sec));

    LogMessage(tr("Dashboard refreshed"));
  }

  // ---- SHOW PROCESSLIST ----
  QueryResult proc_result = conn->Execute("SHOW PROCESSLIST");
  if (proc_result.is_error) {
    LogMessage(tr("SHOW PROCESSLIST failed: %1").arg(proc_result.error_message), true);
    return;
  }

  // Map column names to indices
  int id_idx = -1, user_idx = -1, host_idx = -1, db_idx = -1;
  int cmd_idx = -1, time_idx = -1, state_idx = -1, info_idx = -1;
  for (size_t i = 0; i < proc_result.columns.size(); i++) {
    QString c = proc_result.columns[i].name.toLower();
    if (c == "id")                   id_idx = static_cast<int>(i);
    else if (c == "user")            user_idx = static_cast<int>(i);
    else if (c == "host")            host_idx = static_cast<int>(i);
    else if (c == "db" || c == "database") db_idx = static_cast<int>(i);
    else if (c == "command")         cmd_idx = static_cast<int>(i);
    else if (c == "time")            time_idx = static_cast<int>(i);
    else if (c == "state")           state_idx = static_cast<int>(i);
    else if (c == "info" || c == "query") info_idx = static_cast<int>(i);
  }

  // If no columns found, show as raw text
  if (id_idx < 0 && proc_result.columns.size() > 0) {
    // Fallback: use positional columns
    for (size_t i = 0; i < proc_result.columns.size() && i < 8; i++) {
      switch (i) {
        case 0: id_idx = 0; break;
        case 1: user_idx = 1; break;
        case 2: host_idx = 2; break;
        case 3: db_idx = 3; break;
        case 4: cmd_idx = 4; break;
        case 5: time_idx = 5; break;
        case 6: state_idx = 6; break;
        case 7: info_idx = 7; break;
      }
    }
  }

  process_model_->removeRows(0, process_model_->rowCount());
  for (const auto& row : proc_result.rows) {
    QList<QStandardItem*> items;
    auto cell = [](const QString& s) {
      auto* item = new QStandardItem(s);
      item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      return item;
    };

    auto safeGet = [&row](int idx) -> QString {
      return (idx >= 0 && idx < static_cast<int>(row.size())) ? row[idx] : QString();
    };

    items.append(cell(safeGet(id_idx)));
    items.append(cell(safeGet(user_idx)));
    items.append(cell(safeGet(host_idx)));
    items.append(cell(safeGet(db_idx)));
    items.append(cell(safeGet(cmd_idx)));
    items.append(cell(safeGet(time_idx)));
    items.append(cell(safeGet(state_idx)));
    items.append(cell(safeGet(info_idx)));

    process_model_->appendRow(items);
  }

  LogMessage(tr("Process list refreshed — %1 connections").arg(proc_result.rows.size()));
}

// =============================================================================
// Private Slots — Server Actions
// =============================================================================

void AdminPanel::OnFlushHosts() {
  LogMessage(tr("FLUSH HOSTS..."));
  QueryResult r = ExecuteSql("FLUSH HOSTS");
  if (r.is_error) {
    LogMessage(tr("FLUSH HOSTS failed: %1").arg(r.error_message), true);
  } else {
    LogMessage(tr("FLUSH HOSTS — OK"));
  }
}

void AdminPanel::OnFlushLogs() {
  LogMessage(tr("FLUSH LOGS..."));
  QueryResult r = ExecuteSql("FLUSH LOGS");
  if (r.is_error) {
    LogMessage(tr("FLUSH LOGS failed: %1").arg(r.error_message), true);
  } else {
    LogMessage(tr("FLUSH LOGS — OK"));
  }
}

void AdminPanel::OnFlushTables() {
  LogMessage(tr("FLUSH TABLES..."));
  QueryResult r = ExecuteSql("FLUSH TABLES");
  if (r.is_error) {
    LogMessage(tr("FLUSH TABLES failed: %1").arg(r.error_message), true);
  } else {
    LogMessage(tr("FLUSH TABLES — OK"));
  }
}

void AdminPanel::OnReload() {
  LogMessage(tr("RELOAD..."));
  // Some systems use FLUSH PRIVILEGES instead of RELOAD
  QueryResult r = ExecuteSql("RELOAD");
  if (r.is_error) {
    LogMessage(tr("RELOAD failed: %1").arg(r.error_message), true);
  } else {
    LogMessage(tr("RELOAD — OK"));
  }
}

void AdminPanel::OnPing() {
  LogMessage(tr("PING..."));
  auto* conn = pool_->GetActiveConnection();
  if (!conn || !conn->IsConnected()) {
    LogMessage(tr("PING failed: no active connection"), true);
    return;
  }
  bool ok = conn->Ping();
  if (ok) {
    LogMessage(tr("PING — server is alive"));
  } else {
    LogMessage(tr("PING failed: %1").arg(conn->GetLastError()), true);
  }
}

void AdminPanel::OnShutdown() {
  QMessageBox::StandardButton reply = QMessageBox::warning(
      this,
      tr("Confirm Shutdown"),
      tr("Are you sure you want to shutdown the database server?\n\n"
         "This will disconnect all clients and stop the server process.\n"
         "The server will need to be manually restarted."),
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No);

  if (reply != QMessageBox::Yes) {
    LogMessage(tr("SHUTDOWN cancelled by user"));
    return;
  }

  LogMessage(tr("SHUTDOWN..."));
  QueryResult r = ExecuteSql("SHUTDOWN");
  if (r.is_error) {
    LogMessage(tr("SHUTDOWN failed: %1").arg(r.error_message), true);
  } else {
    LogMessage(tr("SHUTDOWN — server shutting down"));
    // Stop auto-refresh since the server is gone
    refresh_timer_->stop();
  }
}

void AdminPanel::OnKillProcess() {
  auto selection = process_view_->selectionModel()->selectedRows();
  if (selection.isEmpty()) return;

  // Get the connection ID from the first column of the selected row
  int row = selection.first().row();
  QString conn_id = process_model_->item(row, 0)->text();

  QMessageBox::StandardButton reply = QMessageBox::question(
      this,
      tr("Confirm Kill"),
      tr("Are you sure you want to kill connection %1?").arg(conn_id),
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No);

  if (reply != QMessageBox::Yes) return;

  QString sql = QString("KILL %1").arg(conn_id);
  LogMessage(sql + "...");
  QueryResult r = ExecuteSql(sql);
  if (r.is_error) {
    LogMessage(tr("KILL failed: %1").arg(r.error_message), true);
  } else {
    LogMessage(tr("KILL %1 — OK").arg(conn_id));
    Refresh();  // Refresh process list immediately
  }
}

void AdminPanel::OnAutoRefreshToggled(bool enabled) {
  if (enabled) {
    refresh_timer_->start(refresh_interval_spin_->value() * 1000);
    LogMessage(tr("Auto-refresh enabled (%1s)").arg(refresh_interval_spin_->value()));
  } else {
    refresh_timer_->stop();
    LogMessage(tr("Auto-refresh disabled"));
  }
}

void AdminPanel::OnRefreshIntervalChanged(int seconds) {
  if (auto_refresh_cb_->isChecked()) {
    refresh_timer_->start(seconds * 1000);
    LogMessage(tr("Refresh interval changed to %1s").arg(seconds));
  }
}

}  // namespace studio
}  // namespace goods_db
