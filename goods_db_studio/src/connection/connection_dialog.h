#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>

namespace goods_db {
namespace studio {

// =============================================================================
// ConnectionDialog — dialog for creating/editing server connections
// =============================================================================
class ConnectionDialog : public QDialog {
  Q_OBJECT

 public:
  struct ConnectionInfo {
    QString name;
    QString host = "localhost";
    uint16_t port = 3307;
    QString user = "root";
    QString password;
    QString default_database;
  };

  explicit ConnectionDialog(QWidget* parent = nullptr);

  ConnectionInfo GetConnectionInfo() const;
  void SetConnectionInfo(const ConnectionInfo& info);

 private slots:
  void OnTestConnection();
  void OnAccept();

 private:
  QLineEdit* name_edit_;
  QLineEdit* host_edit_;
  QSpinBox* port_spin_;
  QLineEdit* user_edit_;
  QLineEdit* password_edit_;
  QLineEdit* database_edit_;
  QPushButton* test_btn_;
  QPushButton* ok_btn_;
  QPushButton* cancel_btn_;
  QLabel* test_result_;
};

}  // namespace studio
}  // namespace goods_db
