#include "connection/connection_pool.h"

#include <QSettings>

namespace goods_db {
namespace studio {

ConnectionPool::ConnectionPool(QObject* parent) : QObject(parent) {}

ConnectionPool::~ConnectionPool() {
  SaveToSettings();
  for (auto& entry : connections_) {
    if (entry->client) {
      entry->client->Disconnect();
    }
  }
}

void ConnectionPool::AddConnection(const QString& name, const QString& host,
                                    uint16_t port, const QString& user,
                                    const QString& password) {
  // Check if already exists
  for (auto& entry : connections_) {
    if (entry->name == name) {
      // Reconnect
      entry->host = host;
      entry->port = port;
      entry->user = user;
      entry->password = password;
      if (entry->client) {
        entry->client->Connect(host, port);
        entry->client->Authenticate(user, password);
      }
      return;
    }
  }

  auto entry = std::make_unique<ConnEntry>();
  entry->name = name;
  entry->host = host;
  entry->port = port;
  entry->user = user;
  entry->password = password;

  entry->client = new GoodsDbClient(this);
  entry->client->Connect(host, port);

  if (entry->client->IsConnected()) {
    if (entry->client->Authenticate(user, password)) {
      entry->connected = true;
    }
  }

  // Connect signals
  connect(entry->client, &GoodsDbClient::Connected, this,
          [this, name]() { OnClientConnected(name); });
  connect(entry->client, &GoodsDbClient::Disconnected, this,
          [this, name]() { OnClientDisconnected(name); });

  QString entry_name = entry->name;
  connections_.push_back(std::move(entry));

  if (active_name_.isEmpty()) {
    active_name_ = entry_name;
  }

  emit ConnectionAdded(entry_name);
  if (connections_.back()->connected) {
    emit ConnectionEstablished(entry_name);
  }

  SaveToSettings();
}

void ConnectionPool::RemoveConnection(const QString& name) {
  auto it = std::find_if(connections_.begin(), connections_.end(),
                         [&](const auto& e) { return e->name == name; });
  if (it != connections_.end()) {
    if ((*it)->client) {
      (*it)->client->Disconnect();
    }
    connections_.erase(it);
    if (active_name_ == name) {
      active_name_.clear();
      if (!connections_.empty()) {
        active_name_ = connections_.front()->name;
      }
    }
    emit ConnectionRemoved(name);
    SaveToSettings();
  }
}

GoodsDbClient* ConnectionPool::GetConnection(const QString& name) {
  for (auto& entry : connections_) {
    if (entry->name == name) return entry->client;
  }
  return nullptr;
}

GoodsDbClient* ConnectionPool::GetActiveConnection() {
  return GetConnection(active_name_);
}

void ConnectionPool::SetActiveConnection(const QString& name) {
  if (active_name_ != name) {
    active_name_ = name;
    emit ActiveConnectionChanged(name);
  }
}

QStringList ConnectionPool::GetConnectionNames() const {
  QStringList names;
  for (const auto& entry : connections_) {
    names.append(entry->name);
  }
  return names;
}

void ConnectionPool::LoadFromSettings() {
  QSettings settings("goods_db", "goods_db_studio");
  int size = settings.beginReadArray("connections");
  for (int i = 0; i < size; i++) {
    settings.setArrayIndex(i);
    AddConnection(settings.value("name").toString(),
                  settings.value("host").toString(),
                  static_cast<uint16_t>(settings.value("port", 3307).toUInt()),
                  settings.value("user").toString(),
                  settings.value("password").toString());
  }
  settings.endArray();

  active_name_ = settings.value("active_connection").toString();
}

void ConnectionPool::SaveToSettings() {
  QSettings settings("goods_db", "goods_db_studio");
  settings.beginWriteArray("connections");
  for (int i = 0; i < static_cast<int>(connections_.size()); i++) {
    settings.setArrayIndex(i);
    settings.setValue("name", connections_[i]->name);
    settings.setValue("host", connections_[i]->host);
    settings.setValue("port", connections_[i]->port);
    settings.setValue("user", connections_[i]->user);
    settings.setValue("password", connections_[i]->password);
  }
  settings.endArray();
  settings.setValue("active_connection", active_name_);
}

void ConnectionPool::OnClientConnected(const QString& name) {
  for (auto& entry : connections_) {
    if (entry->name == name) {
      entry->connected = true;
      emit ConnectionEstablished(name);
      break;
    }
  }
}

void ConnectionPool::OnClientDisconnected(const QString& name) {
  for (auto& entry : connections_) {
    if (entry->name == name) {
      entry->connected = false;
      emit ConnectionLost(name);
      break;
    }
  }
}

}  // namespace studio
}  // namespace goods_db
