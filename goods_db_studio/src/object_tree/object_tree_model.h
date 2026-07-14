#pragma once

#include <QAbstractItemModel>
#include <QString>
#include <memory>
#include <vector>

namespace goods_db {
namespace studio {

// =============================================================================
// ObjectTreeModel — tree model for database schema browser
//
// Tree structure:
//   Server (root)
//     └─ Database
//          ├─ Tables (folder)
//          │   └─ Table
//          │       └─ Columns (folder)
//          │           └─ Column (name : type)
//          └─ Indexes (folder)
// =============================================================================

struct TreeNode {
  enum Type {
    SERVER,
    DATABASE,
    TABLES_FOLDER,
    TABLE,
    COLUMNS_FOLDER,
    COLUMN,
    INDEXES_FOLDER,
    INDEX_ITEM,
  };

  Type type;
  QString name;
  QString extra;  // column type, index type, etc.
  std::vector<std::unique_ptr<TreeNode>> children;
  TreeNode* parent = nullptr;
};

class ObjectTreeModel : public QAbstractItemModel {
  Q_OBJECT

 public:
  explicit ObjectTreeModel(QObject* parent = nullptr);

  void SetConnectionName(const QString& name);
  void Refresh();

  void AddDatabase(const QString& name);
  void AddTable(const QString& db, const QString& table);
  void AddColumn(const QString& db, const QString& table,
                 const QString& col_name, const QString& col_type);
  void Clear();

  // QAbstractItemModel interface
  QModelIndex index(int row, int col,
                    const QModelIndex& parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex& index) const override;
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

  // Internal helpers
  QModelIndex CreateIndexForNode(TreeNode* node);
  TreeNode* GetNode(const QModelIndex& index) const;
  QString GetFullTableName(const QModelIndex& index) const;
  QString GetDatabaseName(const QModelIndex& index) const;

 private:
  std::unique_ptr<TreeNode> root_;
  QString connection_name_;

  TreeNode* FindDatabaseNode(const QString& db) const;
  TreeNode* FindTablesFolder(const QString& db) const;
};

}  // namespace studio
}  // namespace goods_db
