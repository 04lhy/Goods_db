#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>

#include "sql/log/binary_log.h"

void PrintUsage(const char* prog) {
  std::cout << "goods_db_binlog — Binary log viewer v0.1.0\n"
            << "Usage: " << prog << " [options] <binlog_file>\n"
            << "Options:\n"
            << "  --start-position=N   Start reading from position N\n"
            << "  --stop-position=N    Stop reading at position N\n"
            << "  --verbose            Show detailed event content\n"
            << "  --help               Show this help\n"
            << "\n"
            << "Example:\n"
            << "  goods_db_binlog --verbose goods_db_binlog.000001\n";
}

void PrintEvent(const goods_db::BinlogEvent& event, bool verbose) {
  // Convert timestamp to human-readable
  std::time_t ts = static_cast<std::time_t>(event.timestamp);
  std::tm tm_buf;
  localtime_r(&ts, &tm_buf);

  char time_buf[32];
  std::snprintf(time_buf, sizeof(time_buf),
                "%04d-%02d-%02d %02d:%02d:%02d",
                tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

  std::cout << "# " << time_buf << " "
            << "server_id=" << event.server_id << " "
            << "event=" << goods_db::BinlogEventTypeToString(event.event_type)
            << " "
            << "txn_id=" << event.txn_id << " "
            << "len=" << event.payload.size() << " "
            << "crc=" << std::hex << event.checksum << std::dec << "\n";

  if (verbose && !event.payload.empty()) {
    if (event.event_type == goods_db::BinlogEventType::QUERY) {
      std::string sql(event.payload.begin(), event.payload.end());
      std::cout << sql << "\n";
    } else if (event.event_type == goods_db::BinlogEventType::ROW_INSERT) {
      std::cout << "### INSERT payload: ";
      std::string s(event.payload.begin(),
                    std::min(event.payload.end(), event.payload.begin() + 200));
      std::cout << s;
      if (event.payload.size() > 200) std::cout << "...";
      std::cout << "\n";
    } else if (event.event_type == goods_db::BinlogEventType::ROW_UPDATE) {
      std::cout << "### UPDATE payload: ";
      std::string s(event.payload.begin(),
                    std::min(event.payload.end(), event.payload.begin() + 200));
      std::cout << s;
      if (event.payload.size() > 200) std::cout << "...";
      std::cout << "\n";
    } else if (event.event_type == goods_db::BinlogEventType::ROW_DELETE) {
      std::cout << "### DELETE payload: ";
      std::string s(event.payload.begin(),
                    std::min(event.payload.end(), event.payload.begin() + 200));
      std::cout << s;
      if (event.payload.size() > 200) std::cout << "...";
      std::cout << "\n";
    } else if (event.event_type == goods_db::BinlogEventType::XID) {
      std::cout << "### COMMIT (XID)\n";
    }
  }
}

int main(int argc, char* argv[]) {
  std::string binlog_file;
  uint64_t start_pos = 0;
  uint64_t stop_pos = UINT64_MAX;
  bool verbose = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--start-position" && i + 1 < argc) {
      start_pos = std::stoull(argv[++i]);
    } else if (arg == "--stop-position" && i + 1 < argc) {
      stop_pos = std::stoull(argv[++i]);
    } else if (arg == "--verbose") {
      verbose = true;
    } else if (arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    } else if (arg[0] != '-') {
      binlog_file = arg;
    }
  }

  if (binlog_file.empty()) {
    std::cerr << "ERROR: No binlog file specified.\n";
    PrintUsage(argv[0]);
    return 1;
  }

  goods_db::BinlogReader reader(binlog_file);
  if (!reader.IsOpen()) {
    std::cerr << "ERROR: Cannot open binlog file: " << binlog_file << "\n";
    return 1;
  }

  if (start_pos > 0) {
    reader.SeekToPosition(start_pos);
  }

  goods_db::BinlogEvent event;
  uint64_t current_pos = start_pos;
  int event_count = 0;

  while (reader.ReadNextEvent(event)) {
    if (current_pos >= stop_pos) {
      break;
    }

    PrintEvent(event, verbose);
    event_count++;
    current_pos++;
  }

  std::cout << "# " << event_count << " events shown\n";
  return 0;
}
