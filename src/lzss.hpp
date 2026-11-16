#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace th95::lzss {

/*
 * Port of retail routine (ZUN's LZSS variant), MSB-first bitstream with an 8K ring
 * dictionary addressed by absolute position:
 *   flag 1 -> 8-bit literal
 *   flag 0 -> 13-bit distance (0 terminates) + 4-bit length, copy length + 3
 *
 * Returns at most `outSize` bytes. A short result indicates truncation or an
 * early terminator; valid retail entries decode to exactly the declared size.
 */
std::vector<uint8_t> decode(const std::vector<uint8_t> &src, std::size_t outSize);

/*
 * Inverse of decode() for the save containers. Greedy three byte lookahead
 * over a bounded hash chain; the ring is addressed by absolute position, so a
 * match stores ((source position + 1) & 0x1FFF) as its distance.
 */
std::vector<uint8_t> encode(const std::vector<uint8_t> &src);

} // namespace th95::lzss
