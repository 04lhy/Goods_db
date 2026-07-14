#pragma once

#include <QByteArray>
#include <QString>
#include "network/goods_db_client.h"

namespace goods_db {
namespace studio {

// =============================================================================
// ProtocolTextAdapter — encode/decode helpers for text-based protocol
// =============================================================================
class ProtocolTextAdapter {
 public:
  static QByteArray BuildAuthPacket(const QString& user,
                                     const QString& password,
                                     const QString& db);
  static QByteArray BuildQueryPacket(const QString& sql);
  static QByteArray BuildPingPacket();
  static QByteArray BuildQuitPacket();

  static QueryResult ParseResponse(const QByteArray& data);
  static bool IsOk(const QByteArray& data);
  static bool IsError(const QByteArray& data);
  static bool IsResultSet(const QByteArray& data);
};

}  // namespace studio
}  // namespace goods_db
