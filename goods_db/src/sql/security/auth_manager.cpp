#include "sql/security/auth_manager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/logger.h"
#include "storage/disk/disk_manager.h"
#include "storage/page/table_page.h"
#include "storage/table/table_heap.h"
#include "storage/table/table_iterator.h"
#include "storage/table/tuple.h"
#include "type/schema.h"
#include "type/value.h"

namespace goods_db {

// ---- SHA-256 implementation -------------------------------------------------

namespace {

// SHA-256 constants
static const uint32_t kSha256K[64] = {
  0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1,
  0x923F82A4, 0xAB1C5ED5, 0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
  0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174, 0xE49B69C1, 0xEFBE4786,
  0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
  0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147,
  0x06CA6351, 0x14292967, 0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
  0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85, 0xA2BFE8A1, 0xA81A664B,
  0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
  0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A,
  0x5B9CCA4F, 0x682E6FF3, 0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
  0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

inline uint32_t RotR(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

void Sha256Transform(uint32_t state[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           (static_cast<uint32_t>(block[i * 4 + 3]));
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

  for (int i = 0; i < 64; i++) {
    uint32_t S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
    uint32_t S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = S0 + maj;
    h = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }

  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

}  // anonymous namespace

std::string AuthManager::Sha256(const std::string& data) {
  uint32_t state[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
  };

  // Padding
  size_t bit_len = data.size() * 8;
  size_t padded_len = ((data.size() + 8 + 63) / 64) * 64;
  std::vector<uint8_t> padded(padded_len, 0);
  std::memcpy(padded.data(), data.data(), data.size());
  padded[data.size()] = 0x80;

  // Append bit length in big-endian
  for (int i = 0; i < 8; i++) {
    padded[padded_len - 1 - i] = static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF);
  }

  // Process blocks
  for (size_t i = 0; i < padded_len; i += 64) {
    Sha256Transform(state, padded.data() + i);
  }

  // Output hex string
  std::ostringstream hex;
  for (int i = 0; i < 8; i++) {
    hex << std::hex << std::setw(8) << std::setfill('0') << state[i];
  }
  return hex.str();
}

// =============================================================================
// Helper: create a TableHeap-backed system table file
// =============================================================================

namespace {

/** Initialize a new .db file and return (file_id, root_page_id) */
std::pair<uint16_t, page_id_t> InitTableFile(
    DiskManager* dm, BufferPoolManager* bpm,
    const std::string& table_name, const std::string& file_path) {
  uint16_t file_id = dm->CreateFile(table_name, file_path);
  if (file_id == static_cast<uint16_t>(-1)) {
    return {static_cast<uint16_t>(-1), INVALID_PAGE_ID};
  }

  page_id_t root_page_id = dm->AllocatePage(file_id);
  if (root_page_id == INVALID_PAGE_ID) {
    return {file_id, INVALID_PAGE_ID};
  }

  Page* page = bpm->FetchPage(root_page_id);
  if (!page) return {file_id, INVALID_PAGE_ID};

  page->WLatch();
  std::memset(page->GetData(), 0, PAGE_SIZE);
  TablePage tp;
  tp.LoadFromData(page->GetData());
  tp.Init(root_page_id);
  page->MarkDirty();
  page->WUnlatch();
  bpm->UnpinPage(root_page_id, true);

  return {file_id, root_page_id};
}

}  // anonymous namespace

// ---- System table management -------------------------------------------------

bool AuthManager::OpenSystemTables() {
  // Try to open existing user table
  std::string user_path = data_dir_ + "/__sys_user.db";
  uint16_t user_fid = dm_->OpenFile("__sys_user", user_path);
  if (user_fid == static_cast<uint16_t>(-1)) return false;

  std::string db_path = data_dir_ + "/__sys_db.db";
  uint16_t db_fid = dm_->OpenFile("__sys_db", db_path);
  if (db_fid == static_cast<uint16_t>(-1)) return false;

  std::string tables_priv_path = data_dir_ + "/__sys_tables_priv.db";
  uint16_t tp_fid = dm_->OpenFile("__sys_tables_priv", tables_priv_path);
  if (tp_fid == static_cast<uint16_t>(-1)) return false;

  // Build schemas
  std::vector<Column> user_cols = {
    Column("host", TypeId::VARCHAR, 60, false),
    Column("user", TypeId::VARCHAR, 32, false),
    Column("password_hash", TypeId::VARCHAR, 64, false),
    Column("salt", TypeId::VARCHAR, 16, false),
  };
  user_schema_ = std::make_unique<Schema>(user_cols);

  std::vector<Column> db_cols = {
    Column("host", TypeId::VARCHAR, 60, false),
    Column("user", TypeId::VARCHAR, 32, false),
    Column("db_name", TypeId::VARCHAR, 64, false),
    Column("privileges", TypeId::INTEGER, 0, false),
  };
  db_schema_ = std::make_unique<Schema>(db_cols);

  std::vector<Column> tp_cols = {
    Column("host", TypeId::VARCHAR, 60, false),
    Column("user", TypeId::VARCHAR, 32, false),
    Column("db_name", TypeId::VARCHAR, 64, false),
    Column("table_name", TypeId::VARCHAR, 64, false),
    Column("privileges", TypeId::INTEGER, 0, false),
  };
  tables_priv_schema_ = std::make_unique<Schema>(tp_cols);

  // Create TableHeaps (page 1 is the first data page)
  user_file_id_ = user_fid;
  user_table_ = std::make_unique<TableHeap>(bpm_, MakePageId(user_fid, 1));

  db_file_id_ = db_fid;
  db_table_ = std::make_unique<TableHeap>(bpm_, MakePageId(db_fid, 1));

  tables_priv_file_id_ = tp_fid;
  tables_priv_table_ = std::make_unique<TableHeap>(bpm_, MakePageId(tp_fid, 1));

  LOG_INFO("AuthManager: opened existing system tables in {}", data_dir_);
  return true;
}

void AuthManager::CreateSystemTables() {
  LOG_INFO("AuthManager: creating system tables in {}", data_dir_);

  // ---- User table -----------------------------------------------------------
  std::vector<Column> user_cols = {
    Column("host", TypeId::VARCHAR, 60, false),
    Column("user", TypeId::VARCHAR, 32, false),
    Column("password_hash", TypeId::VARCHAR, 64, false),
    Column("salt", TypeId::VARCHAR, 16, false),
  };
  user_schema_ = std::make_unique<Schema>(user_cols);

  auto [user_fid, user_root] = InitTableFile(
      dm_, bpm_, "__sys_user", data_dir_ + "/__sys_user.db");
  if (user_fid == static_cast<uint16_t>(-1) || user_root == INVALID_PAGE_ID) {
    LOG_ERROR("AuthManager: failed to create __sys_user table");
    return;
  }
  user_file_id_ = user_fid;
  user_table_ = std::make_unique<TableHeap>(bpm_, user_root);

  // ---- DB privileges table --------------------------------------------------
  std::vector<Column> db_cols = {
    Column("host", TypeId::VARCHAR, 60, false),
    Column("user", TypeId::VARCHAR, 32, false),
    Column("db_name", TypeId::VARCHAR, 64, false),
    Column("privileges", TypeId::INTEGER, 0, false),
  };
  db_schema_ = std::make_unique<Schema>(db_cols);

  auto [db_fid, db_root] = InitTableFile(
      dm_, bpm_, "__sys_db", data_dir_ + "/__sys_db.db");
  if (db_fid == static_cast<uint16_t>(-1) || db_root == INVALID_PAGE_ID) {
    LOG_ERROR("AuthManager: failed to create __sys_db table");
    return;
  }
  db_file_id_ = db_fid;
  db_table_ = std::make_unique<TableHeap>(bpm_, db_root);

  // ---- Tables_priv table ----------------------------------------------------
  std::vector<Column> tp_cols = {
    Column("host", TypeId::VARCHAR, 60, false),
    Column("user", TypeId::VARCHAR, 32, false),
    Column("db_name", TypeId::VARCHAR, 64, false),
    Column("table_name", TypeId::VARCHAR, 64, false),
    Column("privileges", TypeId::INTEGER, 0, false),
  };
  tables_priv_schema_ = std::make_unique<Schema>(tp_cols);

  auto [tp_fid, tp_root] = InitTableFile(
      dm_, bpm_, "__sys_tables_priv", data_dir_ + "/__sys_tables_priv.db");
  if (tp_fid == static_cast<uint16_t>(-1) || tp_root == INVALID_PAGE_ID) {
    LOG_ERROR("AuthManager: failed to create __sys_tables_priv table");
    return;
  }
  tables_priv_file_id_ = tp_fid;
  tables_priv_table_ = std::make_unique<TableHeap>(bpm_, tp_root);

  LOG_INFO("AuthManager: system tables created successfully");
}

void AuthManager::CreateDefaultUsers() {
  // root@localhost with empty password
  UserRecord root;
  root.host = "localhost";
  root.user = "root";
  root.salt = GenerateSalt();
  root.password_hash = HashPassword("", root.salt);
  user_cache_.push_back(root);
  PersistUser(root);

  // root@% with empty password
  UserRecord root_any;
  root_any.host = "%";
  root_any.user = "root";
  root_any.salt = GenerateSalt();
  root_any.password_hash = HashPassword("", root_any.salt);
  user_cache_.push_back(root_any);
  PersistUser(root_any);

  // admin@localhost with password "12345" — full administrator
  UserRecord admin;
  admin.host = "localhost";
  admin.user = "admin";
  admin.salt = GenerateSalt();
  admin.password_hash = HashPassword("12345", admin.salt);
  user_cache_.push_back(admin);
  PersistUser(admin);

  // guest@localhost with empty password — read-only guest
  UserRecord guest;
  guest.host = "localhost";
  guest.user = "guest";
  guest.salt = GenerateSalt();
  guest.password_hash = HashPassword("", guest.salt);
  user_cache_.push_back(guest);
  PersistUser(guest);

  // ---- Grant default privileges ----
  // root gets ALL on everything
  GrantPrivilege("root", "%", "*", "*",
                 static_cast<uint32_t>(Privilege::ALL));
  // admin gets ALL on everything (can manage users + grant)
  GrantPrivilege("admin", "localhost", "*", "*",
                 static_cast<uint32_t>(Privilege::ALL));
  // guest gets SELECT only (read-only)
  GrantPrivilege("guest", "localhost", "*", "*",
                 static_cast<uint32_t>(Privilege::SELECT));

  LOG_INFO("AuthManager: default users created (root, admin, guest)");
}

// ---- Public methods ---------------------------------------------------------

void AuthManager::Initialize(Catalog* catalog, BufferPoolManager* bpm,
                              DiskManager* dm, const std::string& data_dir) {
  catalog_ = catalog;
  bpm_ = bpm;
  dm_ = dm;
  data_dir_ = data_dir;
  initialized_ = true;

  // Try to open existing system tables, or create them on first run
  if (!OpenSystemTables()) {
    CreateSystemTables();
    CreateDefaultUsers();
  } else {
    LoadUserCache();
    LoadPrivCache();
  }
}

bool AuthManager::CheckConnection(const std::string& host,
                                   const std::string& user,
                                   const std::string& password) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (IsHostBlocked(host)) {
    return false;
  }

  const UserRecord* rec = FindUser(user, host);
  if (!rec) {
    return false;
  }

  std::string hash = HashPassword(password, rec->salt);
  return hash == rec->password_hash;
}

bool AuthManager::CheckAccess(const std::string& user,
                               const std::string& host, const std::string& db,
                               const std::string& table,
                               Privilege required_priv) {
  std::lock_guard<std::mutex> lock(mutex_);

  const PrivRecord* match = FindBestMatch(user, host, db, table);
  if (!match) {
    return false;
  }

  return HasPrivilege(match->privileges, required_priv);
}

bool AuthManager::CreateUser(const std::string& user, const std::string& host,
                              const std::string& password) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if user exists
  if (FindUser(user, host)) {
    return false;  // Already exists
  }

  UserRecord rec;
  rec.user = user;
  rec.host = host;
  rec.salt = GenerateSalt();
  rec.password_hash = HashPassword(password, rec.salt);
  user_cache_.push_back(rec);

  // Persist to system table
  PersistUser(rec);
  return true;
}

bool AuthManager::DropUser(const std::string& user, const std::string& host) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = std::find_if(user_cache_.begin(), user_cache_.end(),
      [&](const UserRecord& r) { return r.user == user && r.host == host; });
  if (it == user_cache_.end()) {
    return false;
  }

