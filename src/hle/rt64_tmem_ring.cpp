//
// RT64
//
// Always-on TMEM-load ring buffer. See rt64_tmem_ring.hpp for rationale.
//
// Copyright (c) 2026 Matthew Stanley
//

#include "rt64_tmem_ring.hpp"

#include <cstdlib>

namespace RT64 {

    std::atomic<uint64_t> g_tmemRingWriteIdx{0};
    TmemRingEntry g_tmemRingEntries[kTmemRingCapacity];

    // FNV-1a over up to `cap` bytes of RDRAM starting at srcAddr. RDRAM is
    // stored word-swapped on the host (per-byte XOR-3 within each 32-bit
    // word); we walk that swizzle so the hash matches the logical N64 byte
    // order and is stable regardless of host endianness. A texture never
    // exceeds TMEM (4 KiB), so the cap bounds cost and avoids walking into
    // unrelated memory on a bogus span.
    static uint64_t hashSource(const uint8_t *rdram, uint32_t srcAddr, uint32_t srcBytes) {
        if (rdram == nullptr || srcBytes == 0) return 0;
        constexpr uint32_t kCap = 4096;
        const uint32_t n = srcBytes > kCap ? kCap : srcBytes;
        uint64_t h = 1469598103934665603ull; // FNV offset basis
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t b = rdram[(srcAddr + i) ^ 3];
            h ^= b;
            h *= 1099511628211ull; // FNV prime
        }
        return h;
    }

    void tmemRingPush(uint32_t op, uint32_t tile, uint32_t fmt, uint32_t siz,
                      uint32_t tmemAddr, uint32_t tmemBytes,
                      uint32_t srcAddr, uint32_t srcBytes, uint32_t imgAddr,
                      uint32_t width, uint32_t rows, uint32_t wordsPerRow,
                      const uint8_t *rdram) {
        static const bool s_enabled = []() {
            const char *env = std::getenv("RT64_TMEM_RING_DISABLE");
            return !(env != nullptr && env[0] != '\0' && env[0] != '0');
        }();
        if (!s_enabled) return;

        const uint64_t seq = g_tmemRingWriteIdx.fetch_add(1, std::memory_order_relaxed) + 1;
        TmemRingEntry &e = g_tmemRingEntries[seq & kTmemRingMask];
        e.op = op;
        e.tile = tile;
        e.fmt = fmt;
        e.siz = siz;
        e.tmem_addr = tmemAddr;
        e.tmem_bytes = tmemBytes;
        e.src_addr = srcAddr;
        e.src_bytes = srcBytes;
        e.img_addr = imgAddr;
        e.width = width;
        e.rows = rows;
        e.words_per_row = wordsPerRow;
        e.content_hash = hashSource(rdram, srcAddr, srcBytes);
        e.seq.store(seq, std::memory_order_release);
    }

    std::atomic<uint64_t> g_spriteRingWriteIdx{0};
    SpriteRingEntry g_spriteRingEntries[kSpriteRingCapacity];

    void spriteRingPush(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry,
                        uint32_t tile, uint32_t srcAddr) {
        static const bool s_enabled = []() {
            const char *env = std::getenv("RT64_TMEM_RING_DISABLE");
            return !(env != nullptr && env[0] != '\0' && env[0] != '0');
        }();
        if (!s_enabled) return;
        const uint64_t seq = g_spriteRingWriteIdx.fetch_add(1, std::memory_order_relaxed) + 1;
        SpriteRingEntry &e = g_spriteRingEntries[seq & kSpriteRingMask];
        e.ulx = ulx; e.uly = uly; e.lrx = lrx; e.lry = lry;
        e.tile = tile; e.src_addr = srcAddr;
        e.seq.store(seq, std::memory_order_release);
    }

} // namespace RT64

extern "C" uint64_t rt64_tmem_ring_write_idx(void) {
    return RT64::g_tmemRingWriteIdx.load(std::memory_order_relaxed);
}

extern "C" uint32_t rt64_tmem_ring_capacity(void) {
    return RT64::kTmemRingCapacity;
}

extern "C" int rt64_tmem_ring_read(uint64_t seq,
                                   uint64_t *out_seq,
                                   uint32_t out_u32[12],
                                   uint64_t *out_hash) {
    if (seq == 0) return 0;
    RT64::TmemRingEntry &e = RT64::g_tmemRingEntries[seq & RT64::kTmemRingMask];
    if (e.seq.load(std::memory_order_acquire) != seq) return 0;
    const uint32_t op = e.op, tile = e.tile, fmt = e.fmt, siz = e.siz;
    const uint32_t ta = e.tmem_addr, tb = e.tmem_bytes;
    const uint32_t sa = e.src_addr, sb = e.src_bytes, ia = e.img_addr;
    const uint32_t w = e.width, r = e.rows, wpr = e.words_per_row;
    const uint64_t hash = e.content_hash;
    if (e.seq.load(std::memory_order_acquire) != seq) return 0; // torn
    if (out_seq) *out_seq = seq;
    if (out_u32) {
        out_u32[0] = op;  out_u32[1] = tile; out_u32[2] = fmt; out_u32[3] = siz;
        out_u32[4] = ta;  out_u32[5] = tb;   out_u32[6] = sa;  out_u32[7] = sb;
        out_u32[8] = ia;  out_u32[9] = w;    out_u32[10] = r;  out_u32[11] = wpr;
    }
    if (out_hash) *out_hash = hash;
    return 1;
}

extern "C" uint64_t rt64_sprite_ring_write_idx(void) {
    return RT64::g_spriteRingWriteIdx.load(std::memory_order_relaxed);
}

extern "C" uint32_t rt64_sprite_ring_capacity(void) {
    return RT64::kSpriteRingCapacity;
}

extern "C" int rt64_sprite_ring_read(uint64_t seq, uint64_t *out_seq,
                                     int32_t out_i32[4], uint32_t out_u32[2]) {
    if (seq == 0) return 0;
    RT64::SpriteRingEntry &e = RT64::g_spriteRingEntries[seq & RT64::kSpriteRingMask];
    if (e.seq.load(std::memory_order_acquire) != seq) return 0;
    const int32_t ulx = e.ulx, uly = e.uly, lrx = e.lrx, lry = e.lry;
    const uint32_t tile = e.tile, src = e.src_addr;
    if (e.seq.load(std::memory_order_acquire) != seq) return 0;
    if (out_seq) *out_seq = seq;
    if (out_i32) { out_i32[0] = ulx; out_i32[1] = uly; out_i32[2] = lrx; out_i32[3] = lry; }
    if (out_u32) { out_u32[0] = tile; out_u32[1] = src; }
    return 1;
}
