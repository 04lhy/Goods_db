#include "sql/log/binary_log.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdio>

#include "common/logger.h"

namespace goods_db {

const char* BinlogEventTypeToString(BinlogEventType type) {
  switch (type) {
    case BinlogEventType::QUERY:       return "Query";
    case BinlogEventType::ROW_INSERT:  return "RowInsert";
    case BinlogEventType::ROW_UPDATE:  return "RowUpdate";
    case BinlogEventType::ROW_DELETE:  return "RowDelete";
    case BinlogEventType::XID:         return "Xid";
    default:                           return "Unknown";
  }
}

// ---- BinlogWriter ----------------------------------------------------------

BinlogWriter::~BinlogWriter() {
  Shutdown();
}

void BinlogWriter::Initialize(const std::string& file_prefix,
                               size_t max_size_mb) {
  std::lock_guard<std::mutex> lock(mutex_);
  file_prefix_ = file_prefix;
  max_size_bytes_ = max_size_mb * 1024ULL * 1024;

  std::string path = GetFilePath(file_index_);
  file_.open(path, std::ios::out | std::ios::binary | std::ios::app);
  if (!file_.is_open()) {
    return;
  }

  // Get current size
  file_.seekp(0, std::ios::end);
  current_size_ = file_.tellp();

  UpdateIndexFile();
  initialized_ = true;
}

void BinlogWriter::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_ && file_.is_open()) {
    Flush();
    file_.close();
  }
  initialized_ = false;
}

void BinlogWriter::WriteEvent(BinlogEventType type, uint64_t txn_id,
                               const std::vector<uint8_t>& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check rotation
  if (current_size_ >= max_size_bytes_) {
    file_.close();
    file_index_++;
    std::string path = GetFilePath(file_index_);
    file_.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
    current_size_ = 0;
    UpdateIndexFile();
  }

  if (!file_.is_open()) return;

  // Build event
  BinlogEvent event;
  event.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
  event.server_id = 1;
  event.event_type = type;
  event.txn_id = txn_id;
  event.payload = payload;
  event.checksum = CalculateCRC32(payload);

  // Write to file
  WriteUint64(event.timestamp);
  WriteUint32(event.server_id);
  file_.put(static_cast<char>(event.event_type));
  WriteUint64(event.txn_id);
  WriteUint32(static_cast<uint32_t>(event.payload.size()));
  if (!event.payload.empty()) {
    file_.write(reinterpret_cast<const char*>(event.payload.data()),
                event.payload.size());
  }
  WriteUint32(event.checksum);

  current_size_ += 8 + 4 + 1 + 8 + 4 + event.payload.size() + 4;
}

void BinlogWriter::WriteQueryEvent(uint64_t txn_id, const std::string& sql) {
  std::vector<uint8_t> payload(sql.begin(), sql.end());
  WriteEvent(BinlogEventType::QUERY, txn_id, payload);
}

void BinlogWriter::WriteRowEvent(BinlogEventType type, uint64_t txn_id,
                                  const std::string& table_name,
                                  const std::vector<std::string>& columns,
                                  const std::vector<std::string>& values) {
  std::vector<uint8_t> payload;

  // Encode: table_name\0col1\0col2\0...\0val1\0val2\0...
  payload.insert(payload.end(), table_name.begin(), table_name.end());
  payload.push_back(0);
  for (const auto& col : columns) {
    payload.insert(payload.end(), col.begin(), col.end());
    payload.push_back(0);
  }
  payload.push_back(0);  // double null = separator
  for (const auto& val : values) {
    payload.insert(payload.end(), val.begin(), val.end());
    payload.push_back(0);
  }

  WriteEvent(type, txn_id, payload);
}

void BinlogWriter::Flush() {
  if (file_.is_open()) {
    file_.flush();
  }
}

void BinlogWriter::Rotate() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_.is_open()) {
    file_.close();
  }
  file_index_++;
  std::string path = GetFilePath(file_index_);
  file_.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
  current_size_ = 0;
  UpdateIndexFile();
}