  user_cache_.erase(it);

  // Also remove privileges
  priv_cache_.erase(
      std::remove_if(priv_cache_.begin(), priv_cache_.end(),
                     [&](const PrivRecord& p) {
                       return p.user == user && p.host == host;
                     }),
      priv_cache_.end());

  // Remove from persisted system tables
  RemoveUserFromTable(user, host);
  RemovePrivFromTable(user, host, "*", "*");
  return true;
}

bool AuthManager::AlterUserPassword(const std::string& user,
                                     const std::string& host,
                                     const std::string& new_password) {
  return SetPassword(user, host, new_password);
}

bool AuthManager::SetPassword(const std::string& user,
                               const std::string& host,
                               const std::string& new_password) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& rec : user_cache_) {
    if (rec.user == user && rec.host == host) {
      rec.salt = GenerateSalt();
      rec.password_hash = HashPassword(new_password, rec.salt);

      // Update persisted record: remove old, insert new
      RemoveUserFromTable(user, host);
      PersistUser(rec);
      return true;
    }
  }
  return false;
}

bool AuthManager::GrantPrivilege(const std::string& user,
                                  const std::string& host,
                                  const std::string& db,
                                  const std::string& table,
                                  uint32_t privileges) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Find existing or create new
  for (auto& rec : priv_cache_) {
    if (rec.user == user && rec.host == host && rec.db == db &&
        rec.table_name == table) {
      // Remove old persisted record
      RemovePrivFromTable(user, host, db, table);
      rec.privileges |= privileges;
      PersistPriv(rec);
      return true;
    }
  }

  PrivRecord rec;
  rec.user = user;
  rec.host = host;
  rec.db = db;
  rec.table_name = table;
  rec.privileges = privileges;
  priv_cache_.push_back(rec);

  PersistPriv(rec);
  return true;
}

