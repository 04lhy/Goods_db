#include "ui/user_manager.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QIcon>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFormLayout>

#include "connection/connection_pool.h"

namespace goods_db {
namespace studio {

// =============================================================================
// NewUserDialog
// =============================================================================

NewUserDialog::NewUserDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("New User"));
  setMinimumWidth(380);

  auto* layout = new QFormLayout(this);

  username_edit_ = new QLineEdit(this);
  username_edit_->setPlaceholderText(tr("e.g. app_user"));
  layout->addRow(tr("Username:"), username_edit_);

  password_edit_ = new QLineEdit(this);
  password_edit_->setEchoMode(QLineEdit::Password);
  password_edit_->setPlaceholderText(tr("Enter password"));
  layout->addRow(tr("Password:"), password_edit_);

  confirm_edit_ = new QLineEdit(this);
  confirm_edit_->setEchoMode(QLineEdit::Password);
  confirm_edit_->setPlaceholderText(tr("Confirm password"));
  layout->addRow(tr("Confirm:"), confirm_edit_);

  host_edit_ = new QLineEdit(this);
  host_edit_->setText("%");
  host_edit_->setPlaceholderText(tr("% (any host)"));
  layout->addRow(tr("Host:"), host_edit_);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    if (username_edit_->text().trimmed().isEmpty()) {
      QMessageBox::warning(this, tr("Validation Error"),
                           tr("Username cannot be empty."));
      return;
    }
    if (password_edit_->text().isEmpty()) {
      QMessageBox::warning(this, tr("Validation Error"),
                           tr("Password cannot be empty."));
      return;
    }
    if (password_edit_->text() != confirm_edit_->text()) {
      QMessageBox::warning(this, tr("Validation Error"),
                           tr("Passwords do not match."));
      return;
    }
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addRow(buttons);
}

// =============================================================================
// ChangePasswordDialog
// =============================================================================

ChangePasswordDialog::ChangePasswordDialog(const QString& username,
                                           const QString& host,
                                           QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Change Password"));
  setMinimumWidth(380);

  auto* layout = new QFormLayout(this);

  layout->addRow(tr("User:"), new QLabel(
      QString("%1@%2").arg(username, host), this));

  new_password_edit_ = new QLineEdit(this);
  new_password_edit_->setEchoMode(QLineEdit::Password);
  new_password_edit_->setPlaceholderText(tr("Enter new password"));
  layout->addRow(tr("New Password:"), new_password_edit_);

  confirm_edit_ = new QLineEdit(this);
  confirm_edit_->setEchoMode(QLineEdit::Password);
  confirm_edit_->setPlaceholderText(tr("Confirm new password"));
  layout->addRow(tr("Confirm:"), confirm_edit_);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    if (new_password_edit_->text().isEmpty()) {
      QMessageBox::warning(this, tr("Validation Error"),
                           tr("Password cannot be empty."));
      return;
    }
    if (new_password_edit_->text() != confirm_edit_->text()) {
      QMessageBox::warning(this, tr("Validation Error"),
                           tr("Passwords do not match."));
      return;
    }
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addRow(buttons);
}

// =============================================================================
// UserManager
// =============================================================================

UserManager::UserManager(ConnectionPool* pool, QWidget* parent)
    : QWidget(parent), pool_(pool) {
  SetupUi();
}

void UserManager::SetupUi() {
  auto* main_layout = new QHBoxLayout(this);
  main_layout->setContentsMargins(8, 8, 8, 8);
  main_layout->setSpacing(8);

  auto* splitter = new QSplitter(Qt::Horizontal, this);

  splitter->addWidget(CreateUserListPanel());
  splitter->addWidget(CreatePermissionPanel());

  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 3);
  splitter->setSizes({300, 500});

  main_layout->addWidget(splitter);
}

