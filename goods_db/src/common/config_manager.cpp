#include "common/config_manager.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "common/logger.h"

namespace goods_db {

static std::string Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool Config::LoadFromFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN("Config file not found: {}. Using defaults.", filepath);
        return false;
    }

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        line_no++;
        line = Trim(line);

        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;

        // Find '=' separator
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            LOG_WARN("Config line {}: invalid format '{}'", line_no, line);
            continue;
        }

        std::string key = Trim(line.substr(0, eq_pos));
        std::string value = Trim(line.substr(eq_pos + 1));

        if (!key.empty()) {
            config_map_[key] = value;
        }
    }

    LOG_INFO("Loaded {} config entries from {}", config_map_.size(), filepath);
    return true;
}

int32_t Config::GetInt(const std::string& key, int32_t default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = config_map_.find(key);
    if (it == config_map_.end()) return default_val;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return default_val;
    }
}

std::string Config::GetString(const std::string& key,
                               const std::string& default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = config_map_.find(key);
    if (it == config_map_.end()) return default_val;
    return it->second;
}

bool Config::GetBool(const std::string& key, bool default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = config_map_.find(key);
    if (it == config_map_.end()) return default_val;
    std::string val = it->second;
    for (auto& c : val) c = std::tolower(c);
    if (val == "true" || val == "yes" || val == "1") return true;
    if (val == "false" || val == "no" || val == "0") return false;
    return default_val;
}

void Config::Set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_map_[key] = value;
}

bool Config::HasKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_map_.find(key) != config_map_.end();
}

}  // namespace goods_db
