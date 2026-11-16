#include "replay.hpp"

#include "../crypto.hpp"
#include "../lzss.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <system_error>

namespace th95::format {
namespace {
constexpr uint32_t kMagic = 0x72353974; // "t95r"
constexpr uint32_t kUserMagic = 0x52455355; // "USER" (written at 0x434A90 line 57)
constexpr std::size_t kContainerSize = 36;
constexpr std::size_t kBodyHeaderSize = 248;
constexpr uint32_t kVersionWord = 258;

uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t *p, uint32_t v) { put16(p, uint16_t(v)); put16(p + 2, uint16_t(v >> 16)); }

// Shift-JIS banner the retail writer copies from 0x49682C.
const char kInfoBanner[] = "\x93\x8c\x95\xfb\x95\xb6\x89\xd4\x92\x9f "
                           "\x83\x8a\x83\x76\x83\x8c\x83\x43\x83\x74\x83\x40\x83\x43\x83\x8b"
                           "\x8f\xee\x95\xf1\r\n";
// Shift-JIS comment placeholder from 0x496790.
const char kCommentText[] = "\x83\x52\x83\x81\x83\x93\x83\x48"
                            "\x82\xf0\x8f\x91\x82\xaf\x82\xdc\x82\xb7";

void appendUserChunk(std::vector<uint8_t> &out, uint8_t type,
                     const std::string &text)
{
    // 12-byte chunk header, text including its NUL, then four-byte padding.
    const std::size_t payload = text.size() + 1;
    std::size_t length = (12 + payload + 3) & ~std::size_t(3);
    const std::size_t start = out.size();
    out.resize(start + length, 0);
    put32(out.data() + start, kUserMagic);
    put32(out.data() + start + 4, uint32_t(length));
    out[start + 8] = type;
    std::memcpy(out.data() + start + 12, text.data(), text.size());
}

bool loadFile(const std::filesystem::path &path, std::vector<uint8_t> &out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto size = file.tellg();
    if (size < std::streamoff(kContainerSize)) {
        return false;
    }
    out.resize(std::size_t(size));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size()));
    return bool(file);
}

bool readHeader(const std::filesystem::path &path, ReplayHeader &header)
{
    Replay replay;
    if (!readReplay(path, replay)) {
        return false;
    }
    header = std::move(replay.header);
    return true;
}
} // namespace

bool readReplay(const std::filesystem::path &path, Replay &out)
{
    std::vector<uint8_t> data;
    if (!loadFile(path, data)) {
        return false;
    }
    if (u32(data.data()) != kMagic) {
        return false;
    }
    const uint32_t payloadSize = u32(data.data() + 28);
    const uint32_t decodedSize = u32(data.data() + 32);
    if (payloadSize == 0 || decodedSize < kBodyHeaderSize || decodedSize > 16u * 1024 * 1024) {
        return false;
    }
    if (kContainerSize + payloadSize > data.size()) {
        return false;
    }

    std::vector<uint8_t> payload(data.begin() + std::ptrdiff_t(kContainerSize),
                                 data.begin() + std::ptrdiff_t(kContainerSize + payloadSize));
    crypto::decrypt(payload, 170, 225, 1024, payloadSize);
    crypto::decrypt(payload, 61, 122, 128, payloadSize);
    const auto body = lzss::decode(payload, decodedSize);
    if (body.size() < kBodyHeaderSize) {
        return false;
    }

    Replay replay;
    auto &header = replay.header;
    header.sceneId = u16(body.data());
    header.level = body[2];
    header.scene = body[3];
    header.seed = u16(body.data() + 4);
    const char *rawName = reinterpret_cast<const char *>(body.data() + 7);
    header.name.assign(rawName, strnlen(rawName, 8));
    header.timestamp = u32(body.data() + 16);
    header.score = u32(body.data() + 20);
    std::memcpy(header.config.data(), body.data() + 24, header.config.size());
    std::memcpy(&header.rate, body.data() + 224, sizeof(header.rate));
    header.inputLength = u32(body.data() + 240);
    header.trailerLength = u32(body.data() + 244);

    const std::size_t inputEnd = kBodyHeaderSize + header.inputLength;
    if (inputEnd > body.size() ||
        inputEnd + header.trailerLength > body.size()) {
        return false;
    }
    replay.input.assign(body.begin() + std::ptrdiff_t(kBodyHeaderSize),
                        body.begin() + std::ptrdiff_t(inputEnd));
    replay.trailer.assign(body.begin() + std::ptrdiff_t(inputEnd),
                          body.begin() + std::ptrdiff_t(inputEnd + header.trailerLength));
    out = std::move(replay);
    return true;
}