QWidget* UserManager::CreateUserListPanel() {
  auto* panel = new QWidget(this);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* group = new QGroupBox(tr("Users"), panel);
  auto* group_layout = new QVBoxLayout(group);

  // User table
  user_model_ = new QStandardItemModel(0, 3, group);
  user_model_->setHorizontalHeaderLabels({tr("User"), tr("Host"), tr("Created")});

  user_table_view_ = new QTableView(group);
  user_table_view_->setModel(user_model_);
  user_table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  user_table_view_->setSelectionMode(QAbstractItemView::SingleSelection);
  user_table_view_->setAlternatingRowColors(true);
  user_table_view_->verticalHeader()->setVisible(false);
  user_table_view_->horizontalHeader()->setStretchLastSection(true);
  user_table_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  user_table_view_->setColumnWidth(0, 100);
  user_table_view_->setColumnWidth(1, 80);
  connect(user_table_view_->selectionModel(), &QItemSelectionModel::currentRowChanged,
          this, &UserManager::OnUserSelected);

  group_layout->addWidget(user_table_view_);

  // Action buttons
  auto* btn_layout = new QHBoxLayout();
  new_user_btn_ = new QPushButton(tr("New User"), group);
  new_user_btn_->setIcon(QIcon(":/icons/user_add.svg"));
  connect(new_user_btn_, &QPushButton::clicked, this, &UserManager::OnNewUser);
  btn_layout->addWidget(new_user_btn_);

  delete_user_btn_ = new QPushButton(tr("Delete"), group);
  delete_user_btn_->setIcon(QIcon(":/icons/user_delete.svg"));
  delete_user_btn_->setStyleSheet("QPushButton { color: #e74c3c; }");
  connect(delete_user_btn_, &QPushButton::clicked, this, &UserManager::OnDeleteUser);
  btn_layout->addWidget(delete_user_btn_);

  change_pwd_btn_ = new QPushButton(tr("Change Password"), group);
  change_pwd_btn_->setIcon(QIcon(":/icons/password.svg"));
  connect(change_pwd_btn_, &QPushButton::clicked, this, &UserManager::OnChangePassword);
  btn_layout->addWidget(change_pwd_btn_);

  btn_layout->addStretch();
  group_layout->addLayout(btn_layout);

  layout->addWidget(group);

  // Refresh button at bottom
  auto* bottom_layout = new QHBoxLayout();
  bottom_layout->addStretch();
  refresh_btn_ = new QPushButton(tr("Refresh"), panel);
  refresh_btn_->setIcon(QIcon(":/icons/refresh.svg"));
  connect(refresh_btn_, &QPushButton::clicked, this, &UserManager::Refresh);
  bottom_layout->addWidget(refresh_btn_);
  layout->addLayout(bottom_layout);

  return panel;
}

