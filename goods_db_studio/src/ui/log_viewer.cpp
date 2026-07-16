#include "ui/log_viewer.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QHeaderView>
#include <QDateTime>
#include <QTextDocument>
#include <QScrollBar>

#include "connection/connection_pool.h"

namespace goods_db {
namespace studio {

// =============================================================================
// LogViewer
// =============================================================================

LogViewer::LogViewer(ConnectionPool* pool, QWidget* parent)
    : QWidget(parent), pool_(pool) {
  SetupUi();

  refresh_timer_ = new QTimer(this);
  connect(refresh_timer_, &QTimer::timeout, this, &LogViewer::Refresh);
}

void LogViewer::SetupUi() {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(8, 8, 8, 8);
  main_layout->setSpacing(8);

  // ---- Filter Bar ----
  auto* filter_group = new QGroupBox(tr("Filters"), this);
  auto* filter_layout = new QHBoxLayout(filter_group);

  filter_layout->addWidget(new QLabel(tr("Level:"), filter_group));
  level_combo_ = new QComboBox(filter_group);
  level_combo_->addItems({tr("All"), "INFO", "WARNING", "ERROR", "FATAL"});
  filter_layout->addWidget(level_combo_);

  filter_layout->addWidget(new QLabel(tr("From:"), filter_group));
  start_date_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-1), filter_group);
  start_date_->setDisplayFormat("yyyy-MM-dd hh:mm");
  start_date_->setCalendarPopup(true);
  filter_layout->addWidget(start_date_);

  filter_layout->addWidget(new QLabel(tr("To:"), filter_group));
  end_date_ = new QDateTimeEdit(QDateTime::currentDateTime(), filter_group);
  end_date_->setDisplayFormat("yyyy-MM-dd hh:mm");
  end_date_->setCalendarPopup(true);
  filter_layout->addWidget(end_date_);

  filter_layout->addWidget(new QLabel(tr("Keyword:"), filter_group));
  keyword_edit_ = new QLineEdit(filter_group);
  keyword_edit_->setPlaceholderText(tr("Search keyword..."));
  keyword_edit_->setMaximumWidth(200);
  connect(keyword_edit_, &QLineEdit::returnPressed, this, &LogViewer::OnFilterChanged);
  filter_layout->addWidget(keyword_edit_);

  apply_filter_btn_ = new QPushButton(tr("Apply"), filter_group);
  apply_filter_btn_->setIcon(QIcon(":/icons/filter.svg"));
  connect(apply_filter_btn_, &QPushButton::clicked, this, &LogViewer::OnFilterChanged);
  filter_layout->addWidget(apply_filter_btn_);

  clear_filter_btn_ = new QPushButton(tr("Clear"), filter_group);
  clear_filter_btn_->setIcon(QIcon(":/icons/clear_filter.svg"));
  connect(clear_filter_btn_, &QPushButton::clicked, this, [this]() {
    level_combo_->setCurrentIndex(0);
    keyword_edit_->clear();
    start_date_->setDateTime(QDateTime::currentDateTime().addDays(-1));
    end_date_->setDateTime(QDateTime::currentDateTime());
    Refresh();
  });
  filter_layout->addWidget(clear_filter_btn_);

  main_layout->addWidget(filter_group, 0);

  // ---- Log Tabs ----
  log_tabs_ = new QTabWidget(this);

  log_tabs_->addTab(CreateErrorLogTab(), tr("Error Log"));
  log_tabs_->addTab(CreateQueryLogTab(), tr("Query Log"));
  log_tabs_->addTab(CreateBinaryLogTab(), tr("Binary Log"));

  main_layout->addWidget(log_tabs_, 1);

  // ---- Search Bar ----
  main_layout->addWidget(CreateSearchBar(), 0);
}

QWidget* LogViewer::CreateErrorLogTab() {
  auto* widget = new QWidget(this);
  auto* layout = new QVBoxLayout(widget);
  layout->setContentsMargins(4, 4, 4, 4);

  error_model_ = new QStandardItemModel(0, 5, widget);
  error_model_->setHorizontalHeaderLabels({
      tr("Time"), tr("Level"), tr("Code"), tr("Message"), tr("Source")
  });

  error_table_view_ = new QTableView(widget);
  error_table_view_->setModel(error_model_);
  error_table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  error_table_view_->setAlternatingRowColors(true);
  error_table_view_->setSortingEnabled(true);
  error_table_view_->verticalHeader()->setVisible(false);
  error_table_view_->horizontalHeader()->setStretchLastSection(true);
  error_table_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  error_table_view_->setColumnWidth(0, 160);  // Time
  error_table_view_->setColumnWidth(1, 70);   // Level
  error_table_view_->setColumnWidth(2, 60);   // Code
  error_table_view_->setColumnWidth(3, 350);  // Message
  error_table_view_->setColumnWidth(4, 150);  // Source

  layout->addWidget(error_table_view_);
  return widget;
}

