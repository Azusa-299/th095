#pragma once
// Bullet presentation catalog shared by the simulation and the renderer.
// The script id selects a bullet.anm animation, the layer places it among the
// playfield passes and the radius is the collision radius used by the ECL VM.

#include <algorithm>
#include <cstddef>

namespace th95::scene {

inline constexpr int kBulletScripts[24] = {
    0, 16, 32, 48, 64, 80, 96, 111, 127, 143, 159, 175,
    211, 219, 227, 235, 243, 283, 296, 251, 259, 267, 275, 191
};
inline constexpr int kBulletLayers[24] = {
    5, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 1, 2, 1, 2, 2, 0, 2, 2, 2, 1, 2, 3
};
inline constexpr float kBulletRadii[24] = {
    4, 6, 6, 4, 4, 4, 4, 4, 4, 0, 4, 6, 10, 8, 8, 8, 8, 28, 6, 10, 8, 8, 8, 6
};

inline int bulletType(float value) { return std::clamp(static_cast<int>(value), 0, 23); }

inline int bulletScript(int type, int color)
{
    return kBulletScripts[bulletType(static_cast<float>(type))] + color;
}
inline int bulletLayer(int type) { return kBulletLayers[bulletType(static_cast<float>(type))]; }
inline float bulletRadius(int type) { return kBulletRadii[bulletType(static_cast<float>(type))]; }

} // namespace th95::scene