bool AuthManager::RevokePrivilege(const std::string& user,
                                   const std::string& host,
                                   const std::string& db,
                                   const std::string& table,
                                   uint32_t privileges) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& rec : priv_cache_) {
    if (rec.user == user && rec.host == host && rec.db == db &&
        rec.table_name == table) {
      // Remove old persisted record
      RemovePrivFromTable(user, host, db, table);
      rec.privileges &= ~privileges;
      if (rec.privileges != 0) {
        PersistPriv(rec);
      }
      return true;
    }
  }
  return false;
}

void AuthManager::FlushPrivileges() {
  std::lock_guard<std::mutex> lock(mutex_);
  user_cache_.clear();
  priv_cache_.clear();
  LoadUserCache();
  LoadPrivCache();
}

void AuthManager::RecordAuthFailure(const std::string& host) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto now = std::chrono::steady_clock::now();
  for (auto& entry : block_list_) {
    if (entry.host == host) {
      // Reset if block duration expired
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         now - entry.last_failure).count();
      if (elapsed > kBlockDurationSeconds) {
        entry.failure_count = 0;
      }
      entry.failure_count++;
      entry.last_failure = now;
      return;
    }
  }

  BlockEntry entry;
  entry.host = host;
  entry.failure_count = 1;
  entry.last_failure = now;
  block_list_.push_back(entry);
}

