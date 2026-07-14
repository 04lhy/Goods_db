#include "connection/connection_dialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <memory>
#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

ConnectionDialog::ConnectionDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("New Connection"));
  setMinimumWidth(400);

  auto* layout = new QVBoxLayout(this);

  // Form
  auto* form = new QFormLayout();

  name_edit_ = new QLineEdit("localhost");
  name_edit_->setPlaceholderText(tr("Connection name"));
  form->addRow(tr("&Name:"), name_edit_);

  host_edit_ = new QLineEdit("localhost");
  form->addRow(tr("&Host:"), host_edit_);

  port_spin_ = new QSpinBox();
  port_spin_->setRange(1024, 65535);
  port_spin_->setValue(3307);
  form->addRow(tr("&Port:"), port_spin_);

  user_edit_ = new QLineEdit("root");
  form->addRow(tr("&User:"), user_edit_);

  password_edit_ = new QLineEdit();
  password_edit_->setEchoMode(QLineEdit::Password);
  form->addRow(tr("Pass&word:"), password_edit_);

  database_edit_ = new QLineEdit();
  database_edit_->setPlaceholderText(tr("(optional)"));
  form->addRow(tr("&Database:"), database_edit_);

  layout->addLayout(form);

  // Test result label
  test_result_ = new QLabel();
  layout->addWidget(test_result_);

  // Buttons
  auto* btn_layout = new QHBoxLayout();
  test_btn_ = new QPushButton(tr("&Test Connection"));
  ok_btn_ = new QPushButton(tr("&Connect"));
  cancel_btn_ = new QPushButton(tr("&Cancel"));

  btn_layout->addWidget(test_btn_);
  btn_layout->addStretch();
  btn_layout->addWidget(ok_btn_);
  btn_layout->addWidget(cancel_btn_);
  layout->addLayout(btn_layout);

  connect(test_btn_, &QPushButton::clicked, this, &ConnectionDialog::OnTestConnection);
  connect(ok_btn_, &QPushButton::clicked, this, &ConnectionDialog::OnAccept);
  connect(cancel_btn_, &QPushButton::clicked, this, &QDialog::reject);
}

ConnectionDialog::ConnectionInfo ConnectionDialog::GetConnectionInfo() const {
  ConnectionInfo info;
  info.name = name_edit_->text().isEmpty() ? host_edit_->text() : name_edit_->text();
  info.host = host_edit_->text();
  info.port = static_cast<uint16_t>(port_spin_->value());
  info.user = user_edit_->text();
  info.password = password_edit_->text();
  info.default_database = database_edit_->text();
  return info;
}

void ConnectionDialog::SetConnectionInfo(const ConnectionInfo& info) {
  name_edit_->setText(info.name);
  host_edit_->setText(info.host);
  port_spin_->setValue(info.port);
  user_edit_->setText(info.user);
  password_edit_->setText(info.password);
  database_edit_->setText(info.default_database);
}

void ConnectionDialog::OnTestConnection() {
  test_result_->setText(tr("Testing..."));
  test_result_->setStyleSheet("color: #f39c12;");

  auto info = GetConnectionInfo();
  GoodsDbClient client;
  client.Connect(info.host, info.port);

  if (!client.IsConnected()) {
    test_result_->setText(tr("Failed: %1").arg(client.GetLastError()));
    test_result_->setStyleSheet("color: #e74c3c;");
    return;
  }

  if (client.Authenticate(info.user, info.password, info.default_database)) {
    test_result_->setText(tr("Connection successful!"));
    test_result_->setStyleSheet("color: #27ae60;");
  } else {
    test_result_->setText(tr("Auth failed: %1").arg(client.GetLastError()));
    test_result_->setStyleSheet("color: #e74c3c;");
  }

  client.Disconnect();
}

void ConnectionDialog::OnAccept() {
  if (host_edit_->text().isEmpty()) {
    QMessageBox::warning(this, tr("Validation"),
                         tr("Host cannot be empty."));
    return;
  }
  accept();
}

}  // namespace studio
}  // namespace goods_db
