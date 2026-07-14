#include "network/protocol_text_adapter.h"

namespace goods_db {
namespace studio {

QByteArray ProtocolTextAdapter::BuildAuthPacket(const QString& user,
                                                  const QString& password,
                                                  const QString& db) {
  QByteArray packet;
  packet.append("AUTH");
  packet.append('\0');
  packet.append(user.toUtf8());
  packet.append('\0');
  packet.append(password.toUtf8());
  packet.append('\0');
  packet.append(db.toUtf8());
  packet.append('\0');
  return packet;
}

QByteArray ProtocolTextAdapter::BuildQueryPacket(const QString& sql) {
  QByteArray packet;
  packet.append("QUERY");
  packet.append('\0');
  packet.append(sql.toUtf8());
  return packet;
}

QByteArray ProtocolTextAdapter::BuildPingPacket() {
  return QByteArray("PING");
}

QByteArray ProtocolTextAdapter::BuildQuitPacket() {
  return QByteArray("QUIT");
}

QueryResult ProtocolTextAdapter::ParseResponse(const QByteArray& data) {
  QueryResult result;

  QString text = QString::fromUtf8(data).trimmed();
  if (text.startsWith("OK ")) {
    result.is_error = false;
    QStringList parts = text.mid(3).split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
      result.affected_rows = parts[0].toULongLong();
      result.last_insert_id = parts[1].toULongLong();
    }
  } else if (text.startsWith("ERR ")) {
    result.is_error = true;
    result.error_message = text.mid(4);
  }

  return result;
}

bool ProtocolTextAdapter::IsOk(const QByteArray& data) {
  return data.startsWith("OK ");
}

bool ProtocolTextAdapter::IsError(const QByteArray& data) {
  return data.startsWith("ERR ");
}

bool ProtocolTextAdapter::IsResultSet(const QByteArray& data) {
  return data.startsWith("Columns: ") || data.startsWith("ColDef: ");
}

}  // namespace studio
}  // namespace goods_db
