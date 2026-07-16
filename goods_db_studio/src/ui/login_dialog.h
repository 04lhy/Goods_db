#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QString>

namespace goods_db {
namespace studio {

// =============================================================================
// LoginDialog — startup login screen for goods_db Studio
//
// Provides three entry points:
//   1. Admin login  — username + password → full CRUD + admin access
//   2. Skip button  — guest read-only access
//   3. Custom link  — opens the standard ConnectionDialog
// =============================================================================
class LoginDialog : public QDialog {
  Q_OBJECT

 public:
  struct LoginInfo {
    QString name;
    QString host = "localhost";
    uint16_t port = 3307;
    QString user;
    QString password;
    bool is_guest = false;   // "Skip" was used
    bool cancelled = true;   // Dialog was closed without connecting
  };

  explicit LoginDialog(QWidget* parent = nullptr);

  LoginInfo GetLoginInfo() const { return login_info_; }

 private slots:
  void OnLogin();
  void OnSkip();
  void OnCustomConnection();

 private:
  void SetupUi();

  QLineEdit* user_edit_ = nullptr;
  QLineEdit* password_edit_ = nullptr;
  QPushButton* login_btn_ = nullptr;
  QPushButton* skip_btn_ = nullptr;
  QPushButton* custom_btn_ = nullptr;
  QLabel* status_label_ = nullptr;

  LoginInfo login_info_;
};

}  // namespace studio
}  // namespace goods_db