QWidget* LogViewer::CreateQueryLogTab() {
  auto* widget = new QWidget(this);
  auto* layout = new QVBoxLayout(widget);
  layout->setContentsMargins(4, 4, 4, 4);

  query_model_ = new QStandardItemModel(0, 7, widget);
  query_model_->setHorizontalHeaderLabels({
      tr("Time"), tr("User"), tr("Host"), tr("Database"),
      tr("Duration"), tr("Rows"), tr("Query")
  });

  query_table_view_ = new QTableView(widget);
  query_table_view_->setModel(query_model_);
  query_table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  query_table_view_->setAlternatingRowColors(true);
  query_table_view_->setSortingEnabled(true);
  query_table_view_->verticalHeader()->setVisible(false);
  query_table_view_->horizontalHeader()->setStretchLastSection(true);
  query_table_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  query_table_view_->setColumnWidth(0, 160);  // Time
  query_table_view_->setColumnWidth(1, 70);   // User
  query_table_view_->setColumnWidth(2, 100);  // Host
  query_table_view_->setColumnWidth(3, 70);   // Database
  query_table_view_->setColumnWidth(4, 70);   // Duration
  query_table_view_->setColumnWidth(5, 60);   // Rows

  layout->addWidget(query_table_view_);
  return widget;
}

QWidget* LogViewer::CreateBinaryLogTab() {
  auto* widget = new QWidget(this);
  auto* layout = new QVBoxLayout(widget);
  layout->setContentsMargins(4, 4, 4, 4);

  binary_log_text_ = new QPlainTextEdit(widget);
  binary_log_text_->setReadOnly(true);
  binary_log_text_->setFont(QFont("Consolas, Menlo, monospace", 10));
  binary_log_text_->setPlaceholderText(tr("Binary log events will appear here..."));
  binary_log_text_->setLineWrapMode(QPlainTextEdit::NoWrap);
  layout->addWidget(binary_log_text_);

  return widget;
}

QWidget* LogViewer::CreateSearchBar() {
  auto* widget = new QWidget(this);
  auto* layout = new QHBoxLayout(widget);
  layout->setContentsMargins(0, 0, 0, 0);

  layout->addWidget(new QLabel(tr("Find:"), widget));

  search_edit_ = new QLineEdit(widget);
  search_edit_->setPlaceholderText(tr("Enter text to search in current log..."));
  search_edit_->setMinimumWidth(250);
  connect(search_edit_, &QLineEdit::returnPressed, this, &LogViewer::OnSearchNext);
  layout->addWidget(search_edit_);

  search_prev_btn_ = new QPushButton(tr("▲ Prev"), widget);
  search_prev_btn_->setIcon(QIcon(":/icons/search.svg"));
  connect(search_prev_btn_, &QPushButton::clicked, this, &LogViewer::OnSearchPrev);
  layout->addWidget(search_prev_btn_);

  search_next_btn_ = new QPushButton(tr("▼ Next"), widget);
  search_next_btn_->setIcon(QIcon(":/icons/search.svg"));
  connect(search_next_btn_, &QPushButton::clicked, this, &LogViewer::OnSearchNext);
  layout->addWidget(search_next_btn_);

  layout->addStretch();

  // Refresh controls
  auto_refresh_cb_ = new QCheckBox(tr("Auto-refresh (30s)"), widget);
  connect(auto_refresh_cb_, &QCheckBox::toggled,
          this, &LogViewer::OnAutoRefreshToggled);
  layout->addWidget(auto_refresh_cb_);

  refresh_btn_ = new QPushButton(tr("Refresh"), widget);
  refresh_btn_->setIcon(QIcon(":/icons/refresh.svg"));
  connect(refresh_btn_, &QPushButton::clicked, this, &LogViewer::Refresh);
  layout->addWidget(refresh_btn_);

  return widget;
}

// =============================================================================
// Helpers
// =============================================================================