bool AuthManager::IsHostBlocked(const std::string& host) {
  auto now = std::chrono::steady_clock::now();
  for (const auto& entry : block_list_) {
    if (entry.host == host) {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         now - entry.last_failure).count();
      if (elapsed > kBlockDurationSeconds) {
        return false;  // Block expired
      }
      return entry.failure_count >= kMaxAuthFailures;
    }
  }
  return false;
}

void AuthManager::ClearBlockList() {
  std::lock_guard<std::mutex> lock(mutex_);
  block_list_.clear();
}

std::vector<AuthManager::UserRecord> AuthManager::GetUsers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return user_cache_;
}

std::vector<AuthManager::PrivRecord> AuthManager::GetPrivileges() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return priv_cache_;
}

// ---- Private helpers --------------------------------------------------------

std::string AuthManager::GenerateSalt() {
  static const char kChars[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, sizeof(kChars) - 2);

  std::string salt;
  salt.resize(16);
  for (int i = 0; i < 16; i++) {
    salt[i] = kChars[dis(gen)];
  }
  return salt;
}

std::string AuthManager::HashPassword(const std::string& password,
                                       const std::string& salt) {
  return Sha256(password + salt);
}

void AuthManager::LoadUserCache() {
  if (!user_table_ || !user_schema_) return;

  auto it = user_table_->MakeIterator();
  while (it->HasNext()) {
    auto [rid, tuple] = it->Next();
    UserRecord rec;
    rec.host = tuple.GetValue(user_schema_.get(), 0).GetAsVarchar();
    rec.user = tuple.GetValue(user_schema_.get(), 1).GetAsVarchar();
    rec.password_hash = tuple.GetValue(user_schema_.get(), 2).GetAsVarchar();
    rec.salt = tuple.GetValue(user_schema_.get(), 3).GetAsVarchar();
    user_cache_.push_back(rec);
  }
  LOG_INFO("AuthManager: loaded {} user(s) from disk", user_cache_.size());
}