QWidget* UserManager::CreatePermissionPanel() {
  auto* panel = new QWidget(this);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* group = new QGroupBox(tr("Permissions"), panel);
  auto* group_layout = new QVBoxLayout(group);

  // Current user label
  current_user_label_ = new QLabel(
      tr("Select a user from the list to edit permissions."), group);
  current_user_label_->setWordWrap(true);
  current_user_label_->setStyleSheet("font-weight: bold; padding: 4px;");
  group_layout->addWidget(current_user_label_);

  // DB/Table tree
  auto* tree_label = new QLabel(tr("Database / Table:"), group);
  group_layout->addWidget(tree_label);

  perm_db_tree_ = new QTreeWidget(group);
  perm_db_tree_->setHeaderLabels({tr("Object"), tr("Type")});
  perm_db_tree_->setColumnWidth(0, 200);
  perm_db_tree_->setColumnWidth(1, 80);
  connect(perm_db_tree_, &QTreeWidget::currentItemChanged,
          this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            OnDbTableSelected(current, 0);
          });
  group_layout->addWidget(perm_db_tree_);

  // Permission checkboxes
  auto* perm_group = new QGroupBox(tr("Grants"), group);
  auto* perm_layout = new QGridLayout(perm_group);
  perm_layout->setSpacing(6);

  QStringList privileges = {
      "SELECT", "INSERT", "UPDATE", "DELETE",
      "CREATE", "DROP", "INDEX", "GRANT"
  };

  QStringList descriptions = {
      tr("Read data"),
      tr("Insert rows"),
      tr("Update rows"),
      tr("Delete rows"),
      tr("Create tables"),
      tr("Drop tables"),
      tr("Create/drop indexes"),
      tr("Grant privileges to others"),
  };

  for (int i = 0; i < privileges.size(); i++) {
    auto* cb = new QCheckBox(privileges[i], perm_group);
    cb->setToolTip(descriptions[i]);
    perm_checkboxes_[privileges[i]] = cb;

    int row = i / 4;
    int col = i % 4;
    perm_layout->addWidget(cb, row, col);
  }

  group_layout->addWidget(perm_group);

  // Apply / Load buttons
  auto* action_layout = new QHBoxLayout();
  load_perm_btn_ = new QPushButton(tr("Load Permissions"), group);
  load_perm_btn_->setIcon(QIcon(":/icons/permissions.svg"));
  connect(load_perm_btn_, &QPushButton::clicked, this, &UserManager::OnLoadPermissions);
  action_layout->addWidget(load_perm_btn_);

  action_layout->addStretch();

  apply_perm_btn_ = new QPushButton(tr("Apply Permissions"), group);
  apply_perm_btn_->setIcon(QIcon(":/icons/permissions.svg"));
  apply_perm_btn_->setStyleSheet(
      "QPushButton { font-weight: bold; background-color: #2980b9; color: white; padding: 6px 16px; }");
  connect(apply_perm_btn_, &QPushButton::clicked, this, &UserManager::OnApplyPermissions);
  action_layout->addWidget(apply_perm_btn_);

  group_layout->addLayout(action_layout);

  layout->addWidget(group);
  return panel;
}

// =============================================================================
// Helpers
// =============================================================================

QueryResult UserManager::ExecuteSql(const QString& sql) {
  auto* conn = pool_->GetActiveConnection();
  if (!conn || !conn->IsConnected()) {
    QueryResult err;
    err.is_error = true;
    err.error_message = tr("No active connection");
    return err;
  }
  return conn->Execute(sql);
}

// =============================================================================
// Public Slots
// =============================================================================

void UserManager::Refresh() {
  user_model_->removeRows(0, user_model_->rowCount());

  // Query users from mysql.user equivalent
  QueryResult result = ExecuteSql("SELECT user, host, created FROM mysql.user ORDER BY user");
  if (result.is_error) {
    // Try alternative: SHOW USERS (custom command)
    result = ExecuteSql("SHOW USERS");
  }

  if (!result.is_error) {
    for (const auto& row : result.rows) {
      QList<QStandardItem*> items;
      for (const auto& cell : row) {
        items.append(new QStandardItem(cell));
      }
      // Pad to 3 columns
      while (items.size() < 3) items.append(new QStandardItem(""));
      user_model_->appendRow(items);
    }
  }
}

// =============================================================================
// Private Slots
// =============================================================================

void UserManager::OnNewUser() {
  NewUserDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted) {
    QString username = dialog.GetUsername();
    QString password = dialog.GetPassword();
    QString host = dialog.GetHost();

    if (host.isEmpty()) host = "%";

    QueryResult r = ExecuteSql(
        QString("CREATE USER '%1'@'%2' IDENTIFIED BY '%3'")
            .arg(username, host, password));

    if (r.is_error) {
      QMessageBox::critical(this, tr("Error"),
                            tr("Failed to create user:\n%1").arg(r.error_message));
    } else {
      QMessageBox::information(this, tr("Success"),
                               tr("User '%1'@'%2' created successfully.").arg(username, host));
      Refresh();
    }
  }
}