std::pair<int, uint64_t> BinlogWriter::GetPosition() const {
  return {file_index_, current_size_};
}

std::string BinlogWriter::GetFilePath(int index) const {
  std::ostringstream oss;
  oss << file_prefix_ << "." << std::setw(6) << std::setfill('0') << index;
  return oss.str();
}

void BinlogWriter::WriteUint32(uint32_t val) {
  file_.put(static_cast<char>(val & 0xFF));
  file_.put(static_cast<char>((val >> 8) & 0xFF));
  file_.put(static_cast<char>((val >> 16) & 0xFF));
  file_.put(static_cast<char>((val >> 24) & 0xFF));
}

void BinlogWriter::WriteUint64(uint64_t val) {
  for (int i = 0; i < 8; i++) {
    file_.put(static_cast<char>((val >> (i * 8)) & 0xFF));
  }
}

uint32_t BinlogWriter::CalculateCRC32(const std::vector<uint8_t>& data) {
  // Simple CRC32 implementation
  uint32_t crc = 0xFFFFFFFF;
  static const uint32_t table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72576785, 0x05505713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
  };

  for (uint8_t byte : data) {
    crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF];
  }
  return crc ^ 0xFFFFFFFF;
}

void BinlogWriter::UpdateIndexFile() {
  std::string index_path = file_prefix_ + ".index";
  std::ofstream idx(index_path, std::ios::out | std::ios::app);
  if (idx.is_open()) {
    idx << GetFilePath(file_index_) << "\n";
  }
}

// ---- Log management methods ------------------------------------------------

std::string BinlogWriter::GetCurrentFileName() const {
  return GetFilePath(file_index_);
}

uint64_t BinlogWriter::GetCurrentPosition() const {
  return current_pos_;
}

std::string BinlogWriter::GetIndexFilePath() const {
  return file_prefix_ + ".index";
}

std::vector<BinlogEventInfo> BinlogWriter::ReadEvents(
    const std::string& log_name, uint64_t from_pos, int64_t limit) const {
  std::vector<BinlogEventInfo> results;

  std::string file_to_read;
  if (!log_name.empty()) {
    file_to_read = log_name;
  } else {
    file_to_read = GetFilePath(file_index_);
  }

  std::ifstream file(file_to_read, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return results;
  }

  // Seek to position if specified
  if (from_pos > 0) {
    file.seekg(static_cast<std::streamoff>(from_pos));
  }

  auto read_u64 = [&file]() -> uint64_t {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
      int c = file.get();
      if (c == EOF) return val;
      val |= static_cast<uint64_t>(static_cast<uint8_t>(c)) << (i * 8);
    }
    return val;
  };

  auto read_u32 = [&file]() -> uint32_t {
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
      int c = file.get();
      if (c == EOF) return val;
      val |= static_cast<uint32_t>(static_cast<uint8_t>(c)) << (i * 8);
    }
    return val;
  };

  int64_t count = 0;
  while (file.good() && !file.eof()) {
    if (limit >= 0 && count >= limit) break;

    uint64_t pos_before = static_cast<uint64_t>(file.tellg());

    uint64_t timestamp = read_u64();
    if (file.eof() || file.fail()) break;
    uint32_t server_id = read_u32();
    auto event_type = static_cast<BinlogEventType>(file.get());
    uint64_t txn_id = read_u64();
    uint32_t payload_len = read_u32();

    std::string payload_str;
    if (payload_len > 0 && payload_len < 16 * 1024 * 1024) {  // sanity check: max 16MB
      std::vector<char> buf(payload_len);
      file.read(buf.data(), payload_len);
      payload_str.assign(buf.data(), payload_len);
    }

    uint32_t checksum = read_u32();
    uint64_t pos_after = static_cast<uint64_t>(file.tellg());

    if (file.fail()) break;

    // Build info string from payload
    std::string info;
    if (event_type == BinlogEventType::QUERY) {
      info = payload_str;
    } else if (event_type == BinlogEventType::XID) {
      info = "COMMIT /* XID */";
    } else {
      // For ROW events, extract table name
      size_t null_pos = payload_str.find('\0');
      if (null_pos != std::string::npos) {
        info = std::string("table: ") + payload_str.substr(0, null_pos);
      }
    }

    BinlogEventInfo ev_info;
    ev_info.log_name = file_to_read;
    ev_info.position = pos_before;
    ev_info.event_type = BinlogEventTypeToString(event_type);
    ev_info.server_id = server_id;
    ev_info.end_log_pos = pos_after;
    ev_info.info = info;
    results.push_back(ev_info);
    count++;
  }

  return results;
}