QueryResult LogViewer::ExecuteSql(const QString& sql) {
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
// Public Slots
// =============================================================================

void LogViewer::Refresh() {
  int current_tab = log_tabs_->currentIndex();

  // Build filter conditions
  QString time_where;
  QString start_str = start_date_->dateTime().toString("yyyy-MM-dd HH:mm:ss");
  QString end_str = end_date_->dateTime().toString("yyyy-MM-dd HH:mm:ss");
  time_where = QString("WHERE time >= '%1' AND time <= '%2'").arg(start_str, end_str);

  QString level_filter;
  if (level_combo_->currentIndex() > 0) {
    level_filter = QString(" AND level = '%1'").arg(level_combo_->currentText());
  }

  QString keyword_filter;
  QString keyword = keyword_edit_->text().trimmed();
  if (!keyword.isEmpty()) {
    keyword_filter = QString(" AND message LIKE '%%1%'").arg(keyword);
  }

  QString limit_clause = " LIMIT 200";

  // ---- Error Log ----
  if (current_tab == 0 || error_model_->rowCount() == 0) {
    QueryResult result = ExecuteSql(
        "SHOW ERRORLOG " + time_where + level_filter + keyword_filter + limit_clause);
    error_model_->removeRows(0, error_model_->rowCount());
    if (!result.is_error) {
      for (const auto& row : result.rows) {
        QList<QStandardItem*> items;
        for (size_t i = 0; i < row.size() && i < 5; i++) {
          items.append(new QStandardItem(row[i]));
        }
        // Pad if fewer columns
        while (items.size() < 5) items.append(new QStandardItem(""));
        error_model_->appendRow(items);
      }
    }
  }

  // ---- Query Log ----
  if (current_tab == 1 || query_model_->rowCount() == 0) {
    QueryResult result = ExecuteSql(
        "SHOW QUERYLOG " + time_where + keyword_filter + limit_clause);
    query_model_->removeRows(0, query_model_->rowCount());
    if (!result.is_error) {
      for (const auto& row : result.rows) {
        QList<QStandardItem*> items;
        for (size_t i = 0; i < row.size() && i < 7; i++) {
          items.append(new QStandardItem(row[i]));
        }
        while (items.size() < 7) items.append(new QStandardItem(""));
        query_model_->appendRow(items);
      }
    }
  }

  // ---- Binary Log ----
  if (current_tab == 2) {
    QueryResult result = ExecuteSql(
        "SHOW BINARYLOG " + time_where + keyword_filter + limit_clause);
    binary_log_text_->clear();
    if (!result.is_error) {
      QString text;
      for (const auto& row : result.rows) {
        QStringList fields;
        for (const auto& cell : row) fields.append(cell);
        text += fields.join(" | ") + "\n";
      }
      binary_log_text_->setPlainText(text);
    }
  }
}

// =============================================================================
// Private Slots
// =============================================================================

void LogViewer::OnSearch() {
  QString search_text = search_edit_->text();
  if (search_text.isEmpty()) return;

  // Search in the current tab's content
  int current_tab = log_tabs_->currentIndex();

  if (current_tab == 0 && error_model_) {
    for (int r = std::max(0, last_search_pos_); r < error_model_->rowCount(); r++) {
      for (int c = 0; c < error_model_->columnCount(); c++) {
        auto* item = error_model_->item(r, c);
        if (item && item->text().contains(search_text, Qt::CaseInsensitive)) {
          error_table_view_->selectRow(r);
          error_table_view_->scrollTo(item->index());
          last_search_pos_ = r + 1;
          return;
        }
      }
    }
  } else if (current_tab == 1 && query_model_) {
    for (int r = std::max(0, last_search_pos_); r < query_model_->rowCount(); r++) {
      for (int c = 0; c < query_model_->columnCount(); c++) {
        auto* item = query_model_->item(r, c);
        if (item && item->text().contains(search_text, Qt::CaseInsensitive)) {
          query_table_view_->selectRow(r);
          query_table_view_->scrollTo(item->index());
          last_search_pos_ = r + 1;
          return;
        }
      }
    }
  } else if (current_tab == 2 && binary_log_text_) {
    QTextDocument* doc = binary_log_text_->document();
    QTextCursor cursor = doc->find(search_text, last_search_pos_);
    if (!cursor.isNull()) {
      binary_log_text_->setTextCursor(cursor);
      last_search_pos_ = cursor.position();
      return;
    }
  }

  // Not found — wrap around
  last_search_pos_ = 0;
}

void LogViewer::OnSearchNext() {
  last_search_pos_ = 0;
  OnSearch();
}

void LogViewer::OnSearchPrev() {
  // Simplified: just search from the beginning
  // A full implementation would search backward
  last_search_pos_ = 0;
  OnSearch();
}

void LogViewer::OnFilterChanged() {
  last_search_pos_ = 0;
  Refresh();
}

void LogViewer::OnAutoRefreshToggled(bool enabled) {
  if (enabled) {
    refresh_timer_->start(30000);  // 30 seconds
  } else {
    refresh_timer_->stop();
  }
}

}  // namespace studio
}  // namespace goods_db