void UserManager::OnDeleteUser() {
  auto selection = user_table_view_->selectionModel()->selectedRows();
  if (selection.isEmpty()) {
    QMessageBox::information(this, tr("No Selection"),
                             tr("Please select a user to delete."));
    return;
  }

  int row = selection.first().row();
  QString username = user_model_->item(row, 0)->text();
  QString host = user_model_->item(row, 1)->text();
  if (host.isEmpty()) host = "%";

  QMessageBox::StandardButton reply = QMessageBox::warning(
      this, tr("Confirm Delete"),
      tr("Are you sure you want to delete user '%1'@'%2'?\n\n"
         "This action cannot be undone.").arg(username, host),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (reply != QMessageBox::Yes) return;

  QueryResult r = ExecuteSql(
      QString("DROP USER '%1'@'%2'").arg(username, host));

  if (r.is_error) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to delete user:\n%1").arg(r.error_message));
  } else {
    QMessageBox::information(this, tr("Success"),
                             tr("User '%1'@'%2' deleted.").arg(username, host));
    Refresh();
  }
}

void UserManager::OnChangePassword() {
  auto selection = user_table_view_->selectionModel()->selectedRows();
  if (selection.isEmpty()) {
    QMessageBox::information(this, tr("No Selection"),
                             tr("Please select a user first."));
    return;
  }

  int row = selection.first().row();
  QString username = user_model_->item(row, 0)->text();
  QString host = user_model_->item(row, 1)->text();
  if (host.isEmpty()) host = "%";

  ChangePasswordDialog dialog(username, host, this);
  if (dialog.exec() == QDialog::Accepted) {
    QString new_password = dialog.GetNewPassword();

    QueryResult r = ExecuteSql(
        QString("ALTER USER '%1'@'%2' IDENTIFIED BY '%3'")
            .arg(username, host, new_password));

    if (r.is_error) {
      // Try SET PASSWORD as fallback
      r = ExecuteSql(
          QString("SET PASSWORD FOR '%1'@'%2' = PASSWORD('%3')")
              .arg(username, host, new_password));
    }

    if (r.is_error) {
      QMessageBox::critical(this, tr("Error"),
                            tr("Failed to change password:\n%1").arg(r.error_message));
    } else {
      QMessageBox::information(this, tr("Success"),
                               tr("Password changed successfully."));
    }
  }
}

void UserManager::OnUserSelected(const QModelIndex& index) {
  if (!index.isValid()) return;

  int row = index.row();
  current_user_ = user_model_->item(row, 0)->text();
  current_host_ = user_model_->item(row, 1)->text();
  if (current_host_.isEmpty()) current_host_ = "%";

  current_user_label_->setText(
      tr("Editing permissions for: %1@%2").arg(current_user_, current_host_));

  // Refresh DB tree
  perm_db_tree_->clear();

  QueryResult db_result = ExecuteSql("SHOW DATABASES");
  if (!db_result.is_error) {
    for (const auto& row : db_result.rows) {
      if (row.empty()) continue;
      QString db_name = row[0].trimmed();

      auto* db_item = new QTreeWidgetItem(perm_db_tree_);
      db_item->setText(0, db_name);
      db_item->setText(1, tr("Database"));
      db_item->setData(0, Qt::UserRole, "db:" + db_name);

      // Fetch tables
      QueryResult tbl_result = ExecuteSql(
          QString("SHOW TABLES FROM %1").arg(db_name));
      if (!tbl_result.is_error) {
        for (const auto& trow : tbl_result.rows) {
          if (trow.empty()) continue;
          QString tbl_name = trow[0].trimmed();

          auto* tbl_item = new QTreeWidgetItem(db_item);
          tbl_item->setText(0, tbl_name);
          tbl_item->setText(1, tr("Table"));
          tbl_item->setData(0, Qt::UserRole, "table:" + db_name + "." + tbl_name);
        }
      }
    }
  }

  perm_db_tree_->expandAll();
}

