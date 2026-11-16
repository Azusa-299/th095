#include "ecl.hpp"

#include <cstring>
#include <limits>

namespace th95::format {
namespace {

uint16_t read16(const std::vector<uint8_t> &data, std::size_t offset)
{
    return uint16_t(data[offset]) | uint16_t(data[offset + 1]) << 8;
}

uint32_t read32(const std::vector<uint8_t> &data, std::size_t offset)
{
    return uint32_t(data[offset]) | uint32_t(data[offset + 1]) << 8 |
           uint32_t(data[offset + 2]) << 16 | uint32_t(data[offset + 3]) << 24;
}

float readFloat(const std::vector<uint8_t> &data, std::size_t offset)
{
    const uint32_t bits = read32(data, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool range(const std::vector<uint8_t> &data, std::size_t offset, std::size_t size)
{
    return offset <= data.size() && size <= data.size() - offset;
}

} // namespace

int EclCommand::integer(std::size_t index) const
{return index<args.size()/4?static_cast<int32_t>(read32(args,index*4)):0;}
float EclCommand::number(std::size_t index) const
{return index<args.size()/4?readFloat(args,index*4):0;}

bool EclFile::parse(const std::vector<uint8_t> &data)
{
    scriptCount = 0;
    pointerCount = 0;
    scripts.clear();
    timeline.clear();
    if (!range(data, 0, 0x48) || read32(data, 0) != 0x800) {
        return false;
    }
    scriptCount = read16(data, 4);
    pointerCount = read16(data, 6);
    if (scriptCount == 0 || !range(data, 8, std::size_t(pointerCount) * 4) ||
        !range(data, 0x48, std::size_t(scriptCount) * 4)) {
        return false;
    }

    std::vector<uint32_t> offsets(scriptCount);
    for (uint16_t i = 0; i < scriptCount; ++i) {
        offsets[i] = read32(data, 0x48 + std::size_t(i) * 4);
        if (offsets[i] < 0x48 || offsets[i] >= data.size()) {
            scripts.clear();
            return false;
        }
        if (i != 0 && offsets[i] <= offsets[i - 1]) {
            scripts.clear();
            return false;
        }
    }

    scripts.reserve(scriptCount);
    for (uint16_t i = 0; i < scriptCount; ++i) {
        const std::size_t start = offsets[i];
        const std::size_t end = i + 1 < scriptCount ? offsets[i + 1] : data.size();
        EclScript script;
        script.offset = offsets[i];
        std::size_t cursor = start;
        bool terminated = false;
        while (cursor + 12 <= end) {
            const uint32_t time = read32(data, cursor);
            const uint16_t opcode = read16(data, cursor + 4);
            const uint16_t size = read16(data, cursor + 6);
            if (size < 12 || size > end - cursor) {
                scripts.clear();
                return false;
            }
            EclCommand command;
            command.time = time;
            command.opcode = opcode;
            command.size = size;
            command.offset=uint32_t(cursor);
            command.rank=read16(data,cursor+8);command.mask=read16(data,cursor+10);
            command.args.assign(data.begin() + cursor + 12, data.begin() + cursor + size);
            script.commands.push_back(std::move(command));
            cursor += size;
            if (time == UINT32_MAX && opcode == UINT16_MAX) {
                terminated = true;
                break;
            }
        }
        if (!terminated) {
            scripts.clear();
            return false;
        }
        scripts.push_back(std::move(script));
    }
    // retail routine: timeline uses a separate 8-byte header and byte size.
    if(pointerCount) {
        std::size_t cursor=read32(data,8);
        if(cursor>=data.size())return false;
        while(range(data,cursor,8)) {
            const auto time=read32(data,cursor);
            if(time==UINT32_MAX)break;
            const auto size=data[cursor+6];
            if(size<8||!range(data,cursor,size))return false;
            EclCommand c;c.time=time;c.opcode=read16(data,cursor+4);c.size=size;c.offset=uint32_t(cursor);
            c.args.assign(data.begin()+cursor+8,data.begin()+cursor+size);
            timeline.push_back(std::move(c));cursor+=size;
        }
    }
    return true;
}

bool StdFile::parse(const std::vector<uint8_t> &data)
{
    segmentCount = 0;
    segments.clear();
    if (!range(data, 0, 0x94)) {
        return false;
    }
    segmentCount = read16(data, 0);
    if (segmentCount == 0 || segmentCount > 32 ||
        !range(data, 0x90, std::size_t(segmentCount) * 4)) {
        return false;
    }

    // The segment offset table starts at 0x90 and holds one dword per segment,
    // so the first segment can begin right after it. world01 has five segments
    // (first offset 0xA4) but world02 has one (first offset 0x94).
    const std::size_t tableEnd = 0x90 + std::size_t(segmentCount) * 4;
    std::vector<uint32_t> offsets(segmentCount);
    for (uint16_t i = 0; i < segmentCount; ++i) {
        offsets[i] = read32(data, 0x90 + std::size_t(i) * 4);
        if (offsets[i] < tableEnd || offsets[i] >= data.size() ||
            (i != 0 && offsets[i] <= offsets[i - 1])) {
            segments.clear();
            return false;
        }
    }

    segments.reserve(segmentCount);
    for (uint16_t i = 0; i < segmentCount; ++i) {
        const std::size_t start = offsets[i];
        const std::size_t end = i + 1 < segmentCount ? offsets[i + 1] : data.size();
        if (start + 28 > end) {
            segments.clear();
            return false;
        }
        StdSegment segment;
        segment.offset = offsets[i];
        segment.header.assign(data.begin() + start, data.begin() + start + 28);
        for (std::size_t cursor = start + 28; cursor + 28 <= end; cursor += 28) {
            // STD records store a 16-bit kind followed by a 16-bit byte size.
            // The 0xffff/4 terminator is accepted as the end of a segment.
            const uint16_t size = read16(data, cursor + 2);
            if (read16(data, cursor) == 0xFFFF && size == 4) {
                break;
            }
            if (size != 0x1C) {
                break;
            }
            StdRecord record;
            record.type = read32(data, cursor + 4);
            record.x = readFloat(data, cursor + 8);
            record.y = readFloat(data, cursor + 12);
            record.z = readFloat(data, cursor + 16);
            record.width = readFloat(data, cursor + 20);
            record.height = readFloat(data, cursor + 24);
            segment.records.push_back(record);
        }
        segments.push_back(std::move(segment));
    }
    return true;
}

} // namespace th95::format
