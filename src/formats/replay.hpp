#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace th95::format {

// Retail layout (see docs/replay-format.md):
//   36-byte container header, magic "t95r", then the doubly XOR-scrambled
//   LZSS stream. The decoded body starts with a 248-byte header, followed by
//   six input bytes per logic tick and one FPS byte per 30 ticks.
constexpr int kReplayNumberedSlots = 20;
constexpr int kReplaySlots = 80;

struct ReplayHeader {
    uint16_t sceneId = 0;      // body +0  global scene id
    uint8_t level = 0;         // body +2  raw level index (10 = EX)
    uint8_t scene = 0;         // body +3  raw scene index
    uint16_t seed = 1;         // body +4  logic seed
    std::string name;          // body +7  up to eight characters
    uint32_t timestamp = 0;    // body +16 written when the scene finishes
    uint32_t score = 0;        // body +20 written when the scene finishes
    std::array<uint8_t, 200> config{}; // body +24 config snapshot
    float rate = 0;            // body +224 100 - slow/total*100
    uint32_t inputLength = 0;  // body +240
    uint32_t trailerLength = 0;// body +244
};

struct Replay {
    ReplayHeader header;
    std::vector<uint8_t> input;   // three little-endian u16 per tick
    std::vector<uint8_t> trailer; // one signed byte per 30 ticks
};

struct ReplaySlot {
    bool exists = false; // retail only reports a slot whose header decoded
    std::filesystem::path path;
    ReplayHeader header;
};

// Reads and decodes one replay container. Returns false for bad magic,
// truncated payloads, or a decoded body shorter than its declared header.
bool readReplay(const std::filesystem::path &path, Replay &out);

// Writes the container in the retail order: LZSS, XOR with (61,122,128),
// then XOR with (170,225,1024). The optional "USER" metadata chunks follow
// the payload exactly like the original writer.
bool writeReplay(const std::filesystem::path &path, const Replay &replay,
                 const std::string &userName);

// Scans replay/th95_NN.rpy for slots 0..19 and replay/th95_ud????.rpy for the
// user slots. Like FindFirstFileA, exactly four characters must follow "ud",
// and slots are filled in directory-enumeration order (no sorting).
std::array<ReplaySlot, kReplaySlots> scanReplays(const std::filesystem::path &gameDirectory);

} // namespace th95::format
