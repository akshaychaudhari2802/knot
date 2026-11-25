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
