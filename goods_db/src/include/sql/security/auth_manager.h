#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace goods_db {

class Catalog;
class BufferPoolManager;
class DiskManager;
class TableHeap;
class Schema;

// =============================================================================
// Privilege flags (bitmask)
// =============================================================================
enum class Privilege : uint32_t {
  NONE   = 0,
  SELECT = 1 << 0,
  INSERT = 1 << 1,
  UPDATE = 1 << 2,
  DELETE = 1 << 3,
  CREATE = 1 << 4,
  DROP   = 1 << 5,
  INDEX  = 1 << 6,
  ALTER  = 1 << 7,
  GRANT  = 1 << 8,
  ALL    = 0xFFFFFFFF,
};

inline Privilege operator|(Privilege a, Privilege b) {
  return static_cast<Privilege>(static_cast<uint32_t>(a) |
                                static_cast<uint32_t>(b));
}

inline bool HasPrivilege(uint32_t privileges, Privilege required) {
  return (privileges & static_cast<uint32_t>(required)) != 0;
}

inline const char* PrivilegeToString(Privilege p) {
  switch (p) {
    case Privilege::SELECT: return "SELECT";
    case Privilege::INSERT: return "INSERT";
    case Privilege::UPDATE: return "UPDATE";
    case Privilege::DELETE: return "DELETE";
    case Privilege::CREATE: return "CREATE";
    case Privilege::DROP:   return "DROP";
    case Privilege::INDEX:  return "INDEX";
    case Privilege::ALTER:  return "ALTER";
    case Privilege::GRANT:  return "GRANT";
    case Privilege::ALL:    return "ALL";
    default:                return "NONE";
  }
}

// =============================================================================
// AuthManager — authentication and authorization
//
// System tables:
//   goods_db.user (host, user, password_hash, salt)
//   goods_db.db   (host, user, db_name, privileges)
//   goods_db.tables_priv (host, user, db_name, table_name, privileges)
// =============================================================================
class AuthManager {
 public:
  AuthManager() = default;
  ~AuthManager() = default;

  void Initialize(Catalog* catalog, BufferPoolManager* bpm,
                  DiskManager* dm, const std::string& data_dir);
  bool IsInitialized() const { return initialized_; }

  // ---- Authentication ------------------------------------------------------
  bool CheckConnection(const std::string& host, const std::string& user,
                       const std::string& password);

  // ---- Authorization -------------------------------------------------------
  bool CheckAccess(const std::string& user, const std::string& host,
                   const std::string& db, const std::string& table,
                   Privilege required_priv);

  // ---- User management SQL commands ----------------------------------------
  bool CreateUser(const std::string& user, const std::string& host,
                  const std::string& password);
  bool DropUser(const std::string& user, const std::string& host);
  bool AlterUserPassword(const std::string& user, const std::string& host,
                         const std::string& new_password);
  bool SetPassword(const std::string& user, const std::string& host,
                   const std::string& new_password);

  // ---- Privilege management ------------------------------------------------
  bool GrantPrivilege(const std::string& user, const std::string& host,
                      const std::string& db, const std::string& table,
                      uint32_t privileges);
  bool RevokePrivilege(const std::string& user, const std::string& host,
                       const std::string& db, const std::string& table,
                       uint32_t privileges);

  // ---- Cache management ----------------------------------------------------
  void FlushPrivileges();

  // ---- Host blocking -------------------------------------------------------
  void RecordAuthFailure(const std::string& host);
  bool IsHostBlocked(const std::string& host);
  void ClearBlockList();

  // ---- Public accessors for admin commands ---------------------------------
  struct UserRecord {
    std::string host;
    std::string user;
    std::string password_hash;
    std::string salt;
  };

  struct PrivRecord {
    std::string host;
    std::string user;
    std::string db;
    std::string table_name;
    uint32_t privileges = 0;
  };

  std::vector<UserRecord> GetUsers() const;
  std::vector<PrivRecord> GetPrivileges() const;

 private:
  struct BlockEntry {
    std::string host;
    int failure_count = 0;
    std::chrono::steady_clock::time_point last_failure;
  };

  Catalog* catalog_ = nullptr;
  BufferPoolManager* bpm_ = nullptr;
  DiskManager* dm_ = nullptr;
  std::string data_dir_;
  bool initialized_ = false;

  // System table storage
  std::unique_ptr<Schema> user_schema_;
  std::unique_ptr<Schema> db_schema_;
  std::unique_ptr<Schema> tables_priv_schema_;
  std::unique_ptr<TableHeap> user_table_;
  std::unique_ptr<TableHeap> db_table_;
  std::unique_ptr<TableHeap> tables_priv_table_;
  uint16_t user_file_id_ = 0;
  uint16_t db_file_id_ = 0;
  uint16_t tables_priv_file_id_ = 0;

  // In-memory caches
  std::vector<UserRecord> user_cache_;
  std::vector<PrivRecord> priv_cache_;
  std::vector<BlockEntry> block_list_;
  mutable std::mutex mutex_;

  static constexpr int kMaxAuthFailures = 10;
  static constexpr int kBlockDurationSeconds = 300;

  // Password hashing
  static std::string GenerateSalt();
  static std::string HashPassword(const std::string& password,
                                  const std::string& salt);
  static std::string Sha256(const std::string& data);

  // System table management
  bool OpenSystemTables();
  void CreateSystemTables();
  void CreateDefaultUsers();

  // Cache helpers
  void LoadUserCache();
  void LoadPrivCache();
  void PersistUser(const UserRecord& rec);
  void RemoveUserFromTable(const std::string& user, const std::string& host);
  void PersistPriv(const PrivRecord& rec);
  void RemovePrivFromTable(const std::string& user, const std::string& host,
                           const std::string& db, const std::string& table);
  const UserRecord* FindUser(const std::string& user,
                             const std::string& host) const;
  const PrivRecord* FindBestMatch(const std::string& user,
                                  const std::string& host,
                                  const std::string& db,
                                  const std::string& table) const;
};

}  // namespace goods_db
