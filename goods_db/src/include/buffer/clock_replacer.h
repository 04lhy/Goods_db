#pragma once

#include <mutex>
#include <vector>
#include "buffer/replacer.h"

namespace goods_db {

/**
 * Clock Replacer (Second Chance Algorithm).
 *
 * Maintains a circular buffer of frames with reference bits.
 * The clock hand sweeps through frames:
 *   - If ref_bit == 0: evict this frame
 *   - If ref_bit == 1: clear it and move to next frame
 *
 * Simple, low-overhead replacement policy. Good for scan-heavy workloads.
 */
class ClockReplacer : public Replacer {
public:
    explicit ClockReplacer(size_t num_frames);

    bool Victim(frame_id_t* frame_id) override;

    void Pin(frame_id_t frame_id) override;

    void Unpin(frame_id_t frame_id) override;

    size_t Size() override { return size_; }

    std::string Name() const override { return "Clock"; }

private:
    struct FrameState {
        bool evictable{false};
        bool ref_bit{false};
    };

    size_t num_frames_;
    std::vector<FrameState> frames_;
    size_t clock_hand_{0};
    size_t size_{0};

    mutable std::mutex latch_;
};

}  // namespace goods_db
