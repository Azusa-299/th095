#include "lzss.hpp"

#include <array>

namespace th95::lzss {
namespace {

class BitReader {
public:
    BitReader(const std::vector<uint8_t> &src) : m_src(src) {}

    int bit()
    {
        if (m_nbits == 0) {
            if(m_pos >= m_src.size()) {m_exhausted=true;return 0;}
            m_bits = m_src[m_pos++];
            m_nbits = 8;
        }
        const int value = (m_bits >> 7) & 1;
        m_bits = static_cast<uint8_t>(m_bits << 1);
        --m_nbits;
        return value;
    }

    uint32_t bits(int count)
    {
        uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            value = (value << 1) | static_cast<uint32_t>(bit());
        }
        return value;
    }

    bool exhausted() const {return m_exhausted;}

private:
    const std::vector<uint8_t> &m_src;
    std::size_t m_pos = 0;
    uint8_t m_bits = 0;
    int m_nbits = 0;
    bool m_exhausted = false;
};

} // namespace

std::vector<uint8_t> decode(const std::vector<uint8_t> &src, std::size_t outSize)
{
    std::vector<uint8_t> out;
    if (src.empty() || outSize == 0) {
        return out;
    }
    out.reserve(outSize);

    // retail routine consumes ONE stream. A zero dictionary address terminates it.
    // The previous segment hypothesis masked over-decryption beyond the keyed
    // prefix. Do not restart after a terminator or synthesize bits past EOF.
    std::array<uint8_t, 0x2000> ring{};
    std::size_t rp = 1;
    BitReader reader(src);

    while (out.size() < outSize) {
            if (reader.bit() == 1) {
                const auto literal = static_cast<uint8_t>(reader.bits(8));
                if(reader.exhausted())break;
                out.push_back(literal);
                ring[rp] = literal;
                rp = (rp + 1) & 0x1FFF;
                continue;
            }

            const uint32_t dist = reader.bits(13);
            if (dist == 0 || reader.exhausted()) {
                break;
            }
            const uint32_t length = reader.bits(4);
            if(reader.exhausted())break;
            if(length+3>outSize-out.size())return {};
            for (uint32_t i = 0; i <= length + 2 && out.size() < outSize; ++i) {
                const uint8_t byte = ring[(dist + i) & 0x1FFF];
                out.push_back(byte);
                ring[rp] = byte;
                rp = (rp + 1) & 0x1FFF;
            }
    }

    return out;
}

std::vector<uint8_t> encode(const std::vector<uint8_t> &src)
{
    std::vector<uint8_t> out;
    if (src.empty()) {
        return out;
    }

    constexpr int kMaxMatch = 18, kMinMatch = 3, kHashBits = 12;
    constexpr int kChainBudget = 96;
    constexpr uint32_t kHashMask = (1u << kHashBits) - 1;

    std::vector<uint8_t> bits;
    bits.reserve(src.size() + src.size() / 4 + 8);
    uint32_t accumulator = 0;
    int pending = 0;
    const auto put = [&](uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            accumulator = (accumulator << 1) | ((value >> i) & 1u);
            if (++pending == 8) {
                bits.push_back(static_cast<uint8_t>(accumulator));
                accumulator = 0;
                pending = 0;
            }
        }
    };

    std::vector<int> head(std::size_t(1) << kHashBits, -1);
    std::vector<int> previous(src.size(), -1);
    const auto hashAt = [&](std::size_t p) {
        return int(((uint32_t(src[p]) << 8) ^ (uint32_t(src[p + 1]) << 4) ^ uint32_t(src[p + 2])) & kHashMask);
    };

    std::size_t position = 0, inserted = 0;
    while (position < src.size()) {
        while (inserted < position) {
            if (inserted + 2 < src.size()) {
                const int hash = hashAt(inserted);
                previous[inserted] = head[std::size_t(hash)];
                head[std::size_t(hash)] = int(inserted);
            }
            ++inserted;
        }
        int bestLength = 0, bestSource = -1;
        if (position + 2 < src.size()) {
            int candidate = head[std::size_t(hashAt(position))];
            for (int budget = kChainBudget; candidate >= 0 && budget > 0; --budget) {
                const auto at = std::size_t(candidate);
                // The decoder keeps only the last 0x2000 bytes, so a source
                // further back has already been overwritten. A zero ring
                // position would also terminate the stream.
                if (at < position && position - at <= 0x1FFF && ((at + 1) & 0x1FFF) != 0) {
                    std::size_t length = 0;
                    while (length < kMaxMatch && position + length < src.size() &&
                           src[at + length] == src[position + length]) {
                        ++length;
                    }
                    if (int(length) > bestLength) {
                        bestLength = int(length);
                        bestSource = candidate;
                        if (bestLength == kMaxMatch) {
                            break;
                        }
                    }
                }
                candidate = previous[at];
            }
        }
        const std::size_t remaining = src.size() - position;
        if (bestSource >= 0 && bestLength >= kMinMatch && std::size_t(bestLength) <= remaining) {
            put(0, 1);
            put(uint32_t((std::size_t(bestSource) + 1) & 0x1FFF), 13);
            put(uint32_t(bestLength - kMinMatch), 4);
            position += std::size_t(bestLength);
        } else {
            put(1, 1);
            put(src[position], 8);
            ++position;
        }
    }
    if (pending > 0) {
        bits.push_back(static_cast<uint8_t>(accumulator << (8 - pending)));
    }
    return bits;
}

} // namespace th95::lzss
