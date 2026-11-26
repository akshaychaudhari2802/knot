// src/lock_decoder.cpp
#include "knot/lock.hpp"

#include <cstring>

namespace knot {

namespace {
std::int32_t i32(const unsigned char* p) { std::int32_t v; std::memcpy(&v, p, 4); return v; }
std::uint32_t u32(const unsigned char* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
}  // namespace

// Two INDEPENDENT fields must agree before we believe it is a mutex:
//   __kind has only known bits, AND the futex word is consistent with "locked"
//   (1 or 2 for a normal mutex; for PI/robust the word itself carries the owner TID).
// Note what is NOT checked: that the owner thread exists. A missing owner is an
// ORPHANED LOCK, which is a finding. (v1's design rejected it and would have hidden the bug.)
std::optional<DecodedLock> LockDecoder::try_mutex(std::uint64_t base) const {
    unsigned char raw[40];
    if (!src_.read_memory(base, raw, sizeof raw)) return std::nullopt;
    const std::int32_t lock = i32(raw + 0);
    const std::int32_t owner = i32(raw + 8);
    const std::uint32_t kind = u32(raw + 16);

    constexpr std::uint32_t kKnownBits = 0x03 | 0x10 | 0x20 | 0x40 | 0x80;
    if ((kind & ~kKnownBits) != 0 || (kind & 0x03) == 0x03) return std::nullopt;
    if (owner <= 0) return std::nullopt;
    const bool pi_or_robust = (kind & (0x10 | 0x20)) != 0;
    const bool lock_ok = pi_or_robust ? (lock & 0x3FFFFFFF) == owner   // second witness
                                      : (lock == 1 || lock == 2);        // 2 = locked with waiters
    if (!lock_ok) return std::nullopt;

    DecodedLock d;
    d.kind = LockKind::Mutex;
    d.addr = d.futex_addr = base;
    d.owner_ns_tid = owner;
    d.kind_flags = kind;
    d.pi = (kind & 0x20) != 0;
    d.robust = (kind & 0x10) != 0;
    d.recursive = (kind & 0x03) == 1;
    d.process_shared = (kind & 0x80) != 0;
    return d;
}

// glibc >= 2.25 rwlock. We can only name the WRITER holding it; readers are
// counted in __readers but their identities are not recorded anywhere.
std::optional<DecodedLock> LockDecoder::try_rwlock(std::uint64_t base, std::uint64_t futex_addr) const {
    unsigned char raw[56];
    if (!src_.read_memory(base, raw, sizeof raw)) return std::nullopt;
    const std::uint32_t readers = u32(raw + 0);
    const std::uint32_t futex_word = u32(raw + (futex_addr - base));
    const std::int32_t cur_writer = i32(raw + 24);
    const std::int32_t shared = i32(raw + 28);
    const std::uint32_t flags = u32(raw + 48);
    if (cur_writer <= 0) return std::nullopt;                    // no writer to blame
    if (shared != 0 && shared != 1) return std::nullopt;         // PTHREAD_PROCESS_* only
    if (flags > 2) return std::nullopt;                          // PTHREAD_RWLOCK_PREFER_* only
    // Two more witnesses from glibc's protocol (nptl/pthread_rwlock_common.c):
    // a writer holds it => __readers has PTHREAD_RWLOCK_WRLOCKED (2) set, and the
    // futex word being waited on has bit 0 set (write phase / writer owns).
    // Bit 1 is PTHREAD_RWLOCK_FUTEX_USED, set once anyone has slept: a stuck
    // rwlock reads 3 here (verified live), so mask it out.
    if ((readers & 2u) == 0) return std::nullopt;
    if ((futex_word & ~2u) != 1u) return std::nullopt;
    DecodedLock d;
    d.kind = LockKind::RwLockWrite;
    d.addr = base;
    d.futex_addr = futex_addr;
    d.owner_ns_tid = cur_writer;
    d.process_shared = shared == 1;
    d.reader_wait = (futex_addr == base + 8);
    return d;
}

DecodedLock LockDecoder::decode(std::uint64_t futex_addr) const {
    if (auto m = try_mutex(futex_addr)) return *m;
    // Verified live: blocked readers sleep on rwlock+8, blocked writers on rwlock+12.
    // So the rwlock starts BEFORE the futex address.
    for (std::uint64_t back : {8u, 12u})
        if (futex_addr >= back)
            if (auto r = try_rwlock(futex_addr - back, futex_addr)) return *r;
    DecodedLock unknown;
