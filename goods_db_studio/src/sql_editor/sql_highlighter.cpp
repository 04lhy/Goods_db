#include "sql_editor/sql_highlighter.h"

namespace goods_db {
namespace studio {

SqlHighlighter::SqlHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
  SetupRules();
}

void SqlHighlighter::SetupRules() {
  HighlightingRule rule;

  // ---- Keywords (blue bold) ------------------------------------------------
  QTextCharFormat keyword_format;
  keyword_format.setForeground(QColor("#569CD6"));
  keyword_format.setFontWeight(QFont::Bold);

  const QStringList keywords = {
    "SELECT", "FROM", "WHERE", "INSERT", "INTO", "UPDATE", "DELETE", "CREATE",
    "DROP", "ALTER", "TABLE", "INDEX", "JOIN", "ON", "GROUP", "ORDER", "BY",
    "HAVING", "LIMIT", "UNION", "AS", "IN", "NOT", "NULL", "IS", "AND", "OR",
    "BETWEEN", "LIKE", "SET", "VALUES", "PRIMARY", "KEY", "FOREIGN",
    "REFERENCES", "DISTINCT", "EXISTS", "CASE", "WHEN", "THEN", "ELSE", "END",
    "LEFT", "RIGHT", "INNER", "OUTER", "CROSS", "FULL", "COMMIT", "ROLLBACK",
    "GRANT", "REVOKE", "EXPLAIN", "START", "TRANSACTION", "BEGIN", "IF",
    "TRUE", "FALSE", "TRUNCATE", "REPLACE", "ASC", "DESC", "OFFSET", "FETCH",
    "NEXT", "ROWS", "ONLY", "WITH", "RECURSIVE", "ALL", "ANY", "SOME", "VIEW",
    "DATABASE", "SCHEMA", "ADD", "COLUMN", "RENAME", "TO", "DEFAULT",
    "CONSTRAINT", "UNIQUE", "CHECK", "CASCADE", "RESTRICT", "USE", "SHOW",
    "DESCRIBE", "LOCK", "UNLOCK", "FLUSH", "TABLES", "HOSTS", "LOGS"
  };
  for (const QString& kw : keywords) {
    rule.pattern = QRegularExpression(
        "\\b" + kw + "\\b", QRegularExpression::CaseInsensitiveOption);
    rule.format = keyword_format;
    rules_.push_back(rule);
  }

  // ---- Data types (cyan) ---------------------------------------------------
  QTextCharFormat type_format;
  type_format.setForeground(QColor("#4EC9B0"));

  const QStringList types = {
    "INT", "INTEGER", "BIGINT", "SMALLINT", "TINYINT", "BOOLEAN", "BIT",
    "DECIMAL", "NUMERIC", "DOUBLE", "FLOAT", "REAL", "CHAR", "VARCHAR",
    "TEXT", "TINYTEXT", "MEDIUMTEXT", "LONGTEXT", "BLOB", "TIMESTAMP",
    "DATE", "DATETIME", "TIME", "YEAR", "ENUM", "SET", "SERIAL", "JSON",
    "VARBINARY", "BINARY"
  };
  for (const QString& t : types) {
    rule.pattern = QRegularExpression(
        "\\b" + t + "\\b", QRegularExpression::CaseInsensitiveOption);
    rule.format = type_format;
    rules_.push_back(rule);
  }

  // ---- Strings (red/orange) ------------------------------------------------
  QTextCharFormat string_format;
  string_format.setForeground(QColor("#CE9178"));
  rule.pattern = QRegularExpression("'[^']*'");
  rule.format = string_format;
  rules_.push_back(rule);

  // Double-quoted identifiers (darker red)
  QTextCharFormat dq_format;
  dq_format.setForeground(QColor("#D7BA7D"));
  rule.pattern = QRegularExpression("\"[^\"]*\"");
  rule.format = dq_format;
  rules_.push_back(rule);

  // ---- Numbers (orange) ----------------------------------------------------
  QTextCharFormat number_format;
  number_format.setForeground(QColor("#B5CEA8"));
  rule.pattern = QRegularExpression("\\b\\d+(\\.\\d+)?\\b");
  rule.format = number_format;
  rules_.push_back(rule);

  // ---- Functions (purple) --------------------------------------------------
  QTextCharFormat func_format;
  func_format.setForeground(QColor("#C586C0"));

  const QStringList functions = {
    "COUNT", "SUM", "AVG", "MAX", "MIN", "COALESCE", "IFNULL", "NULLIF",
    "NOW", "CURDATE", "CURTIME", "DATE_FORMAT", "DATEDIFF", "TIMESTAMPDIFF",
    "UPPER", "LOWER", "TRIM", "LTRIM", "RTRIM", "LENGTH", "CHAR_LENGTH",
    "SUBSTRING", "SUBSTR", "REPLACE", "CONCAT", "CONCAT_WS", "GROUP_CONCAT",
    "ABS", "ROUND", "CEIL", "CEILING", "FLOOR", "MOD", "POW", "POWER",
    "SQRT", "LOG", "LOG10", "EXP", "RAND", "SIGN", "GREATEST", "LEAST",
    "IF", "IFNULL", "CAST", "CONVERT", "EXTRACT", "YEAR", "MONTH", "DAY",
    "HOUR", "MINUTE", "SECOND", "WEEKDAY", "DAYOFWEEK", "DAYOFYEAR",
    "LAST_INSERT_ID", "DATABASE", "VERSION", "USER", "CURRENT_USER"
  };
  for (const QString& fn : functions) {
    rule.pattern = QRegularExpression(
        "\\b" + fn + "\\s*(?=\\()",
        QRegularExpression::CaseInsensitiveOption);
    rule.format = func_format;
    rules_.push_back(rule);
  }

  // ---- Single-line comments (gray italic) ----------------------------------
  QTextCharFormat comment_format;
  comment_format.setForeground(QColor("#6A9955"));
  comment_format.setFontItalic(true);
  rule.pattern = QRegularExpression("--[^\n]*");
  rule.format = comment_format;
  rules_.push_back(rule);

  // ---- Multi-line comments -------------------------------------------------
  multi_line_comment_start_ = QRegularExpression("/\\*");
  multi_line_comment_end_ = QRegularExpression("\\*/");
  multi_line_comment_format_ = comment_format;
}

void SqlHighlighter::highlightBlock(const QString& text) {
  // Apply single-pattern rules
  for (const auto& rule : rules_) {
    QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
    while (it.hasNext()) {
      QRegularExpressionMatch match = it.next();
      setFormat(match.capturedStart(), match.capturedLength(), rule.format);
    }
  }

  // Handle multi-line comments
  setCurrentBlockState(0);

  int start_index = 0;
  if (previousBlockState() != 1) {
    QRegularExpressionMatch start_match =
        multi_line_comment_start_.match(text, start_index);
    start_index = start_match.hasMatch() ? start_match.capturedStart() : -1;
  }

  while (start_index >= 0) {
    QRegularExpressionMatch end_match =
        multi_line_comment_end_.match(text, start_index + 2);
    int end_index = end_match.hasMatch() ? end_match.capturedStart() : -1;
    int comment_length;

    if (end_index == -1) {
      setCurrentBlockState(1);
      comment_length = text.length() - start_index;
    } else {
      comment_length = end_index - start_index + end_match.capturedLength();
    }

    setFormat(start_index, comment_length, multi_line_comment_format_);

    QRegularExpressionMatch next_start =
        multi_line_comment_start_.match(text, start_index + comment_length);
    start_index = next_start.hasMatch() ? next_start.capturedStart() : -1;
  }
}

}  // namespace studio
}  // namespace goods_db
