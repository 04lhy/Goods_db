#pragma once

#include <QWidget>
#include <QTableView>
#include <QStandardItemModel>
#include <QTreeWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMap>

#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

class ConnectionPool;

// =============================================================================
// UserManager — user account and permission management
//
// Features:
//   - User list: QTableView with columns User / Host / Created
//   - New user / Delete user / Change password dialogs
//   - Permission editor: left QTreeWidget (DB → Table hierarchy)
//                        + right QCheckBox matrix (SELECT/INSERT/UPDATE/DELETE/
//                          CREATE/DROP/INDEX/GRANT)
//   - "Apply Permissions" button sends GRANT/REVOKE SQL
// =============================================================================
class UserManager : public QWidget {
  Q_OBJECT

 public:
  explicit UserManager(ConnectionPool* pool, QWidget* parent = nullptr);

 public slots:
  void Refresh();

 private slots:
  void OnNewUser();
  void OnDeleteUser();
  void OnChangePassword();
  void OnUserSelected(const QModelIndex& index);
  void OnLoadPermissions();
  void OnApplyPermissions();
  void OnDbTableSelected(QTreeWidgetItem* item, int column);

 private:
  void SetupUi();
  QWidget* CreateUserListPanel();
  QWidget* CreatePermissionPanel();

  QueryResult ExecuteSql(const QString& sql);

  ConnectionPool* pool_;

  // ---- User list ----
  QTableView* user_table_view_;
  QStandardItemModel* user_model_;
  QPushButton* new_user_btn_;
  QPushButton* delete_user_btn_;
  QPushButton* change_pwd_btn_;
  QPushButton* refresh_btn_;

  // ---- Permission editor ----
  QTreeWidget* perm_db_tree_;
  QMap<QString, QCheckBox*> perm_checkboxes_;
  QPushButton* load_perm_btn_;
  QPushButton* apply_perm_btn_;
  QLabel* current_user_label_;

  // Currently selected user for permissions
  QString current_user_;
  QString current_host_;
};

// =============================================================================
// NewUserDialog — create a new user account
// =============================================================================
class NewUserDialog : public QDialog {
  Q_OBJECT
 public:
  explicit NewUserDialog(QWidget* parent = nullptr);

  QString GetUsername() const { return username_edit_->text().trimmed(); }
  QString GetPassword() const { return password_edit_->text(); }
  QString GetHost() const { return host_edit_->text().trimmed(); }

 private:
  QLineEdit* username_edit_;
  QLineEdit* password_edit_;
  QLineEdit* confirm_edit_;
  QLineEdit* host_edit_;
};

// =============================================================================
// ChangePasswordDialog — change a user's password
// =============================================================================
class ChangePasswordDialog : public QDialog {
  Q_OBJECT
 public:
  explicit ChangePasswordDialog(const QString& username, const QString& host,
                                QWidget* parent = nullptr);

  QString GetNewPassword() const { return new_password_edit_->text(); }

 private:
  QLineEdit* new_password_edit_;
  QLineEdit* confirm_edit_;
};

}  // namespace studio
}  // namespace goods_db
