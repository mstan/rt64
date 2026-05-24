//
// RT64
//
// Always-on triangle vertex ring buffer. See rt64_vtx_ring.hpp for the
// design rationale.
//
// Copyright (c) 2026 Matthew Stanley
//

#include "rt64_vtx_ring.hpp"

#include <cstdlib>

namespace RT64 {

    std::atomic<uint64_t> g_vtxRingWriteIdx{0};
    VtxRingEntry g_vtxRingEntries[kVtxRingCapacity];

    void vtxRingPush(float v0x, float v0y, float v0z,
                     float v1x, float v1y, float v1z,
                     float v2x, float v2y, float v2z) {
        // Read env var once at first call; cached in a function-local
        // static. Subsequent calls are a cheap branch on a bool.
        static const bool s_enabled = []() {
            const char* env = std::getenv("RT64_VTX_RING_DISABLE");
            return !(env != nullptr && env[0] != '\0' && env[0] != '0');
        }();
        if (!s_enabled) return;
        // Acquire a unique sequence number. fetch_add returns the
        // pre-increment value; we use the post-increment seq to keep
        // 0 reserved as "no entry yet" for readers.
        const uint64_t seq = g_vtxRingWriteIdx.fetch_add(1, std::memory_order_relaxed) + 1;
        VtxRingEntry& e = g_vtxRingEntries[seq & kVtxRingMask];
        // Step 3: write positions (single-writer, relaxed). Reader
        // bookends with acquire loads on seq to detect tears.
        e.v0_x = v0x; e.v0_y = v0y; e.v0_z = v0z;
        e.v1_x = v1x; e.v1_y = v1y; e.v1_z = v1z;
        e.v2_x = v2x; e.v2_y = v2y; e.v2_z = v2z;
        // Step 4: publish the new seq with release. Readers acquiring
        // this value are guaranteed to see the position writes above.
        e.seq.store(seq, std::memory_order_release);
    }

} // namespace RT64

extern "C" uint64_t rt64_vtx_ring_write_idx(void) {
    return RT64::g_vtxRingWriteIdx.load(std::memory_order_relaxed);
}

extern "C" uint32_t rt64_vtx_ring_capacity(void) {
    return RT64::kVtxRingCapacity;
}

extern "C" int rt64_vtx_ring_read(uint64_t seq,
                                  uint64_t* out_seq,
                                  float out_floats[9]) {
    if (seq == 0) return 0;
    RT64::VtxRingEntry& e = RT64::g_vtxRingEntries[seq & RT64::kVtxRingMask];
    // First acquire: validates the slot currently holds the requested seq.
    if (e.seq.load(std::memory_order_acquire) != seq) return 0;
    // Read positions while the slot is known to hold our seq.
    const float v0x = e.v0_x, v0y = e.v0_y, v0z = e.v0_z;
    const float v1x = e.v1_x, v1y = e.v1_y, v1z = e.v1_z;
    const float v2x = e.v2_x, v2y = e.v2_y, v2z = e.v2_z;
    // Second acquire: verifies no concurrent writer overwrote the slot
    // mid-read. If seq changed, the positions we just read are torn.
    if (e.seq.load(std::memory_order_acquire) != seq) return 0;
    if (out_seq) *out_seq = seq;
    if (out_floats) {
        out_floats[0] = v0x; out_floats[1] = v0y; out_floats[2] = v0z;
        out_floats[3] = v1x; out_floats[4] = v1y; out_floats[5] = v1z;
        out_floats[6] = v2x; out_floats[7] = v2y; out_floats[8] = v2z;
    }
    return 1;
}
