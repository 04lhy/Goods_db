#include "object_tree/object_tree_model.h"

#include <QIcon>

namespace goods_db {
namespace studio {

ObjectTreeModel::ObjectTreeModel(QObject* parent) : QAbstractItemModel(parent) {
  root_ = std::make_unique<TreeNode>();
  root_->type = TreeNode::SERVER;
  root_->name = "Disconnected";
}

void ObjectTreeModel::SetConnectionName(const QString& name) {
  beginResetModel();
  root_->name = name;
  root_->children.clear();
  endResetModel();
}

void ObjectTreeModel::Refresh() {
  beginResetModel();
  root_->children.clear();
  endResetModel();
}

// ---- Helper: create a QModelIndex for a given node -------------------------

QModelIndex ObjectTreeModel::CreateIndexForNode(TreeNode* node) {
  if (!node || !node->parent) return QModelIndex();
  TreeNode* p = node->parent;
  for (size_t i = 0; i < p->children.size(); i++) {
    if (p->children[i].get() == node) {
      if (p->type == TreeNode::SERVER) return QModelIndex();
      return createIndex(static_cast<int>(i), 0, node);
    }
  }
  return QModelIndex();
}

// ---- Data methods -----------------------------------------------------------

void ObjectTreeModel::AddDatabase(const QString& name) {
  for (auto& child : root_->children) {
    if (child->name == name) return;
  }

  int row = static_cast<int>(root_->children.size());
  beginInsertRows(QModelIndex(), row, row);

  auto db = std::make_unique<TreeNode>();
  db->type = TreeNode::DATABASE;
  db->name = name;
  db->parent = root_.get();

  auto tables = std::make_unique<TreeNode>();
  tables->type = TreeNode::TABLES_FOLDER;
  tables->name = "Tables";
  tables->parent = db.get();
  db->children.push_back(std::move(tables));

  auto indexes = std::make_unique<TreeNode>();
  indexes->type = TreeNode::INDEXES_FOLDER;
  indexes->name = "Indexes";
  indexes->parent = db.get();
  db->children.push_back(std::move(indexes));

  root_->children.push_back(std::move(db));
  endInsertRows();
}

void ObjectTreeModel::AddTable(const QString& db, const QString& table) {
  TreeNode* tables_folder = FindTablesFolder(db);
  if (!tables_folder) return;

  for (auto& child : tables_folder->children) {
    if (child->name == table) return;
  }

  // Build the QModelIndex for tables_folder
  QModelIndex parent_idx = CreateIndexForNode(tables_folder);
  int row = static_cast<int>(tables_folder->children.size());
  beginInsertRows(parent_idx, row, row);

  auto tbl = std::make_unique<TreeNode>();
  tbl->type = TreeNode::TABLE;
  tbl->name = table;
  tbl->parent = tables_folder;

  auto cols = std::make_unique<TreeNode>();
  cols->type = TreeNode::COLUMNS_FOLDER;
  cols->name = "Columns";
  cols->parent = tbl.get();
  tbl->children.push_back(std::move(cols));

  tables_folder->children.push_back(std::move(tbl));
  endInsertRows();
}

void ObjectTreeModel::AddColumn(const QString& db, const QString& table,
                                 const QString& col_name,
                                 const QString& col_type) {
  TreeNode* tables_folder = FindTablesFolder(db);
  if (!tables_folder) return;

  TreeNode* tbl = nullptr;
  for (auto& child : tables_folder->children) {
    if (child->name == table) {
      tbl = child.get();
      break;
    }
  }
  if (!tbl || tbl->children.empty()) return;

  TreeNode* cols_folder = tbl->children[0].get();
  QModelIndex parent_idx = CreateIndexForNode(cols_folder);
  int row = static_cast<int>(cols_folder->children.size());
  beginInsertRows(parent_idx, row, row);

  auto col = std::make_unique<TreeNode>();
  col->type = TreeNode::COLUMN;
  col->name = col_name;
  col->extra = col_type;
  col->parent = cols_folder;

  cols_folder->children.push_back(std::move(col));
  endInsertRows();
}

void ObjectTreeModel::Clear() {
  beginResetModel();
  root_->children.clear();
  endResetModel();
}

// ---- QAbstractItemModel -----------------------------------------------------

QModelIndex ObjectTreeModel::index(int row, int col,
                                    const QModelIndex& parent) const {
  if (!hasIndex(row, col, parent)) return QModelIndex();

  TreeNode* parent_node = parent.isValid()
                              ? static_cast<TreeNode*>(parent.internalPointer())
                              : root_.get();

  if (row >= 0 && row < static_cast<int>(parent_node->children.size())) {
    return createIndex(row, col, parent_node->children[row].get());
  }
  return QModelIndex();
}

QModelIndex ObjectTreeModel::parent(const QModelIndex& index) const {
  if (!index.isValid()) return QModelIndex();

  TreeNode* node = static_cast<TreeNode*>(index.internalPointer());
  TreeNode* parent_node = node->parent;

  if (!parent_node || parent_node == root_.get()) return QModelIndex();

  TreeNode* grandparent = parent_node->parent;
  if (!grandparent) return QModelIndex();

  for (int i = 0; i < static_cast<int>(grandparent->children.size()); i++) {
    if (grandparent->children[i].get() == parent_node) {
      return createIndex(i, 0, parent_node);
    }
  }
  return QModelIndex();
}

int ObjectTreeModel::rowCount(const QModelIndex& parent) const {
  if (parent.column() > 0) return 0;
  TreeNode* node = parent.isValid()
                       ? static_cast<TreeNode*>(parent.internalPointer())
                       : root_.get();
  return static_cast<int>(node->children.size());
}

int ObjectTreeModel::columnCount(const QModelIndex&) const {
  return 2;
}

QVariant ObjectTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return QVariant();

