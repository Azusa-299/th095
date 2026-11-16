#pragma once
// Platform-independent numeric semantics for the reimplementation.
//
// The retail build targeted x87: game state was stored as 32-bit float, but
// every intermediate expression was evaluated at 80-bit precision and rounded
// only when written back to memory. Reproducing that split is required before
// frame hashes can be compared with the original, so gameplay code must not
// use bare float/double operators for state transitions.
//
// Everything here is deterministic and free of Win32 or graphics headers.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace th95::numeric {

using f80 = long double;
static_assert(std::numeric_limits<f80>::digits >= 64,
              "th95 requires an 80-bit long double (GCC/Clang on x86); a 53-bit "
              "double would silently lose the x87 extended-precision contract");
constexpr f80 kPi = 3.14159265358979323846264338327950288L;
constexpr f80 kTwoPi = 6.28318530717958647692528676655900576L;

// ---------------------------------------------------------------------------
// Storage rounding
// ---------------------------------------------------------------------------

inline float narrow(f80 value) { return static_cast<float>(value); }
inline double narrow64(f80 value) { return static_cast<double>(value); }
inline f80 wide(float value) { return static_cast<f80>(value); }
inline f80 wide(double value) { return static_cast<f80>(value); }

// ---------------------------------------------------------------------------
// Arithmetic: evaluate at 80-bit, round once at the store boundary
// ---------------------------------------------------------------------------

inline float add(float a, float b) { return narrow(wide(a) + wide(b)); }
inline float sub(float a, float b) { return narrow(wide(a) - wide(b)); }
inline float mul(float a, float b) { return narrow(wide(a) * wide(b)); }
inline float div(float a, float b) { return narrow(wide(a) / wide(b)); }
inline float neg(float a) { return narrow(-wide(a)); }
inline float add3(float a, float b, float c) { return narrow(wide(a) + wide(b) + wide(c)); }
// x87 has no fused multiply-add: a*b+c keeps one 80-bit chain and rounds once.
inline float mulAdd(float a, float b, float c) { return narrow(wide(a) * wide(b) + wide(c)); }

inline float abs(float v) { return narrow(std::fabs(wide(v))); }
inline float min(float a, float b) { return a < b ? a : b; }
inline float max(float a, float b) { return a > b ? a : b; }
inline float clamp(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
inline bool finite(float v) { return std::isfinite(wide(v)); }

// ---------------------------------------------------------------------------
// Transcendentals at extended precision, rounded to the stored width
// ---------------------------------------------------------------------------

inline float sin(float v) { return narrow(std::sin(wide(v))); }
inline float cos(float v) { return narrow(std::cos(wide(v))); }
inline float tan(float v) { return narrow(std::tan(wide(v))); }
inline float atan2(float y, float x) { return narrow(std::atan2(wide(y), wide(x))); }
inline float asin(float v) { return narrow(std::asin(wide(v))); }
inline float acos(float v) { return narrow(std::acos(wide(v))); }
inline float sqrt(float v) { return narrow(std::sqrt(wide(v))); }
inline float pow(float base, float exponent) {
    return narrow(std::pow(wide(base), wide(exponent)));
}
inline float hypot(float a, float b) {
    const f80 x = wide(a), y = wide(b);
    return narrow(std::sqrt(x * x + y * y));
}
inline float remainder(float v, float divisor) {
    return narrow(std::remainder(wide(v), wide(divisor)));
}
inline float floorValue(float v) { return narrow(std::floor(wide(v))); }
inline float ceilValue(float v) { return narrow(std::ceil(wide(v))); }

// Retail angle normalisation: both VM families keep headings in [-pi, pi].
inline float wrapAngle(float angle) {
    f80 a = wide(angle);
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return narrow(a);
}
inline float lerp(float from, float to, float t) {
    return narrow(wide(from) + (wide(to) - wide(from)) * wide(t));
}

// ---------------------------------------------------------------------------
// Float-to-integer conversions
// ---------------------------------------------------------------------------

// x87 FISTP without a control-word change rounds to nearest, ties to even.
inline int32_t roundToInt(float v) {
    const f80 x = std::nearbyint(wide(v));
    if (!(x >= -2147483648.0L && x <= 2147483647.0L)) return INT32_MIN;
    return static_cast<int32_t>(x);
}
// C-style conversion used by the retail code: truncation toward zero.
inline int32_t truncToInt(float v) {
    const f80 x = std::trunc(wide(v));
    if (!(x >= -2147483648.0L && x <= 2147483647.0L)) return INT32_MIN;
    return static_cast<int32_t>(x);
}
inline int32_t floorToInt(float v) {
    const f80 x = std::floor(wide(v));
    if (!(x >= -2147483648.0L && x <= 2147483647.0L)) return INT32_MIN;
    return static_cast<int32_t>(x);
}
inline int32_t ceilToInt(float v) {
    const f80 x = std::ceil(wide(v));
    if (!(x >= -2147483648.0L && x <= 2147483647.0L)) return INT32_MIN;
    return static_cast<int32_t>(x);
}
// Fractional part with the sign of the input (retail fprem behaviour).
inline float fractional(float v) { return narrow(wide(v) - std::trunc(wide(v))); }
// Two-decimal fixed-point step used by the player position clamp.
inline float quantizeHundredths(float v) {
    return narrow(std::floor(wide(v) * 100.0L) / 100.0L);
}

// ---------------------------------------------------------------------------
// Integer wraparound
// ---------------------------------------------------------------------------

inline int32_t wrapAdd(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
inline int32_t wrapSub(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}
inline int32_t wrapMul(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
inline int32_t wrapNeg(int32_t a) { return static_cast<int32_t>(0u - static_cast<uint32_t>(a)); }
inline int16_t wrapAdd16(int16_t a, int16_t b) {
    return static_cast<int16_t>(static_cast<uint16_t>(
        static_cast<uint16_t>(a) + static_cast<uint16_t>(b)));
}
inline uint16_t wrapAddU16(uint16_t a, uint16_t b) { return uint16_t(a + b); }
inline uint16_t wrapSubU16(uint16_t a, uint16_t b) { return uint16_t(a - b); }

// ---------------------------------------------------------------------------
// Signed/unsigned comparison order
// ---------------------------------------------------------------------------

inline bool belowUnsigned(int32_t a, int32_t b) {
    return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
}
inline bool aboveUnsigned(int32_t a, int32_t b) {
    return static_cast<uint32_t>(a) > static_cast<uint32_t>(b);
}
inline bool belowSigned(int32_t a, int32_t b) { return a < b; }
inline bool aboveSigned(int32_t a, int32_t b) { return a > b; }

// ---------------------------------------------------------------------------
// Retail 16-bit seed rotation (ECL and ANM share the same primitive)
// ---------------------------------------------------------------------------

inline uint16_t rotateSeed16(uint16_t seed) {
    const uint16_t x = static_cast<uint16_t>((seed ^ 0x9630u) - 25939u);
    return static_cast<uint16_t>((static_cast<uint32_t>(x) << 2) | (x >> 14));
}
inline uint32_t combineSeeds(uint32_t high, uint32_t low) {
    return (high << 16) | (low & 0xFFFFu);
}
inline float unitFromUint32(uint32_t value) {
    return narrow(static_cast<f80>(value) / 4294967296.0L);
}

} // namespace th95::numeric
