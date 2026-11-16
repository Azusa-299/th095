#include "crypto.hpp"

#include <algorithm>

namespace th95::crypto {

void decrypt(std::vector<uint8_t> &buf, uint8_t key, uint8_t step, std::size_t block,
             std::size_t limit)
{
    const std::size_t size = buf.size();

    if (size == 0 || block < 2) {
        return;
    }

    const std::size_t rem = size % block;
    const std::size_t tail = (rem < block / 4) ? rem : 0;
    // The original uses a signed remaining-byte count. A small odd payload
    // entirely inside the untouched tail gives -1 and must skip the loop.
    // Unsigned subtraction here wrapped and wrote beyond small buffers.
    const std::size_t skipped = tail + (size & 1);
    if (skipped >= size || limit == 0) {
        return;
    }
    const std::size_t keep = std::min(limit, size);
    const std::vector<uint8_t> src(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(keep));

    std::size_t srcPos = 0;
    std::size_t span = size - skipped;
    std::size_t pos = 0;
    std::size_t room = limit;

    while (span > 0 && room > 0) {
        if (span < block) {
            block = span;
        }
        if (block < 2) {
            break;
        }

        std::size_t w = pos + block - 1;
        for (std::size_t i = 0; i < (block + 1) / 2 && srcPos < keep; ++i) {
            buf[w] = static_cast<uint8_t>(key ^ src[srcPos]);
            w -= 2;
            key = static_cast<uint8_t>(key + step);
            ++srcPos;
        }

        w = pos + block - 2;
        for (std::size_t i = 0; i < block / 2 && srcPos < keep; ++i) {
            buf[w] = static_cast<uint8_t>(key ^ src[srcPos]);
            w -= 2;
            key = static_cast<uint8_t>(key + step);
            ++srcPos;
        }

        span -= block;
        pos += block;
        room = room > block ? room - block : 0;
    }
}

void encrypt(std::vector<uint8_t> &buf, uint8_t key, uint8_t step, std::size_t block,
             std::size_t limit)
{
    const std::size_t size = buf.size();

    if (size == 0 || block < 2) {
        return;
    }

    const std::size_t rem = size % block;
    const std::size_t tail = (rem < block / 4) ? rem : 0;
    const std::size_t skipped = tail + (size & 1);
    if (skipped >= size || limit == 0) {
        return;
    }
    const std::size_t keep = std::min(limit, size);
    const std::vector<uint8_t> src(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(keep));

    std::size_t span = size - skipped;
    std::size_t pos = 0;
    std::size_t room = limit;
    std::size_t written = 0;

    while (span > 0 && room > 0) {
        if (span < block) {
            block = span;
        }
        if (block < 2) {
            break;
        }
        // Odd offsets of the block first, then the even ones, while the
        // destination advances one byte at a time.
        for (std::size_t i = 0; i < (block + 1) / 2 && written < keep; ++i) {
            buf[written++] = static_cast<uint8_t>(key ^ src[pos + block - 1 - 2 * i]);
            key = static_cast<uint8_t>(key + step);
        }
        for (std::size_t i = 0; i < block / 2 && written < keep; ++i) {
            buf[written++] = static_cast<uint8_t>(key ^ src[pos + block - 2 - 2 * i]);
            key = static_cast<uint8_t>(key + step);
        }
        span -= block;
        pos += block;
        room = room > block ? room - block : 0;
    }
}

} // namespace th95::crypto
