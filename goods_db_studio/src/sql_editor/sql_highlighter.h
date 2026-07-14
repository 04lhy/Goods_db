#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <vector>

namespace goods_db {
namespace studio {

// =============================================================================
// SqlHighlighter — SQL syntax highlighter for QPlainTextEdit
//
// Highlights 6 categories:
//   - Keywords (blue bold): SELECT, FROM, WHERE, etc. (~80 keywords)
//   - Data Types (cyan): INT, VARCHAR, DECIMAL, etc.
//   - Strings (red): '...'
//   - Numbers (orange): integers and decimals
//   - Comments (gray italic): -- single-line, /* multi-line */
//   - Functions (purple): COUNT, SUM, AVG, etc.
// =============================================================================
class SqlHighlighter : public QSyntaxHighlighter {
  Q_OBJECT

 public:
  explicit SqlHighlighter(QTextDocument* parent = nullptr);

 protected:
  void highlightBlock(const QString& text) override;

 private:
  struct HighlightingRule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };

  std::vector<HighlightingRule> rules_;
  QRegularExpression multi_line_comment_start_;
  QRegularExpression multi_line_comment_end_;
  QTextCharFormat multi_line_comment_format_;

  void SetupRules();
};

}  // namespace studio
}  // namespace goods_db
