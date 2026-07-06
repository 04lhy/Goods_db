#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace goods_db {

/**
 * RID (Row Identifier) - uniquely identifies a tuple in a table.
 *
 * Structure:
 *   - page_id (4 bytes): the page number within the table's file
 *   - slot_id (2 bytes): the slot number within the page
 */
struct RID {
    int32_t page_id{0};
    int16_t slot_id{0};

    RID() = default;
    RID(int32_t page_id, int16_t slot_id) : page_id(page_id), slot_id(slot_id) {}

    bool operator==(const RID& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }

    bool operator!=(const RID& other) const { return !(*this == other); }

    bool operator<(const RID& other) const {
        if (page_id != other.page_id) return page_id < other.page_id;
        return slot_id < other.slot_id;
    }

    std::string ToString() const {
        return "RID(" + std::to_string(page_id) + ", " + std::to_string(slot_id) + ")";
    }

    struct Hash {
        size_t operator()(const RID& rid) const {
            return std::hash<int32_t>()(rid.page_id) ^
                   (std::hash<int16_t>()(rid.slot_id) << 1);
        }
    };
};

}  // namespace goods_db
