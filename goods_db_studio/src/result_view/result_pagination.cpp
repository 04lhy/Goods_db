#include "result_view/result_pagination.h"

#include <QHBoxLayout>
#include <cmath>

namespace goods_db {
namespace studio {

ResultPagination::ResultPagination(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);

  prev_btn_ = new QPushButton(tr("← Prev"));
  prev_btn_->setFixedWidth(80);
  layout->addWidget(prev_btn_);

  info_label_ = new QLabel();
  info_label_->setAlignment(Qt::AlignCenter);
  layout->addWidget(info_label_, 1);

  next_btn_ = new QPushButton(tr("Next →"));
  next_btn_->setFixedWidth(80);
  layout->addWidget(next_btn_);

  layout->addSpacing(16);

  QLabel* jump_label = new QLabel(tr("Jump to:"));
  layout->addWidget(jump_label);

  jump_edit_ = new QLineEdit();
  jump_edit_->setFixedWidth(60);
  jump_edit_->setPlaceholderText(tr("page"));
  layout->addWidget(jump_edit_);

  jump_btn_ = new QPushButton(tr("Go"));
  jump_btn_->setFixedWidth(50);
  layout->addWidget(jump_btn_);

  layout->addStretch();

  connect(prev_btn_, &QPushButton::clicked, this, &ResultPagination::OnPrevPage);
  connect(next_btn_, &QPushButton::clicked, this, &ResultPagination::OnNextPage);
  connect(jump_btn_, &QPushButton::clicked, this, &ResultPagination::OnJumpToPage);
  connect(jump_edit_, &QLineEdit::returnPressed, this, &ResultPagination::OnJumpToPage);

  UpdateControls();
}

void ResultPagination::SetTotalRows(int64_t total) {
  total_rows_ = total;
  current_page_ = 1;
  UpdateControls();
}

void ResultPagination::UpdateControls() {
  int total_pages = static_cast<int>(
      std::ceil(static_cast<double>(total_rows_) / page_size_));
  if (total_pages < 1) total_pages = 1;

  info_label_->setText(
      tr("Page %1 / %2  |  Total: %3 rows")
          .arg(current_page_)
          .arg(total_pages)
          .arg(total_rows_));

  prev_btn_->setEnabled(current_page_ > 1);
  next_btn_->setEnabled(current_page_ < total_pages);
}

void ResultPagination::OnPrevPage() {
  if (current_page_ > 1) {
    current_page_--;
    UpdateControls();
    emit PageChanged(current_page_, GetOffset());
  }
}

void ResultPagination::OnNextPage() {
  int total_pages = static_cast<int>(
      std::ceil(static_cast<double>(total_rows_) / page_size_));
  if (current_page_ < total_pages) {
    current_page_++;
    UpdateControls();
    emit PageChanged(current_page_, GetOffset());
  }
}

void ResultPagination::OnJumpToPage() {
  bool ok;
  int page = jump_edit_->text().toInt(&ok);
  int total_pages = static_cast<int>(
      std::ceil(static_cast<double>(total_rows_) / page_size_));

  if (ok && page >= 1 && page <= total_pages) {
    current_page_ = page;
    UpdateControls();
    emit PageChanged(current_page_, GetOffset());
  }
}

}  // namespace studio
}  // namespace goods_db
