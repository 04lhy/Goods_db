#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QTableView>
#include <QStandardItemModel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QPlainTextEdit>
#include <QDateTimeEdit>
#include <QCheckBox>

#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

class ConnectionPool;

// =============================================================================
// LogViewer — server log browser
//
// Three tabs:
//   - Error Log:   SHOW ERRORLOG / system error messages
//   - Query Log:   SHOW QUERYLOG / SQL execution history
//   - Binary Log:  SHOW BINARYLOG / data change events
//
// Each tab has:
//   - Top filter bar (level/date/keyword)
//   - Middle content area (QTableView for structured, QPlainTextEdit for raw)
//   - Bottom search bar with find-next/find-prev
// =============================================================================
class LogViewer : public QWidget {
  Q_OBJECT

 public:
  explicit LogViewer(ConnectionPool* pool, QWidget* parent = nullptr);

 public slots:
  void Refresh();

 private slots:
  void OnSearch();
  void OnSearchNext();
  void OnSearchPrev();
  void OnFilterChanged();
  void OnAutoRefreshToggled(bool enabled);

 private:
  void SetupUi();
  QWidget* CreateErrorLogTab();
  QWidget* CreateQueryLogTab();
  QWidget* CreateBinaryLogTab();
  QWidget* CreateFilterBar(const QStringList& level_options);
  QWidget* CreateSearchBar();

  QueryResult ExecuteSql(const QString& sql);

  // Common filter bar widgets
  QComboBox* level_combo_;
  QDateTimeEdit* start_date_;
  QDateTimeEdit* end_date_;
  QLineEdit* keyword_edit_;
  QPushButton* apply_filter_btn_;
  QPushButton* clear_filter_btn_;

  // Content widgets per tab
  QTableView* error_table_view_;
  QStandardItemModel* error_model_;
  QTableView* query_table_view_;
  QStandardItemModel* query_model_;
  QPlainTextEdit* binary_log_text_;

  // Log tab widget
  QTabWidget* log_tabs_;

  // Search bar widgets
  QLineEdit* search_edit_;
  QPushButton* search_next_btn_;
  QPushButton* search_prev_btn_;

  // Controls
  ConnectionPool* pool_;
  QTimer* refresh_timer_;
  QCheckBox* auto_refresh_cb_;
  QPushButton* refresh_btn_;

  // Last search position
  int last_search_pos_ = -1;
};

}  // namespace studio
}  // namespace goods_db
