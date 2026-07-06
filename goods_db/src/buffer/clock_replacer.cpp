#include "buffer/clock_replacer.h"
#include <algorithm>

namespace goods_db {

ClockReplacer::ClockReplacer(size_t num_frames)
    : num_frames_(num_frames), frames_(num_frames) {}

bool ClockReplacer::Victim(frame_id_t* frame_id) {
    std::lock_guard<std::mutex> lock(latch_);

    if (size_ == 0) return false;

    // Sweep the clock hand until we find an evictable frame
    size_t scanned = 0;
    while (scanned < num_frames_) {
        auto& frame = frames_[clock_hand_];
        if (frame.evictable) {
            if (!frame.ref_bit) {
                // Evict this frame
                frame.evictable = false;
                *frame_id = clock_hand_;
                size_--;
                clock_hand_ = (clock_hand_ + 1) % num_frames_;
                return true;
            } else {
                // Give it a second chance: clear ref_bit and move on
                frame.ref_bit = false;
            }
        }
        clock_hand_ = (clock_hand_ + 1) % num_frames_;
        scanned++;
    }

    return false;  // No evictable frames found
}

void ClockReplacer::Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= num_frames_) return;

    auto& frame = frames_[frame_id];
    if (frame.evictable) {
        frame.evictable = false;
        frame.ref_bit = true;
        size_--;
    }
}

void ClockReplacer::Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= num_frames_) return;

    auto& frame = frames_[frame_id];
    if (!frame.evictable) {
        frame.evictable = true;
        frame.ref_bit = true;
        size_++;
    }
}

}  // namespace goods_db
