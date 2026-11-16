#include "frame_trace.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace th95::sim {
namespace {

std::string hex64(uint64_t value)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::string number(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

bool findHex(const std::string &line, const char *key, uint64_t &out)
{
    const std::string pattern = std::string("\"") + key + "\":\"";
    const auto pos = line.find(pattern);
    if (pos == std::string::npos) return false;
    const auto begin = pos + pattern.size();
    const auto end = line.find('"', begin);
    if (end == std::string::npos) return false;
    out = std::strtoull(line.substr(begin, end - begin).c_str(), nullptr, 16);
    return true;
}

bool findInt(const std::string &line, const char *key, int &out)
{
    const std::string pattern = std::string("\"") + key + "\":";
    const auto pos = line.find(pattern);
    if (pos == std::string::npos) return false;
    out = std::atoi(line.c_str() + pos + pattern.size());
    return true;
}

void appendEvents(std::string &line, const FrameRecord &record)
{
    line += ",\"sounds\":[";
    for (std::size_t i = 0; i < record.sounds.size(); ++i) {
        const auto &sound = record.sounds[i];
        if (i) line += ',';
        line += "{\"id\":" + std::to_string(sound.id);
        line += ",\"count\":" + std::to_string(sound.count);
        line += ",\"stop\":";
        line += sound.stop ? "true" : "false";
        line += ",\"x\":" + number(sound.x) + "}";
    }
    line += "],\"effectList\":[";
    for (std::size_t i = 0; i < record.effectList.size(); ++i) {
        const auto &effect = record.effectList[i];
        if (i) line += ',';
        line += "{\"script\":" + std::to_string(effect.script);
        line += ",\"follow\":" + std::to_string(effect.follow);
        line += ",\"x\":" + number(effect.x);
        line += ",\"y\":" + number(effect.y) + "}";
    }
    line += ']';
}

} // namespace

std::string toJsonLine(const FrameRecord &record, bool verbose)
{
    std::string line;
    line.reserve(256);
    line += "{\"frame\":" + std::to_string(record.frame);
    line += ",\"state\":\"" + hex64(record.state) + "\"";
    line += ",\"layout\":\"" + hex64(record.layout) + "\"";
    line += ",\"audio\":\"" + hex64(record.audio) + "\"";
    line += ",\"effects\":\"" + hex64(record.effects) + "\"";
    line += ",\"score\":" + std::to_string(record.score);
    line += ",\"photos\":" + std::to_string(record.photos);
    line += ",\"attempts\":" + std::to_string(record.attempts);
    line += ",\"enemies\":" + std::to_string(record.enemies);
    line += ",\"bullets\":" + std::to_string(record.bullets);
    if (verbose) appendEvents(line, record);
    line += '}';
    return line;
}

bool parseJsonLine(const std::string &line, FrameRecord &record)
{
    record = FrameRecord{};
    if (!findInt(line, "frame", record.frame)) return false;
    if (!findHex(line, "state", record.state)) return false;
    findHex(line, "layout", record.layout);
    findHex(line, "audio", record.audio);
    findHex(line, "effects", record.effects);
    findInt(line, "score", record.score);
    findInt(line, "photos", record.photos);
    findInt(line, "attempts", record.attempts);
    findInt(line, "enemies", record.enemies);
    findInt(line, "bullets", record.bullets);
    return true;
}

void compareFrame(const FrameRecord &expected, const FrameRecord &actual,
                  Comparison &comparison, std::size_t detailLimit)
{
    ++comparison.frames;
    bool mismatched = false;
    const auto record = [&](const char *field, const std::string &left,
                            const std::string &right) {
        mismatched = true;
        if (comparison.details.size() < detailLimit)
            comparison.details.push_back({expected.frame, field, left, right});
    };
    const auto recordInt = [&](const char *field, int left, int right) {
        if (left == right) return;
        record(field, std::to_string(left), std::to_string(right));
    };

    if (expected.state != actual.state) record("state", hex64(expected.state), hex64(actual.state));
    if (expected.layout != actual.layout) {
        record("layout", hex64(expected.layout), hex64(actual.layout));
        ++comparison.layoutMismatches;
    }
    if (expected.audio != actual.audio) {
        record("audio", hex64(expected.audio), hex64(actual.audio));
        ++comparison.audioMismatches;
    }
    if (expected.effects != actual.effects) {
        record("effects", hex64(expected.effects), hex64(actual.effects));
        ++comparison.effectMismatches;
    }
    recordInt("score", expected.score, actual.score);
    recordInt("photos", expected.photos, actual.photos);
    recordInt("attempts", expected.attempts, actual.attempts);
    recordInt("enemies", expected.enemies, actual.enemies);
    recordInt("bullets", expected.bullets, actual.bullets);
    if (mismatched) ++comparison.mismatchedFrames;
}

} // namespace th95::sim
