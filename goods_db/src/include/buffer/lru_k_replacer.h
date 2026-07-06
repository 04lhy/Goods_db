#pragma once

#include <limits>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "buffer/replacer.h"

namespace goods_db {

/**
 * LRU-K Replacer.
 *
 * Implements the LRU-K algorithm: evicts the frame with the largest
 * "backward k-distance" (current timestamp - k-th most recent access timestamp).
 *
 * Frames accessed fewer than K times have infinite distance and are preferred
 * for eviction (less "established" pages). Among those with < K accesses,
 * standard LRU (based on earliest/first access) is used.
 *
 * This improves over simple LRU by accounting for access frequency patterns.
 */
class LRUKReplacer : public Replacer {
public:
    /**
     * @param num_frames Total number of frames in the buffer pool
     * @param k The K value for LRU-K (typically 2)
     */
    explicit LRUKReplacer(size_t num_frames, size_t k = 2);

    bool Victim(frame_id_t* frame_id) override;

    void Pin(frame_id_t frame_id) override;

    void Unpin(frame_id_t frame_id) override;

    size_t Size() override { return curr_size_; }

    std::string Name() const override { return "LRU-K"; }

private:
    /** Record a frame access (for tracking K most recent accesses) */
    void RecordAccess(frame_id_t frame_id);

    /** Get the backward k-distance for a frame */
    size_t GetKDistance(frame_id_t frame_id);

    size_t k_;

    /** Access history: frame_id → list of timestamps (most recent first) */
    std::unordered_map<frame_id_t, std::list<size_t>> access_history_;

    /** Evictable frames set */
    std::unordered_map<frame_id_t, bool> evictable_;

    size_t curr_size_{0};
    size_t curr_timestamp_{0};

    mutable std::mutex latch_;
};

}  // namespace goods_db