void AuthManager::LoadPrivCache() {
  // Load database-level privileges
  if (db_table_ && db_schema_) {
    auto it = db_table_->MakeIterator();
    while (it->HasNext()) {
      auto [rid, tuple] = it->Next();
      PrivRecord rec;
      rec.host = tuple.GetValue(db_schema_.get(), 0).GetAsVarchar();
      rec.user = tuple.GetValue(db_schema_.get(), 1).GetAsVarchar();
      rec.db = tuple.GetValue(db_schema_.get(), 2).GetAsVarchar();
      rec.privileges = static_cast<uint32_t>(
          tuple.GetValue(db_schema_.get(), 3).GetAsInteger());
      rec.table_name = "*";
      priv_cache_.push_back(rec);
    }
  }

  // Load table-level privileges
  if (tables_priv_table_ && tables_priv_schema_) {
    auto it = tables_priv_table_->MakeIterator();
    while (it->HasNext()) {
      auto [rid, tuple] = it->Next();
      PrivRecord rec;
      rec.host = tuple.GetValue(tables_priv_schema_.get(), 0).GetAsVarchar();
      rec.user = tuple.GetValue(tables_priv_schema_.get(), 1).GetAsVarchar();
      rec.db = tuple.GetValue(tables_priv_schema_.get(), 2).GetAsVarchar();
      rec.table_name = tuple.GetValue(tables_priv_schema_.get(), 3).GetAsVarchar();
      rec.privileges = static_cast<uint32_t>(
          tuple.GetValue(tables_priv_schema_.get(), 4).GetAsInteger());
      priv_cache_.push_back(rec);
    }
  }

  LOG_INFO("AuthManager: loaded {} privilege(s) from disk", priv_cache_.size());
}

void AuthManager::PersistUser(const UserRecord& rec) {
  if (!user_table_ || !user_schema_) return;

  std::vector<Value> values = {
    Value::CreateVarchar(rec.host),
    Value::CreateVarchar(rec.user),
    Value::CreateVarchar(rec.password_hash),
    Value::CreateVarchar(rec.salt),
  };
  Tuple tuple = Tuple::CreateFromValues(values, user_schema_.get());
  RID rid;
  user_table_->InsertTuple(tuple, &rid);
}

void AuthManager::RemoveUserFromTable(const std::string& user,
                                       const std::string& host) {
  if (!user_table_ || !user_schema_) return;

  auto it = user_table_->MakeIterator();
  while (it->HasNext()) {
    auto [rid, tuple] = it->Next();
    std::string h = tuple.GetValue(user_schema_.get(), 0).GetAsVarchar();
    std::string u = tuple.GetValue(user_schema_.get(), 1).GetAsVarchar();
    if (h == host && u == user) {
      user_table_->MarkDelete(rid);
    }
  }
}

