#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

namespace goods_db {

/**
 * Simple configuration manager.
 * Reads key-value pairs from a configuration file.
 *
 * File format (goods_db.conf):
 *   key = value
 *   # comment
 */
class Config {
public:
    Config() = default;
    ~Config() = default;

    /** Load configuration from file. Returns true on success. */
    bool LoadFromFile(const std::string& filepath);

    /** Get integer value. Returns default_val if key not found. */
    int32_t GetInt(const std::string& key, int32_t default_val = 0) const;

    /** Get string value. Returns default_val if key not found. */
    std::string GetString(const std::string& key,
                          const std::string& default_val = "") const;

    /** Get boolean value. Returns default_val if key not found. */
    bool GetBool(const std::string& key, bool default_val = false) const;

    /** Set a value programmatically */
    void Set(const std::string& key, const std::string& value);

    /** Check if a key exists */
    bool HasKey(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> config_map_;
    mutable std::mutex mutex_;
};

}  // namespace goods_db
