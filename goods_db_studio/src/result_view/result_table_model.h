#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

// =============================================================================
// ResultTableModel — QAbstractTableModel for query result sets
//
// Special formatting:
//   - NULL values → gray italic "(NULL)"
//   - Numeric types → right-aligned
//   - String types → left-aligned
// =============================================================================
class ResultTableModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  explicit ResultTableModel(QObject* parent = nullptr);

  void SetResult(const QueryResult& result);
  void Clear();

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

  const QueryResult& GetResult() const { return result_; }
  bool IsEmpty() const { return result_.columns.empty(); }

 private:
  QueryResult result_;

  bool IsNumericType(const QString& type_name) const;
};

}  // namespace studio
}  // namespace goods_db
