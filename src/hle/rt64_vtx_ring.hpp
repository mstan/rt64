//
// RT64
//
// Always-on triangle vertex ring buffer. Snapshots every triangle's
// 3 post-MVP / post-divide screen-space positions at drawIndexedTri
// time, plus a monotonic sequence number. Lock-free single-writer
// (gfx thread) multi-reader (debug server). Used to answer "do
// adjacent quads at the same world position produce identical
// screen-space coords?" without RenderDoc or per-bug instrumentation.
//
// Disable at runtime with RT64_VTX_RING_DISABLE=1.
//
// Copyright (c) 2026 Matthew Stanley
//

#ifndef RT64_VTX_RING_HPP
#define RT64_VTX_RING_HPP

#include <atomic>
#include <cstdint>

namespace RT64 {

    constexpr uint32_t kVtxRingCapacity = 16384; // power of two
    constexpr uint64_t kVtxRingMask = kVtxRingCapacity - 1;

    // Slot layout: position floats first (written by writer in step 3),
    // seq atomic last (written in step 4 with release). Readers acquire
    // seq before reading positions and re-acquire after, rejecting on
    // mismatch to detect mid-write tears.
    struct VtxRingEntry {
        float v0_x, v0_y, v0_z;
        float v1_x, v1_y, v1_z;
        float v2_x, v2_y, v2_z;
        std::atomic<uint64_t> seq;
    };

    extern std::atomic<uint64_t> g_vtxRingWriteIdx;
    extern VtxRingEntry g_vtxRingEntries[kVtxRingCapacity];

    // Push one triangle (3 screen-space positions in N64-pixel units —
    // i.e., post-viewport-scale, the same value RT64's drawRect logic
    // reads from posScreen). Single-writer hot path. Reads
    // RT64_VTX_RING_DISABLE env var lazily on first call; subsequent
    // calls are a cheap branch on the cached bool.
    void vtxRingPush(float v0x, float v0y, float v0z,
                     float v1x, float v1y, float v1z,
                     float v2x, float v2y, float v2z);

} // namespace RT64

extern "C" {

// Public C ABI for debug_server consumption. write_idx is the most
// recent sequence number written; idx values up to write_idx are
// candidate slots (older entries get overwritten as the ring wraps).
uint64_t rt64_vtx_ring_write_idx(void);
uint32_t rt64_vtx_ring_capacity(void);

// Reads slot for the given sequence number. Returns 1 if a coherent
// read succeeded (slot still held the requested seq before and after
// reading positions); 0 if torn or overwritten. out_floats[0..8] are
// (v0_x, v0_y, v0_z, v1_x, v1_y, v1_z, v2_x, v2_y, v2_z).
int rt64_vtx_ring_read(uint64_t seq,
                       uint64_t* out_seq,
                       float out_floats[9]);

} // extern "C"

#endif // RT64_VTX_RING_HPP