bool writeReplay(const std::filesystem::path &path, const Replay &replay,
                 const std::string &userName)
{
    const auto &header = replay.header;
    std::vector<uint8_t> body(kBodyHeaderSize + replay.input.size() + replay.trailer.size(), 0);
    put16(body.data(), header.sceneId);
    body[2] = header.level;
    body[3] = header.scene;
    put16(body.data() + 4, header.seed);
    const std::string name = userName.empty() ? header.name : userName;
    const std::size_t copied = std::min<std::size_t>(name.size(), 8);
    std::memcpy(body.data() + 7, name.data(), copied);
    // Retail strcpy()s the name then pads every remaining byte with spaces,
    // overwriting the terminator, so short names carry no NUL inside +7..+14.
    for (std::size_t i = copied; i < 8; ++i) {
        body[7 + i] = ' ';
    }
    put32(body.data() + 16, header.timestamp);
    put32(body.data() + 20, header.score);
    std::memcpy(body.data() + 24, header.config.data(), header.config.size());
    const float rate = header.rate;
    std::memcpy(body.data() + 224, &rate, sizeof(rate));
    put32(body.data() + 240, uint32_t(replay.input.size()));
    put32(body.data() + 244, uint32_t(replay.trailer.size()));
    if (!replay.input.empty()) {
        std::memcpy(body.data() + kBodyHeaderSize, replay.input.data(), replay.input.size());
    }
    if (!replay.trailer.empty()) {
        std::memcpy(body.data() + kBodyHeaderSize + replay.input.size(),
                    replay.trailer.data(), replay.trailer.size());
    }

    std::vector<uint8_t> payload = lzss::encode(body);
    if (payload.empty()) {
        return false;
    }
    crypto::encrypt(payload, 61, 122, 128, payload.size());
    crypto::encrypt(payload, 170, 225, 1024, payload.size());

    std::vector<uint8_t> out(kContainerSize, 0);
    put32(out.data(), kMagic);
    // +4 is the container version (1 in every retail sample), not the seed;
    // the seed lives in the body header at +4.
    put16(out.data() + 4, 1);
    put32(out.data() + 12, uint32_t(kContainerSize + payload.size()));
    put32(out.data() + 16, kVersionWord);
    put32(out.data() + 28, uint32_t(payload.size()));
    put32(out.data() + 32, uint32_t(body.size()));
    out.insert(out.end(), payload.begin(), payload.end());

    // Informational chunks. Playback never reads them, but keeping the layout
    // makes port-written files indistinguishable from retail ones.
    char lines[512];
    char date[64];
    const std::time_t stamp = header.timestamp;
    const std::tm *tmValue = std::localtime(&stamp);
    if (tmValue) {
        std::snprintf(date, sizeof(date), "Date %.2d/%.2d/%.2d %.2d:%.2d\r\n",
                      tmValue->tm_year % 100, tmValue->tm_mon + 1, tmValue->tm_mday,
                      tmValue->tm_hour, tmValue->tm_min);
    } else {
        std::snprintf(date, sizeof(date), "Date --/--/-- --:--\r\n");
    }
    // Retail prints body +7 with %s, so the chunk keeps the eight padded bytes.
    const std::string shownName(reinterpret_cast<const char *>(body.data() + 7), 8);
    if (header.level == 10) {
        std::snprintf(lines, sizeof(lines),
                      "Version 1.02a\r\nName %s\r\nLevel EX\r\nScene %d\r\n%sScore %d\r\nSlow Rate %2.2f\r\n",
                      shownName.c_str(), int(header.scene) + 1, date,
                      int(header.score), double(header.rate));
    } else {
        std::snprintf(lines, sizeof(lines),
                      "Version 1.02a\r\nName %s\r\nLevel %d\r\nScene %d\r\n%sScore %d\r\nSlow Rate %2.2f\r\n",
                      shownName.c_str(), int(header.level) + 1, int(header.scene) + 1, date,
                      int(header.score), double(header.rate));
    }
    appendUserChunk(out, 0, std::string(kInfoBanner) + lines);
    appendUserChunk(out, 1, kCommentText);

    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size()));
    return bool(file);
}

std::array<ReplaySlot, kReplaySlots> scanReplays(const std::filesystem::path &gameDirectory)
{
    std::array<ReplaySlot, kReplaySlots> slots;
    const auto directory = gameDirectory / "replay";
    for (int i = 0; i < kReplayNumberedSlots; ++i) {
        char name[24];
        std::snprintf(name, sizeof(name), "th95_%.2d.rpy", i + 1);
        auto &slot = slots[std::size_t(i)];
        slot.path = directory / name;
        slot.exists = readHeader(slot.path, slot.header);
    }

    std::vector<std::filesystem::path> userFiles;
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string filename = it->path().filename().string();
        std::string lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        // Retail scans FindFirstFileA("th95_ud????.rpy"): exactly four
        // characters between "th95_ud" and ".rpy", and it fills slots in
        // directory-enumeration order rather than sorting the names.
        if (lower.rfind("th95_ud", 0) != 0 || lower.size() != 15 ||
            lower.compare(11, 4, ".rpy") != 0) {
            continue;
        }
        userFiles.push_back(it->path());
    }
    const std::size_t room = std::size_t(kReplaySlots - kReplayNumberedSlots);
    if (userFiles.size() > room) {
        userFiles.resize(room);
    }
    for (std::size_t i = 0; i < userFiles.size(); ++i) {
        auto &slot = slots[std::size_t(kReplayNumberedSlots) + i];
        slot.path = userFiles[i];
        slot.exists = readHeader(slot.path, slot.header);
    }
    return slots;
}

} // namespace th95::format