void AuthManager::PersistPriv(const PrivRecord& rec) {
  if (rec.table_name == "*" || rec.table_name == "%") {
    // Store as database-level privilege
    if (!db_table_ || !db_schema_) return;
    std::vector<Value> values = {
      Value::CreateVarchar(rec.host),
      Value::CreateVarchar(rec.user),
      Value::CreateVarchar(rec.db),
      Value::CreateInteger(static_cast<int32_t>(rec.privileges)),
    };
    Tuple tuple = Tuple::CreateFromValues(values, db_schema_.get());
    RID rid;
    db_table_->InsertTuple(tuple, &rid);
  } else {
    // Store as table-level privilege
    if (!tables_priv_table_ || !tables_priv_schema_) return;
    std::vector<Value> values = {
      Value::CreateVarchar(rec.host),
      Value::CreateVarchar(rec.user),
      Value::CreateVarchar(rec.db),
      Value::CreateVarchar(rec.table_name),
      Value::CreateInteger(static_cast<int32_t>(rec.privileges)),
    };
    Tuple tuple = Tuple::CreateFromValues(values, tables_priv_schema_.get());
    RID rid;
    tables_priv_table_->InsertTuple(tuple, &rid);
  }
}

void AuthManager::RemovePrivFromTable(const std::string& user,
                                       const std::string& host,
                                       const std::string& db,
                                       const std::string& table) {
  // Remove from database-level privileges
  if (db_table_ && db_schema_) {
    auto it = db_table_->MakeIterator();
    while (it->HasNext()) {
      auto [rid, tuple] = it->Next();
      std::string h = tuple.GetValue(db_schema_.get(), 0).GetAsVarchar();
      std::string u = tuple.GetValue(db_schema_.get(), 1).GetAsVarchar();
      std::string d = tuple.GetValue(db_schema_.get(), 2).GetAsVarchar();
      if (h == host && u == user && d == db) {
        db_table_->MarkDelete(rid);
      }
    }
  }

  // Remove from table-level privileges
  if (tables_priv_table_ && tables_priv_schema_) {
    auto it = tables_priv_table_->MakeIterator();
    while (it->HasNext()) {
      auto [rid, tuple] = it->Next();
      std::string h = tuple.GetValue(tables_priv_schema_.get(), 0).GetAsVarchar();
      std::string u = tuple.GetValue(tables_priv_schema_.get(), 1).GetAsVarchar();
      std::string d = tuple.GetValue(tables_priv_schema_.get(), 2).GetAsVarchar();
      std::string t = tuple.GetValue(tables_priv_schema_.get(), 3).GetAsVarchar();
      if (h == host && u == user && d == db && t == table) {
        tables_priv_table_->MarkDelete(rid);
      }
    }
  }
}

const AuthManager::UserRecord* AuthManager::FindUser(
    const std::string& user, const std::string& host) const {
  // Exact match first
  for (const auto& rec : user_cache_) {
    if (rec.user == user && rec.host == host) {
      return &rec;
    }
  }
  // Treat 127.0.0.1 / ::1 as equivalent to "localhost"
  if (host == "127.0.0.1" || host == "::1") {
    for (const auto& rec : user_cache_) {
      if (rec.user == user && rec.host == "localhost") {
        return &rec;
      }
    }
  }
  // Try wildcard host
  for (const auto& rec : user_cache_) {
    if (rec.user == user && rec.host == "%") {
      return &rec;
    }
  }
  return nullptr;
}

const AuthManager::PrivRecord* AuthManager::FindBestMatch(
    const std::string& user, const std::string& host, const std::string& db,
    const std::string& table) const {
  // Priority: exact match > localhost alias > wildcard host > wildcard db > wildcard table
  const PrivRecord* best = nullptr;
  int best_score = -1;

  for (const auto& rec : priv_cache_) {
    int score = 0;
    if (rec.user == user) {
      if (rec.host == host) score += 4;
      else if ((host == "127.0.0.1" || host == "::1") && rec.host == "localhost") score += 3;
      else if (rec.host == "%") score += 2;
      else continue;

      if (rec.db == db) score += 3;
      else if (rec.db == "*" || rec.db == "%") score += 1;
      else continue;

      if (rec.table_name == table) score += 2;
      else if (rec.table_name == "*" || rec.table_name == "%") score += 0;
      else continue;

      if (score > best_score) {
        best_score = score;
        best = &rec;
      }
    }
  }

  return best;
}

}  // namespace goods_db
