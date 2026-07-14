#include "object_tree/object_tree_widget.h"
#include "object_tree/object_tree_model.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QMenu>
#include <QVBoxLayout>

namespace goods_db {
namespace studio {

ObjectTreeWidget::ObjectTreeWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Toolbar
  toolbar_ = new QToolBar(this);
  SetupToolbar();
  layout->addWidget(toolbar_);

  // Tree view
  tree_view_ = new QTreeView(this);
  model_ = new ObjectTreeModel(this);
  tree_view_->setModel(model_);
  tree_view_->setHeaderHidden(false);
  tree_view_->setAlternatingRowColors(true);
  tree_view_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_view_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_view_->setAnimated(true);
  tree_view_->setIndentation(20);
  tree_view_->header()->setStretchLastSection(true);
  tree_view_->header()->resizeSection(0, 180);
  layout->addWidget(tree_view_, 1);

  // Connections
  connect(tree_view_, &QTreeView::doubleClicked,
          this, &ObjectTreeWidget::OnDoubleClicked);
  connect(tree_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &ObjectTreeWidget::OnSelectionChanged);
  connect(tree_view_, &QTreeView::customContextMenuRequested,
          this, &ObjectTreeWidget::OnCustomContextMenu);

  // Right-click menu
  SetupContextMenu();

  // Add demo data for testing UI
  model_->AddDatabase("warehouse_db");
  model_->AddTable("warehouse_db", "warehouses");
  model_->AddColumn("warehouse_db", "warehouses", "id", "INT");
  model_->AddColumn("warehouse_db", "warehouses", "name", "VARCHAR(100)");
  model_->AddColumn("warehouse_db", "warehouses", "location", "VARCHAR(255)");
  model_->AddColumn("warehouse_db", "warehouses", "capacity", "INT");
  model_->AddTable("warehouse_db", "products");
  model_->AddColumn("warehouse_db", "products", "id", "INT");
  model_->AddColumn("warehouse_db", "products", "name", "VARCHAR(200)");
  model_->AddColumn("warehouse_db", "products", "sku", "VARCHAR(50)");
  model_->AddColumn("warehouse_db", "products", "unit_price", "DECIMAL(10,2)");
  model_->AddTable("warehouse_db", "inventory");
  model_->AddColumn("warehouse_db", "inventory", "warehouse_id", "INT");
  model_->AddColumn("warehouse_db", "inventory", "product_id", "INT");
  model_->AddColumn("warehouse_db", "inventory", "quantity", "INT");
  model_->AddTable("warehouse_db", "orders");
  model_->AddColumn("warehouse_db", "orders", "id", "INT");
  model_->AddColumn("warehouse_db", "orders", "customer_id", "INT");
  model_->AddColumn("warehouse_db", "orders", "order_date", "TIMESTAMP");
  model_->AddColumn("warehouse_db", "orders", "status", "VARCHAR(50)");
  model_->AddColumn("warehouse_db", "orders", "total_amount", "DECIMAL(12,2)");

  tree_view_->expandAll();
}

void ObjectTreeWidget::SetConnection(const QString& name) {
  model_->SetConnectionName(name);
  tree_view_->expandAll();
}

void ObjectTreeWidget::Refresh() {
  model_->Refresh();
  emit RefreshRequested();
}

void ObjectTreeWidget::SetupToolbar() {
  QAction* refresh_action = toolbar_->addAction(tr("Refresh"));
  connect(refresh_action, &QAction::triggered, this, [this]() { Refresh(); });
  QAction* collapse_action = toolbar_->addAction(tr("Collapse All"));
  connect(collapse_action, &QAction::triggered, this, [this]() {
    tree_view_->collapseAll();
  });
}

void ObjectTreeWidget::SetupContextMenu() {
  // The context menu is created on-demand in OnCustomContextMenu
}

void ObjectTreeWidget::OnDoubleClicked(const QModelIndex& index) {
  TreeNode* node = model_->GetNode(index);
  if (!node) return;

  if (node->type == TreeNode::TABLE) {
    QString db = model_->GetDatabaseName(index);
    QString table = node->name;
    emit TableDoubleClicked(db, table);
  }
}

void ObjectTreeWidget::OnSelectionChanged() {
  QModelIndexList selected = tree_view_->selectionModel()->selectedIndexes();
  if (selected.isEmpty()) return;

  TreeNode* node = model_->GetNode(selected.first());
  if (!node) return;

  if (node->type == TreeNode::TABLE) {
    QString db = model_->GetDatabaseName(selected.first());
    emit TableSelected(db, node->name);
  }
}

void ObjectTreeWidget::OnCustomContextMenu(const QPoint& pos) {
  QModelIndex index = tree_view_->indexAt(pos);
  TreeNode* node = model_->GetNode(index);

  QMenu menu(this);

  if (node && node->type == TreeNode::TABLE) {
    QString db = model_->GetDatabaseName(index);
    QString table = node->name;

    menu.addAction(tr("Open Table"), [this, db, table]() {
      emit TableDoubleClicked(db, table);
    });

    menu.addAction(tr("View Structure"), [this, db, table]() {
      emit TableSelected(db, table);
    });

    menu.addSeparator();

    menu.addAction(tr("Copy Table Name"), [table]() {
      QApplication::clipboard()->setText(table);
    });

    menu.addAction(tr("Copy Full Name"), [db, table]() {
      QApplication::clipboard()->setText(db + "." + table);
    });

    menu.addSeparator();

    QMenu* crud_menu = menu.addMenu(tr("Generate CRUD"));
    crud_menu->addAction(tr("SELECT Template"), [db, table]() {
      QApplication::clipboard()->setText(
          QString("SELECT * FROM %1.%2 WHERE id = ?;").arg(db).arg(table));
    });
    crud_menu->addAction(tr("INSERT Template"), [db, table]() {
      QApplication::clipboard()->setText(
          QString("INSERT INTO %1.%2 (...) VALUES (...);").arg(db).arg(table));
    });
    crud_menu->addAction(tr("UPDATE Template"), [db, table]() {
      QApplication::clipboard()->setText(
          QString("UPDATE %1.%2 SET ... WHERE id = ?;").arg(db).arg(table));
    });
    crud_menu->addAction(tr("DELETE Template"), [db, table]() {
      QApplication::clipboard()->setText(
          QString("DELETE FROM %1.%2 WHERE id = ?;").arg(db).arg(table));
    });
  } else if (node && node->type == TreeNode::DATABASE) {
    QAction* ref_action = menu.addAction(tr("Refresh Database"));
    connect(ref_action, &QAction::triggered, this, [this]() { Refresh(); });
  } else {
    QAction* ref_action = menu.addAction(tr("Refresh All"));
    connect(ref_action, &QAction::triggered, this, [this]() { Refresh(); });
  }

  menu.exec(tree_view_->viewport()->mapToGlobal(pos));
}

}  // namespace studio
}  // namespace goods_db
