#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <cstdint>

namespace goods_db {
namespace studio {

// =============================================================================
// ResultPagination — pagination controls for large result sets
//
// Layout: [Prev] Page X / Y [Next]  Jump to: [___] [Go]  | Total: Z rows
// =============================================================================
class ResultPagination : public QWidget {
  Q_OBJECT

 public:
  explicit ResultPagination(QWidget* parent = nullptr);

  void SetTotalRows(int64_t total);
  void SetPageSize(int size) { page_size_ = size; }
  int GetCurrentPage() const { return current_page_; }
  int GetOffset() const { return (current_page_ - 1) * page_size_; }

 signals:
  void PageChanged(int page, int offset);

 private slots:
  void OnPrevPage();
  void OnNextPage();
  void OnJumpToPage();

 private:
  int64_t total_rows_ = 0;
  int page_size_ = 1000;
  int current_page_ = 1;

  QPushButton* prev_btn_;
  QPushButton* next_btn_;
  QLineEdit* jump_edit_;
  QPushButton* jump_btn_;
  QLabel* info_label_;

  void UpdateControls();
};

}  // namespace studio
}  // namespace goods_db
