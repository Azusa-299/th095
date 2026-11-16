#pragma once
// Deterministic frame hashing for the dynamic baseline.
//
// A record must be reproducible across machines, compilers and optimisation
// levels, so the hasher consumes the exact bit pattern of every stored float
// instead of a formatted decimal. FNV-1a 64 is used because it is simple,
// stable, and adequate for change detection.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace th95::sim {

class Hasher {
public:
    void bytes(const void *data, std::size_t size)
    {
        const auto *p = static_cast<const unsigned char *>(data);
        for (std::size_t i = 0; i < size; ++i) {
            m_state ^= p[i];
            m_state *= 0x100000001B3ull;
        }
    }
    void u8(uint8_t v) { bytes(&v, sizeof(v)); }
    void u16(uint16_t v) { bytes(&v, sizeof(v)); }
    void u32(uint32_t v) { bytes(&v, sizeof(v)); }
    void u64(uint64_t v) { bytes(&v, sizeof(v)); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
    void flag(bool v) { u8(v ? 1u : 0u); }
    void text(const char *value)
    {
        if (!value) return;
        bytes(value, std::strlen(value));
        u8(0);
    }
    uint64_t value() const { return m_state; }

private:
    uint64_t m_state = 0xCBF29CE484222325ull;
};

// Layout quantisation: the picture is determined by geometry, and
// differences below 1/256 world unit cannot change a rasterised pixel.
inline int32_t quantize(float v)
{
    const double scaled = static_cast<double>(v) * 256.0;
    if (!(scaled > -2147483648.0 && scaled < 2147483647.0)) return INT32_MIN;
    return static_cast<int32_t>(std::lround(scaled));
}

} // namespace th95::sim
