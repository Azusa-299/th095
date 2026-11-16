#pragma once
// Machine-readable per-frame trace shared by the port and an original-side
// capture tool. Scalars are always present; event arrays are only emitted in
// verbose records so goldens stay small and diff-friendly.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace th95::sim {

struct SoundEvent {
    int id = 0;
    int count = 1;
    bool stop = false;
    float x = 0;
};

struct EffectEvent {
    int script = 0;
    unsigned follow = 0;
    float x = 0, y = 0;
};

struct FrameRecord {
    int frame = 0;
    uint64_t state = 0;
    uint64_t layout = 0;
    uint64_t audio = 0;
    uint64_t effects = 0;
    int score = 0, photos = 0, attempts = 0, enemies = 0, bullets = 0;
    std::vector<SoundEvent> sounds;
    std::vector<EffectEvent> effectList;
};

std::string toJsonLine(const FrameRecord &record, bool verbose = false);
bool parseJsonLine(const std::string &line, FrameRecord &record);

struct FrameDiff {
    int frame = 0;
    std::string field;
    std::string expected;
    std::string actual;
};

struct Comparison {
    int frames = 0;
    int mismatchedFrames = 0;
    int layoutMismatches = 0;
    int audioMismatches = 0;
    int effectMismatches = 0;
    std::vector<FrameDiff> details;
};

// Accumulates one golden/actual pair. detailLimit bounds the stored evidence
// while the counters still describe every mismatch.
void compareFrame(const FrameRecord &expected, const FrameRecord &actual,
                  Comparison &comparison, std::size_t detailLimit = 16);

} // namespace th95::sim