void BinlogWriter::PurgeTo(const std::string& log_name) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Extract the index number from the log name (e.g., "goods_db_binlog.000003" → 3)
  int target_index = -1;
  size_t dot_pos = log_name.rfind('.');
  if (dot_pos != std::string::npos) {
    std::string num_str = log_name.substr(dot_pos + 1);
    try {
      target_index = std::stoi(num_str);
    } catch (...) {
      return;
    }
  }

  if (target_index <= 0) return;

  // Remove all binlog files before the target index
  for (int i = 1; i < target_index; i++) {
    std::string path = GetFilePath(i);
    std::remove(path.c_str());
    LOG_DEBUG("Purged binlog file: %s", path.c_str());
  }

  // Update index file
  UpdateIndexFile();
}

void BinlogWriter::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Close current file
  if (file_.is_open()) {
    file_.close();
  }

  // Remove all binlog files
  for (int i = 1; i <= file_index_; i++) {
    std::string path = GetFilePath(i);
    std::remove(path.c_str());
  }

  // Remove index file
  std::string index_path = GetIndexFilePath();
  std::remove(index_path.c_str());

  // Reset state
  file_index_ = 1;
  current_size_ = 0;
  current_pos_ = 0;

  // Start fresh
  std::string path = GetFilePath(1);
  file_.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
  UpdateIndexFile();
}

std::vector<std::string> BinlogWriter::ListBinlogFiles() const {
  std::vector<std::string> files;
  for (int i = 1; i <= file_index_; i++) {
    files.push_back(GetFilePath(i));
  }
  return files;
}

// ---- BinlogReader ----------------------------------------------------------

BinlogReader::BinlogReader(const std::string& file_path) {
  file_.open(file_path, std::ios::in | std::ios::binary);
}

BinlogReader::~BinlogReader() {
  if (file_.is_open()) {
    file_.close();
  }
}

bool BinlogReader::ReadNextEvent(BinlogEvent& event) {
  if (!file_.is_open() || file_.eof()) {
    return false;
  }

  auto read_u64 = [this]() -> uint64_t {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
      int c = file_.get();
      if (c == EOF) return val;
      val |= static_cast<uint64_t>(static_cast<uint8_t>(c)) << (i * 8);
    }
    return val;
  };

  auto read_u32 = [this]() -> uint32_t {
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
      int c = file_.get();
      if (c == EOF) return val;
      val |= static_cast<uint32_t>(static_cast<uint8_t>(c)) << (i * 8);
    }
    return val;
  };

  event.timestamp = read_u64();
  if (file_.eof()) return false;
  event.server_id = read_u32();
  event.event_type = static_cast<BinlogEventType>(file_.get());
  event.txn_id = read_u64();
  uint32_t payload_len = read_u32();
  event.payload.resize(payload_len);
  if (payload_len > 0) {
    file_.read(reinterpret_cast<char*>(event.payload.data()), payload_len);
  }
  event.checksum = read_u32();

  return !file_.fail();
}

void BinlogReader::SeekToPosition(uint64_t position) {
  if (file_.is_open()) {
    file_.seekg(position);
  }
}

}  // namespace goods_db
