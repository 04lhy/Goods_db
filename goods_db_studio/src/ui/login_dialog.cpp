#include "ui/login_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFont>
#include <QMessageBox>
#include <QTcpSocket>

namespace goods_db {
namespace studio {

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("goods_db Studio — Login"));
  setFixedSize(420, 380);
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  SetupUi();
}

void LoginDialog::SetupUi() {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setSpacing(12);
  main_layout->setContentsMargins(30, 20, 30, 20);

  // ---- Title ----
  auto* title = new QLabel(tr("goods_db Studio"));
  QFont title_font("sans-serif", 22, QFont::Bold);
  title->setFont(title_font);
  title->setAlignment(Qt::AlignCenter);
  title->setStyleSheet("color: #0078d4;");
  main_layout->addWidget(title);

  auto* subtitle = new QLabel(tr("New-Retail Database Management System"));
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setStyleSheet("color: #888; font-size: 11px;");
  main_layout->addWidget(subtitle);

  main_layout->addSpacing(8);

  // ---- Admin Login Group ----
  auto* group = new QGroupBox(tr("Database Login"));
  group->setStyleSheet(
      "QGroupBox { font-weight: bold; border: 1px solid #555; "
      "border-radius: 6px; margin-top: 8px; padding-top: 16px; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 12px; }");
  auto* form = new QFormLayout(group);

  user_edit_ = new QLineEdit("admin");
  user_edit_->setPlaceholderText(tr("Username"));
  user_edit_->setMinimumHeight(30);
  form->addRow(tr("Username:"), user_edit_);

  password_edit_ = new QLineEdit("12345");
  password_edit_->setEchoMode(QLineEdit::Password);
  password_edit_->setPlaceholderText(tr("Password"));
  password_edit_->setMinimumHeight(30);
  form->addRow(tr("Password:"), password_edit_);

  // Login button
  login_btn_ = new QPushButton(tr("  Login  "));
  login_btn_->setMinimumHeight(36);
  login_btn_->setStyleSheet(
      "QPushButton { background-color: #0078d4; color: white; "
      "border-radius: 4px; font-weight: bold; font-size: 13px; }"
      "QPushButton:hover { background-color: #1a8ae0; }");

  auto* btn_row = new QHBoxLayout();
  btn_row->addStretch();
  btn_row->addWidget(login_btn_);
  btn_row->addStretch();
  form->addRow(btn_row);

  main_layout->addWidget(group);

  // ---- Status label ----
  status_label_ = new QLabel();
  status_label_->setAlignment(Qt::AlignCenter);
  main_layout->addWidget(status_label_);

  // ---- Skip button ----
  skip_btn_ = new QPushButton(tr("Skip (Read-Only Guest Access)"));
  skip_btn_->setMinimumHeight(34);
  skip_btn_->setStyleSheet(
      "QPushButton { background-color: transparent; color: #aaa; "
      "border: 1px solid #666; border-radius: 4px; }"
      "QPushButton:hover { color: #fff; border-color: #999; }");
  main_layout->addWidget(skip_btn_);

  // ---- Custom connection link ----
  custom_btn_ = new QPushButton(tr("Custom connection..."));
  custom_btn_->setFlat(true);
  custom_btn_->setCursor(Qt::PointingHandCursor);
  custom_btn_->setStyleSheet(
      "QPushButton { color: #888; text-decoration: underline; border: none; }"
      "QPushButton:hover { color: #0078d4; }");
  main_layout->addWidget(custom_btn_, 0, Qt::AlignCenter);

  // ---- Connect signals ----
  connect(login_btn_, &QPushButton::clicked, this, &LoginDialog::OnLogin);
  connect(skip_btn_, &QPushButton::clicked, this, &LoginDialog::OnSkip);
  connect(custom_btn_, &QPushButton::clicked, this, &LoginDialog::OnCustomConnection);

  // Press Enter in password field triggers login
  connect(password_edit_, &QLineEdit::returnPressed, this, &LoginDialog::OnLogin);
  connect(user_edit_, &QLineEdit::returnPressed, this, [this]() {
    password_edit_->setFocus();
  });
}

void LoginDialog::OnLogin() {
  status_label_->setText(tr("Connecting..."));
  status_label_->setStyleSheet("color: #f39c12;");

  QString user = user_edit_->text().trimmed();
  QString password = password_edit_->text();

  if (user.isEmpty()) {
    status_label_->setText(tr("Please enter a username."));
    status_label_->setStyleSheet("color: #e74c3c;");
    return;
  }

  // Quick connectivity test to localhost:3307
  QTcpSocket test_socket;
  test_socket.connectToHost("localhost", 3307);
  if (!test_socket.waitForConnected(2000)) {
    status_label_->setText(tr("Cannot reach server at localhost:3307"));
    status_label_->setStyleSheet("color: #e74c3c;");
    return;
  }
  test_socket.disconnectFromHost();

  // Fill login info
  login_info_.name = user;
  login_info_.host = "localhost";
  login_info_.port = 3307;
  login_info_.user = user;
  login_info_.password = password;
  login_info_.is_guest = false;
  login_info_.cancelled = false;

  accept();
}

void LoginDialog::OnSkip() {
  login_info_.name = "guest";
  login_info_.host = "localhost";
  login_info_.port = 3307;
  login_info_.user = "guest";
  login_info_.password = "";
  login_info_.is_guest = true;
  login_info_.cancelled = false;

  accept();
}

void LoginDialog::OnCustomConnection() {
  // Accept with a flag — MainWindow will show ConnectionDialog
  login_info_.name = "__custom__";
  login_info_.cancelled = true;  // Don't auto-connect; let user create manually
  reject();
}

}  // namespace studio
}  // namespace goods_db
