#pragma once

#include <QWidget>
#include <QTreeView>
#include <QToolBar>

namespace goods_db {
namespace studio {

class ObjectTreeModel;

// =============================================================================
// ObjectTreeWidget — database schema browser
//
// QTreeView displaying: Server → Database → Tables → Table → Columns → Column
// Supports:
//   - Double-click table → emit TableDoubleClicked(db, table)
//   - Right-click context menu (Open, View Structure, Copy Name, CRUD template)
//   - Refresh button to reload schema
// =============================================================================
class ObjectTreeWidget : public QWidget {
  Q_OBJECT

 public:
  explicit ObjectTreeWidget(QWidget* parent = nullptr);

  void SetConnection(const QString& name);
  void Refresh();

 signals:
  void TableDoubleClicked(const QString& db, const QString& table);
  void TableSelected(const QString& db, const QString& table);
  void RefreshRequested();

 private slots:
  void OnDoubleClicked(const QModelIndex& index);
  void OnSelectionChanged();
  void OnCustomContextMenu(const QPoint& pos);

 private:
  QTreeView* tree_view_;
  ObjectTreeModel* model_;
  QToolBar* toolbar_;

  void SetupToolbar();
  void SetupContextMenu();
};

}  // namespace studio
}  // namespace goods_db