void UserManager::OnDbTableSelected(QTreeWidgetItem* item, int column) {
  Q_UNUSED(column);
  if (!item || current_user_.isEmpty()) return;

  // Load permissions for this user on this object
  OnLoadPermissions();
}

void UserManager::OnLoadPermissions() {
  if (current_user_.isEmpty()) return;

  auto* current_item = perm_db_tree_->currentItem();
  if (!current_item) return;

  QString data = current_item->data(0, Qt::UserRole).toString();

  // Reset all checkboxes
  for (auto& cb : perm_checkboxes_) cb->setChecked(false);

  // Query current grants
  QString target;
  if (data.startsWith("table:")) {
    target = data.mid(6);  // "db.table"
  } else if (data.startsWith("db:")) {
    target = data.mid(3) + ".*";  // "db.*"
  } else {
    return;
  }

  QueryResult r = ExecuteSql(
      QString("SHOW GRANTS FOR '%1'@'%2'").arg(current_user_, current_host_));

  if (!r.is_error) {
    for (const auto& row : r.rows) {
      if (row.empty()) continue;
      QString grant_stmt = row[0];
      // Parse grant statement: "GRANT SELECT, INSERT ON db.* TO ..."
      if (grant_stmt.contains(target, Qt::CaseInsensitive) ||
          grant_stmt.contains("*.*")) {
        for (const QString& priv : perm_checkboxes_.keys()) {
          if (grant_stmt.contains(priv, Qt::CaseInsensitive) ||
              grant_stmt.contains("ALL PRIVILEGES", Qt::CaseInsensitive)) {
            perm_checkboxes_[priv]->setChecked(true);
          }
        }
      }
    }
  }
}

void UserManager::OnApplyPermissions() {
  if (current_user_.isEmpty()) return;

  auto* current_item = perm_db_tree_->currentItem();
  if (!current_item) {
    QMessageBox::information(this, tr("No Object Selected"),
                             tr("Please select a database or table from the tree."));
    return;
  }

  QString data = current_item->data(0, Qt::UserRole).toString();

  // Collect checked privileges
  QStringList granted;
  for (auto it = perm_checkboxes_.begin(); it != perm_checkboxes_.end(); ++it) {
    if (it.value()->isChecked()) {
      granted.append(it.key());
    }
  }

  // Build GRANT statement
  QString priv_list = granted.isEmpty() ? "USAGE" : granted.join(", ");

  QString on_target;
  if (data.startsWith("table:")) {
    on_target = data.mid(6);  // "db.table"
  } else if (data.startsWith("db:")) {
    on_target = data.mid(3) + ".*";  // "db.*"
  } else {
    on_target = "*.*";
  }

  // First revoke all privileges on this object
  QueryResult revoke_r = ExecuteSql(
      QString("REVOKE ALL PRIVILEGES ON %1 FROM '%2'@'%3'")
          .arg(on_target, current_user_, current_host_));
  // Revoke might fail if no privileges existed — ignore

  // Then grant the selected privileges
  if (!granted.isEmpty()) {
    QueryResult grant_r = ExecuteSql(
        QString("GRANT %1 ON %2 TO '%3'@'%4'")
            .arg(priv_list, on_target, current_user_, current_host_));

    if (grant_r.is_error) {
      QMessageBox::critical(this, tr("Error"),
                            tr("Failed to apply permissions:\n%1").arg(grant_r.error_message));
      return;
    }
  }

  QMessageBox::information(this, tr("Success"),
                           tr("Permissions applied for '%1'@'%2' on %3:\n%4")
                               .arg(current_user_, current_host_, on_target,
                                    granted.isEmpty() ? tr("(none — all privileges revoked)")
                                                      : granted.join(", ")));
}

}  // namespace studio
}  // namespace goods_db
