//
// RT64
//

#pragma once

#include <cassert>
#include <chrono>
#include <stdint.h>

namespace RT64 {
    typedef std::chrono::high_resolution_clock::time_point Timestamp;

    struct Timer {
        static void initialize();
        static Timestamp current();
        static int64_t deltaMicroseconds(const Timestamp t1, const Timestamp t2);
        // Blocks until endTime: coarse fixed-length sleeps cover the bulk of the wait, then a busy spin handles the final remainder.
        static void preciseSleepUntil(const Timestamp endTime);
    };
};