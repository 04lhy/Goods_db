#include "sql_editor/sql_editor_widget.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QTextBlock>
#include <QVBoxLayout>

#include "sql_editor/sql_highlighter.h"

namespace goods_db {
namespace studio {

// ---- SqlEditorWidget --------------------------------------------------------

SqlEditorWidget::SqlEditorWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  tab_widget_ = new QTabWidget(this);
  tab_widget_->setTabsClosable(true);
  tab_widget_->setMovable(true);
  tab_widget_->setDocumentMode(true);
  layout->addWidget(tab_widget_);

  connect(tab_widget_, &QTabWidget::tabCloseRequested,
          this, &SqlEditorWidget::CloseTab);
  connect(tab_widget_, &QTabWidget::currentChanged,
          this, &SqlEditorWidget::TabChanged);

  NewTab();
}

SqlEditor* SqlEditorWidget::CreateEditor() {
  auto* editor = new SqlEditor();
  SetupEditor(editor);
  return editor;
}

void SqlEditorWidget::SetupEditor(SqlEditor* editor) {
  QFont font("DejaVu Sans Mono", 11);
  font.setStyleHint(QFont::Monospace);
  editor->setFont(font);

  editor->setTabStopDistance(
      QFontMetrics(editor->font()).horizontalAdvance(' ') * 4);
  editor->setLineWrapMode(QPlainTextEdit::NoWrap);

  // Syntax highlighter
  new SqlHighlighter(editor->document());

  // Line number area
  auto* line_area = new LineNumberArea(editor);

  connect(editor, &QPlainTextEdit::blockCountChanged,
          [editor, line_area](int) {
    editor->SetViewportMargins(line_area->sizeHint().width(), 0, 0, 0);
  });

  connect(editor, &QPlainTextEdit::updateRequest,
          [editor, line_area](const QRect& rect, int dy) {
    if (dy) {
      line_area->scroll(0, dy);
    } else {
      line_area->update(0, rect.y(), line_area->width(), rect.height());
    }
  });

  editor->SetViewportMargins(line_area->sizeHint().width(), 0, 0, 0);
}

void SqlEditorWidget::NewTab() {
  auto* editor = CreateEditor();
  QString title = tr("Query %1").arg(next_tab_number_++);
  int index = tab_widget_->addTab(editor, title);
  tab_widget_->setCurrentIndex(index);
}

void SqlEditorWidget::CloseTab(int index) {
  if (tab_widget_->count() <= 1) {
    auto* editor = qobject_cast<QPlainTextEdit*>(tab_widget_->widget(index));
    if (editor) editor->clear();
    return;
  }
  QWidget* widget = tab_widget_->widget(index);
  tab_widget_->removeTab(index);
  delete widget;
  emit TabClosed(index);
}

QString SqlEditorWidget::GetCurrentSql() const {
  auto* editor = GetCurrentEditor();
  if (editor) return editor->toPlainText();
  return QString();
}

QString SqlEditorWidget::GetSelectedOrAllSql() const {
  auto* editor = GetCurrentEditor();
  if (!editor) return QString();
  QTextCursor cursor = editor->textCursor();
  if (cursor.hasSelection()) return cursor.selectedText();
  return editor->toPlainText();
}

void SqlEditorWidget::SetSql(const QString& sql) {
  auto* editor = qobject_cast<QPlainTextEdit*>(GetCurrentEditor());
  if (!editor) {
    NewTab();
    editor = qobject_cast<QPlainTextEdit*>(GetCurrentEditor());
  }
  if (editor) editor->setPlainText(sql);
}

QPlainTextEdit* SqlEditorWidget::GetCurrentEditor() const {
  return qobject_cast<QPlainTextEdit*>(tab_widget_->currentWidget());
}

// ---- LineNumberArea ---------------------------------------------------------

LineNumberArea::LineNumberArea(SqlEditor* editor)
    : QWidget(editor), editor_(editor) {}

QSize LineNumberArea::sizeHint() const {
  int digits = 1;
  int max = qMax(1, editor_->blockCount());
  while (max >= 10) { max /= 10; digits++; }
  int width = 10 + QFontMetrics(editor_->font()).horizontalAdvance(QLatin1Char('9')) * digits;
  return QSize(width, 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.fillRect(event->rect(), QColor("#1e1e1e"));

  QTextBlock block = editor_->FirstVisibleBlock();
  int block_number = block.blockNumber();
  QRectF geom = editor_->BlockBoundingGeometry(block);
  geom.translate(editor_->ContentOffset());
  int top = qRound(geom.top());
  int bottom = top + qRound(editor_->BlockBoundingRect(block).height());

  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      QString number = QString::number(block_number + 1);
      painter.setPen(QColor("#858585"));
      painter.drawText(0, top, width() - 4,
                       editor_->fontMetrics().height(),
                       Qt::AlignRight, number);
    }

    block = block.next();
    top = bottom;
    bottom = top + qRound(editor_->BlockBoundingRect(block).height());
    block_number++;
  }
}

}  // namespace studio
}  // namespace goods_db
