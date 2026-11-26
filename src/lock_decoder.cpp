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
