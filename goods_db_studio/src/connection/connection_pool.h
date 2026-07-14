#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

// =============================================================================
// ConnectionPool — manages multiple server connections
//
// Maintains a list of named connections, each with a GoodsDbClient.
// Supports add/remove/activate operations and persists to QSettings.
// =============================================================================
class ConnectionPool : public QObject {
  Q_OBJECT

 public:
  explicit ConnectionPool(QObject* parent = nullptr);
  ~ConnectionPool() override;

  void AddConnection(const QString& name, const QString& host, uint16_t port,
                     const QString& user, const QString& password);
  void RemoveConnection(const QString& name);
  GoodsDbClient* GetConnection(const QString& name);
  GoodsDbClient* GetActiveConnection();
  void SetActiveConnection(const QString& name);

  QStringList GetConnectionNames() const;
  int GetConnectionCount() const { return static_cast<int>(connections_.size()); }

  void LoadFromSettings();
  void SaveToSettings();

 signals:
  void ConnectionAdded(const QString& name);
  void ConnectionRemoved(const QString& name);
  void ActiveConnectionChanged(const QString& name);
  void ConnectionEstablished(const QString& name);
  void ConnectionLost(const QString& name);

 private:
  struct ConnEntry {
    QString name;
    QString host;
    QString user;
    QString password;
    uint16_t port = 3307;
    GoodsDbClient* client = nullptr;
    bool connected = false;
  };

  std::vector<std::unique_ptr<ConnEntry>> connections_;
  QString active_name_;

  void OnClientConnected(const QString& name);
  void OnClientDisconnected(const QString& name);
};

}  // namespace studio
}  // namespace goods_db
