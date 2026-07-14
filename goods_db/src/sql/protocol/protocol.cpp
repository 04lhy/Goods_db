#include "sql/protocol/protocol.h"

namespace goods_db {

uint32_t ReadPacketLength(const uint8_t* header) {
  return static_cast<uint32_t>(header[0]) |
         (static_cast<uint32_t>(header[1]) << 8) |
         (static_cast<uint32_t>(header[2]) << 16);
}

void WritePacketLength(uint8_t* header, uint32_t len) {
  header[0] = static_cast<uint8_t>(len & 0xFF);
  header[1] = static_cast<uint8_t>((len >> 8) & 0xFF);
  header[2] = static_cast<uint8_t>((len >> 16) & 0xFF);
}

}  // namespace goods_db
