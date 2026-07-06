#include "buffer/lru_k_replacer.h"
#include <algorithm>
#include "common/logger.h"

namespace goods_db {

LRUKReplacer::LRUKReplacer(size_t /*num_frames*/, size_t k)
    : k_(k) {}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
    curr_timestamp_++;

    auto& history = access_history_[frame_id];
    history.push_front(curr_timestamp_);

    // Keep only the last K accesses
    if (history.size() > k_) {
        history.pop_back();
    }
}

size_t LRUKReplacer::GetKDistance(frame_id_t frame_id) {
    auto it = access_history_.find(frame_id);
    if (it == access_history_.end() || it->second.empty()) {
        return std::numeric_limits<size_t>::max();
    }

    if (it->second.size() < k_) {
        return std::numeric_limits<size_t>::max();  // Infinite distance (not enough history)
    }

    // k-distance = current_timestamp - k-th most recent access
    size_t kth_access = it->second.back();
    return curr_timestamp_ - kth_access;
}

bool LRUKReplacer::Victim(frame_id_t* frame_id) {
    std::lock_guard<std::mutex> lock(latch_);

    if (curr_size_ == 0) return false;

    // Find the evictable frame with the largest k-distance
    frame_id_t victim = INVALID_FRAME_ID;
    size_t max_distance = 0;
    bool found = false;

    for (const auto& [fid, evictable] : evictable_) {
        if (!evictable) continue;

        size_t distance = GetKDistance(fid);
        if (!found || distance > max_distance) {
            victim = fid;
            max_distance = distance;
            found = true;
        }
    }

    if (!found) return false;

    // Remove victim from tracking
    evictable_.erase(victim);
    access_history_.erase(victim);
    curr_size_--;

    *frame_id = victim;
    return true;
}

void LRUKReplacer::Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);

    RecordAccess(frame_id);
    auto it = evictable_.find(frame_id);
    if (it != evictable_.end()) {
        evictable_.erase(it);
        curr_size_--;
    }
}

void LRUKReplacer::Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);

    if (evictable_.find(frame_id) != evictable_.end()) return;  // Already evictable

    RecordAccess(frame_id);
    evictable_[frame_id] = true;
    curr_size_++;
}

}  // namespace goods_db
