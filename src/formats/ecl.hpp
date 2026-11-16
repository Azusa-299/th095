#pragma once

#include <cstdint>
#include <vector>

namespace th95::format {

struct EclCommand {
    uint32_t time = 0;
    uint16_t opcode = 0;
    uint16_t size = 0;
    uint16_t rank = 0, mask = 0;
    uint32_t offset = 0;
    std::vector<uint8_t> args;
    int integer(std::size_t index) const;
    float number(std::size_t index) const;

    bool terminator() const { return time == UINT32_MAX && opcode == UINT16_MAX; }
};

struct EclScript {
    uint32_t offset = 0;
    std::vector<EclCommand> commands;
};

struct EclFile {
    uint16_t scriptCount = 0;
    uint16_t pointerCount = 0;
    std::vector<EclScript> scripts;
    std::vector<EclCommand> timeline;

    bool parse(const std::vector<uint8_t> &data);
};

struct StdRecord {
    uint32_t type = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct StdSegment {
    uint32_t offset = 0;
    std::vector<uint8_t> header;
    std::vector<StdRecord> records;
};

struct StdFile {
    uint16_t segmentCount = 0;
    std::vector<StdSegment> segments;

    bool parse(const std::vector<uint8_t> &data);
};

} // namespace th95::format
