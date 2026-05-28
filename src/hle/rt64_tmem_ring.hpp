//
// RT64
//
// Always-on TMEM-load ring buffer. Snapshots every RDP texture-load
// operation (loadTile / loadBlock / loadTLUT) at the moment RT64 fills
// TMEM from RDRAM: the tile descriptor, the TMEM destination, the source
// RDRAM address + byte range, and a content hash of the loaded bytes.
//
// Purpose: diagnose the menu cursor/icon sprite corruption (different
// sprites garble on different visits — a stale-TMEM / texture-source-UAF
// fingerprint). With this ring we can answer, for the corrupting sprite:
//   - did its TMEM region get a load at all this frame? (stale TMEM)
//   - did the load read from a plausible / consistent source address, or
//     a varying/garbage one? (freed-source UAF)
//   - is the loaded content hash stable across visits, or does it change
//     when the sprite corrupts? (the smoking gun)
//
// Lock-free single-writer (gfx thread) multi-reader (debug server), same
// tear-detection contract as rt64_vtx_ring. Disable with
// RT64_TMEM_RING_DISABLE=1.
//
// Copyright (c) 2026 Matthew Stanley
//

#ifndef RT64_TMEM_RING_HPP
#define RT64_TMEM_RING_HPP

#include <atomic>
#include <cstdint>

namespace RT64 {

    constexpr uint32_t kTmemRingCapacity = 8192; // power of two
    constexpr uint64_t kTmemRingMask = kTmemRingCapacity - 1;

    // op codes
    enum TmemOp : uint32_t { TMEM_OP_TILE = 0, TMEM_OP_BLOCK = 1, TMEM_OP_TLUT = 2 };

    // Field order: payload first (single-writer, relaxed), seq atomic last
    // (release). Readers acquire seq, read payload, re-acquire seq and
    // reject on mismatch to catch mid-write tears.
    struct TmemRingEntry {
        uint32_t op;            // TmemOp
        uint32_t tile;          // tile descriptor index
        uint32_t fmt;           // G_IM_FMT_*
        uint32_t siz;           // G_IM_SIZ_*
        uint32_t tmem_addr;     // TMEM destination start (bytes)
        uint32_t tmem_bytes;    // bytes written to TMEM
        uint32_t src_addr;      // source RDRAM offset (textureStart)
        uint32_t src_bytes;     // source byte span (textureEnd - textureStart)
        uint32_t img_addr;      // G_SETTIMG base (loadTexture.address)
        uint32_t width;         // loadTexture.width
        uint32_t rows;          // row count loaded
        uint32_t words_per_row; // words per row
        uint64_t content_hash;  // FNV-1a over the source bytes (capped)
        std::atomic<uint64_t> seq;
    };

    extern std::atomic<uint64_t> g_tmemRingWriteIdx;
    extern TmemRingEntry g_tmemRingEntries[kTmemRingCapacity];

    // ── Sprite-draw ring ────────────────────────────────────────────
    // One entry per drawTexRect: the on-screen rectangle, the tile, and
    // the source RDRAM address of the texture it samples (the most-recent
    // recorded load operation). Lets us map a corrupt on-screen sprite
    // (by position) to its texture source — e.g. move the menu selection
    // and the cursor's rect moves while others stay fixed.
    constexpr uint32_t kSpriteRingCapacity = 4096; // power of two
    constexpr uint64_t kSpriteRingMask = kSpriteRingCapacity - 1;
    struct SpriteRingEntry {
        int32_t ulx, uly, lrx, lry; // screen rect (N64 pixel units)
        uint32_t tile;              // tile index used
        uint32_t src_addr;          // source RDRAM offset of the texture
        std::atomic<uint64_t> seq;
    };
    extern std::atomic<uint64_t> g_spriteRingWriteIdx;
    extern SpriteRingEntry g_spriteRingEntries[kSpriteRingCapacity];

    // Push one drawTexRect. `srcAddr` is the texture source (caller reads
    // it from the workload's most-recent load operation). Env-gated by the
    // same RT64_TMEM_RING_DISABLE.
    void spriteRingPush(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry,
                        uint32_t tile, uint32_t srcAddr);

    // Push one TMEM-load event. `rdram` is RT64's RDRAM base; the source
    // bytes [src_addr, src_addr+src_bytes) are hashed (capped at 4 KiB,
    // TMEM's size) for content identity. Single-writer hot path; env var
    // read lazily on first call.
    void tmemRingPush(uint32_t op, uint32_t tile, uint32_t fmt, uint32_t siz,
                      uint32_t tmemAddr, uint32_t tmemBytes,
                      uint32_t srcAddr, uint32_t srcBytes, uint32_t imgAddr,
                      uint32_t width, uint32_t rows, uint32_t wordsPerRow,
                      const uint8_t *rdram);

} // namespace RT64

extern "C" {

// Public C ABI for debug_server consumption, mirroring rt64_vtx_ring.
uint64_t rt64_tmem_ring_write_idx(void);
uint32_t rt64_tmem_ring_capacity(void);

// Reads the slot for `seq`. Returns 1 on a coherent read, 0 if torn /
// overwritten / seq==0. out_u32[0..11] = {op, tile, fmt, siz, tmem_addr,
// tmem_bytes, src_addr, src_bytes, img_addr, width, rows, words_per_row};
// out_hash = content_hash; out_seq = seq.
int rt64_tmem_ring_read(uint64_t seq,
                        uint64_t *out_seq,
                        uint32_t out_u32[12],
                        uint64_t *out_hash);

// Sprite-draw ring. out_i32[0..3] = {ulx,uly,lrx,lry}; out_u32[0..1] =
// {tile, src_addr}. Returns 1 on coherent read, 0 if torn / seq==0.
uint64_t rt64_sprite_ring_write_idx(void);
uint32_t rt64_sprite_ring_capacity(void);
int rt64_sprite_ring_read(uint64_t seq, uint64_t *out_seq,
                          int32_t out_i32[4], uint32_t out_u32[2]);

} // extern "C"

#endif // RT64_TMEM_RING_HPP
