#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include "common/config.h"

namespace goods_db {

using frame_id_t = int32_t;

/**
 * Replacer - abstract base class for page replacement policies.
 *
 * A Replacer tracks which frames are evictable (unpinned) and
 * chooses a victim frame when the buffer pool is full.
 */
class Replacer {
public:
    Replacer() = default;
    virtual ~Replacer() = default;

    /**
     * Remove a victim frame from the replacer.
     * @param frame_id [out] The victim frame ID
     * @return true if a victim was found and removed
     */
    virtual bool Victim(frame_id_t* frame_id) = 0;

    /**
     * Pin a frame (mark as non-evictable).
     * Called when a page is fetched.
     */
    virtual void Pin(frame_id_t frame_id) = 0;

    /**
     * Unpin a frame (mark as evictable).
     * Called when a page's pin_count reaches 0.
     */
    virtual void Unpin(frame_id_t frame_id) = 0;

    /** Get number of evictable frames */
    virtual size_t Size() = 0;

    /** Get name of the replacement policy */
    virtual std::string Name() const = 0;
};

}  // namespace goods_db