  TreeNode* node = static_cast<TreeNode*>(index.internalPointer());
  if (role == Qt::DisplayRole) {
    if (index.column() == 0) return node->name;
    if (index.column() == 1) return node->extra;
  }
  if (role == Qt::DecorationRole && index.column() == 0) {
    switch (node->type) {
      case TreeNode::SERVER:       return QIcon(":/icons/connect.svg");
      case TreeNode::DATABASE:     return QIcon(":/icons/database.svg");
      case TreeNode::TABLES_FOLDER: return QIcon(":/icons/folder.svg");
      case TreeNode::TABLE:        return QIcon(":/icons/table.svg");
      case TreeNode::COLUMNS_FOLDER: return QIcon(":/icons/folder.svg");
      case TreeNode::COLUMN:       return QIcon(":/icons/column.svg");
      case TreeNode::INDEXES_FOLDER: return QIcon(":/icons/folder.svg");
      case TreeNode::INDEX_ITEM:   return QIcon(":/icons/column.svg");
    }
  }
  return QVariant();
}

QVariant ObjectTreeModel::headerData(int section, Qt::Orientation orientation,
                                      int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    return section == 0 ? tr("Name") : tr("Type");
  }
  return QVariant();
}

// ---- Node helpers -----------------------------------------------------------

TreeNode* ObjectTreeModel::GetNode(const QModelIndex& index) const {
  if (!index.isValid()) return root_.get();
  return static_cast<TreeNode*>(index.internalPointer());
}

QString ObjectTreeModel::GetFullTableName(const QModelIndex& index) const {
  TreeNode* node = GetNode(index);
  if (!node) return QString();
  QString db, table;
  TreeNode* current = node;
  while (current) {
    if (current->type == TreeNode::TABLE) table = current->name;
    else if (current->type == TreeNode::DATABASE) db = current->name;
    current = current->parent;
  }
  if (!db.isEmpty() && !table.isEmpty()) return db + "." + table;
  return node->name;
}

QString ObjectTreeModel::GetDatabaseName(const QModelIndex& index) const {
  TreeNode* node = GetNode(index);
  while (node) {
    if (node->type == TreeNode::DATABASE) return node->name;
    node = node->parent;
  }
  return QString();
}

// ---- Private helpers --------------------------------------------------------

TreeNode* ObjectTreeModel::FindDatabaseNode(const QString& db) const {
  for (auto& child : root_->children) {
    if (child->name == db && child->type == TreeNode::DATABASE) {
      return child.get();
    }
  }
  return nullptr;
}

TreeNode* ObjectTreeModel::FindTablesFolder(const QString& db) const {
  TreeNode* db_node = FindDatabaseNode(db);
  if (!db_node) return nullptr;
  for (auto& child : db_node->children) {
    if (child->type == TreeNode::TABLES_FOLDER) return child.get();
  }
  return nullptr;
}

}  // namespace studio
}  // namespace goods_db
