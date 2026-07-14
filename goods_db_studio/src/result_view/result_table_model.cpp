#include "result_view/result_table_model.h"

#include <QFont>
#include <QBrush>

namespace goods_db {
namespace studio {

ResultTableModel::ResultTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void ResultTableModel::SetResult(const QueryResult& result) {
  beginResetModel();
  result_ = result;
  endResetModel();
}

void ResultTableModel::Clear() {
  beginResetModel();
  result_ = QueryResult();
  endResetModel();
}

int ResultTableModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(result_.rows.size());
}

int ResultTableModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(result_.columns.size());
}

QVariant ResultTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return QVariant();

  int row = index.row();
  int col = index.column();

  if (role == Qt::DisplayRole) {
    if (row < static_cast<int>(result_.rows.size()) &&
        col < static_cast<int>(result_.rows[row].size())) {
      const QString& val = result_.rows[row][col];
      if (val.isEmpty()) {
        return QString("(NULL)");
      }
      return val;
    }
    return QString("(NULL)");
  }

  if (role == Qt::FontRole) {
    if (row < static_cast<int>(result_.rows.size()) &&
        col < static_cast<int>(result_.rows[row].size())) {
      if (result_.rows[row][col].isEmpty()) {
        QFont font;
        font.setItalic(true);
        return font;
      }
    }
  }

  if (role == Qt::ForegroundRole) {
    if (row < static_cast<int>(result_.rows.size()) &&
        col < static_cast<int>(result_.rows[row].size())) {
      if (result_.rows[row][col].isEmpty()) {
        return QBrush(QColor("#808080"));
      }
    }
  }

  if (role == Qt::TextAlignmentRole) {
    if (col < static_cast<int>(result_.columns.size())) {
      if (IsNumericType(result_.columns[col].type_name)) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
      }
    }
    return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
  }

  if (role == Qt::ToolTipRole) {
    if (row < static_cast<int>(result_.rows.size()) &&
        col < static_cast<int>(result_.rows[row].size())) {
      const QString& val = result_.rows[row][col];
      if (val.length() > 100) {
        return val;  // Show full value in tooltip for long strings
      }
    }
  }

  return QVariant();
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const {
  if (role == Qt::DisplayRole) {
    if (orientation == Qt::Horizontal) {
      if (section < static_cast<int>(result_.columns.size())) {
        return result_.columns[section].name;
      }
    } else {
      return section + 1;  // Row number
    }
  }

  if (role == Qt::FontRole && orientation == Qt::Horizontal) {
    QFont font;
    font.setBold(true);
    return font;
  }

  return QVariant();
}

bool ResultTableModel::IsNumericType(const QString& type_name) const {
  const QString tn = type_name.toUpper();
  return tn == "INT" || tn == "INTEGER" || tn == "BIGINT" ||
         tn == "SMALLINT" || tn == "TINYINT" || tn == "DECIMAL" ||
         tn == "NUMERIC" || tn == "DOUBLE" || tn == "FLOAT" ||
         tn == "REAL" || tn == "BIT";
}

}  // namespace studio
}  // namespace goods_db
