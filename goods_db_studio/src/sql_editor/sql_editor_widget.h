#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QTextBlock>

namespace goods_db {
namespace studio {

// =============================================================================
// SqlEditor — QPlainTextEdit subclass that exposes protected members for
// the line number area widget.
// =============================================================================
class SqlEditor : public QPlainTextEdit {
  Q_OBJECT
 public:
  explicit SqlEditor(QWidget* parent = nullptr) : QPlainTextEdit(parent) {}

  void SetViewportMargins(int left, int top, int right, int bottom) {
    setViewportMargins(left, top, right, bottom);
  }
  QTextBlock FirstVisibleBlock() const { return firstVisibleBlock(); }
  QRectF BlockBoundingGeometry(const QTextBlock& block) const {
    return blockBoundingGeometry(block);
  }
  QRectF BlockBoundingRect(const QTextBlock& block) const {
    return blockBoundingRect(block);
  }
  QPointF ContentOffset() const { return contentOffset(); }
};

// =============================================================================
// SqlEditorWidget — multi-tab SQL editor with line numbers and syntax highlighting
//
// Features:
//   - Ctrl+T: new tab   Ctrl+W: close tab
//   - Line number area on each editor
//   - SQL syntax highlighting (via SqlHighlighter)
//   - Current line highlighting
// =============================================================================
class SqlEditorWidget : public QWidget {
  Q_OBJECT

 public:
  explicit SqlEditorWidget(QWidget* parent = nullptr);

  QString GetCurrentSql() const;
  QString GetSelectedOrAllSql() const;
  void SetSql(const QString& sql);

  void NewTab();
  void CloseTab(int index);
  int GetTabCount() const { return tab_widget_->count(); }
  QPlainTextEdit* GetCurrentEditor() const;

 signals:
  void TabChanged(int index);
  void TabClosed(int index);

 private:
  QTabWidget* tab_widget_;
  int next_tab_number_ = 1;

  SqlEditor* CreateEditor();
  void SetupEditor(SqlEditor* editor);
};

// =============================================================================
// LineNumberArea — line number widget painted next to the editor
// =============================================================================
class LineNumberArea : public QWidget {
 public:
  explicit LineNumberArea(SqlEditor* editor);

  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  SqlEditor* editor_;
};

}  // namespace studio
}  // namespace goods_db
