// Headless dynamic baseline driver.
//
// Runs one LV1 scene from the user-supplied original DAT with a fixed input
// script, emits a machine-readable per-frame trace, and compares it with a
// golden trace. The record contains a gameplay-state hash, a geometry/layout
// hash standing in for the picture, an audio-event hash, an effect-event hash,
// scene counters and the identity of every resource involved. The same JSONL
// schema is what an original-side capture tool is expected to produce.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "archive.hpp"
#include "formats/ecl.hpp"
#include "scenes/level_sim.hpp"
#include "scenes/scene_table.hpp"
#include "sim/frame_trace.hpp"
#include "sim/input_script.hpp"
#include "sim/state_hash.hpp"

namespace {

using th95::Archive;
using th95::Entry;
using th95::platform::InputState;
using th95::scene::LevelSimulation;
using th95::scene::SceneEntry;
using th95::scene::sceneEntry;
using namespace th95::sim;

struct Options {
    std::string archive;
    std::string script;
    std::string record;
    std::string check;
    std::string report;
    int mission = 0;
    int frames = 0;
    int tolerance = 0;
    bool verbose = false;
    bool failOnUnsupported = false;
};

void usage()
{
    std::printf(
        "usage: th95-baseline --archive <dat> [--mission <scene id>] [--script <file>]\n"
        "                     [--frames N] [--record <jsonl>] [--check <jsonl>]\n"
        "                     [--tolerance N] [--report <json>] [--verbose]\n"
        "                     [--fail-on-unsupported]\n");
}

bool parseOptions(int argc, char **argv, Options &options, std::string &error)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&](std::string &target) {
            if (i + 1 >= argc) {
                error = "missing value for " + arg;
                return false;
            }
            target = argv[++i];
            return true;
        };
        if (arg == "--archive") { if (!value(options.archive)) return false; }
        else if (arg == "--script") { if (!value(options.script)) return false; }
        else if (arg == "--record") { if (!value(options.record)) return false; }
        else if (arg == "--check") { if (!value(options.check)) return false; }
        else if (arg == "--report") { if (!value(options.report)) return false; }
        else if (arg == "--mission") { std::string v; if (!value(v)) return false; options.mission = std::atoi(v.c_str()); }
        else if (arg == "--frames") { std::string v; if (!value(v)) return false; options.frames = std::atoi(v.c_str()); }
        else if (arg == "--tolerance") { std::string v; if (!value(v)) return false; options.tolerance = std::atoi(v.c_str()); }
        else if (arg == "--verbose") options.verbose = true;
        else if (arg == "--fail-on-unsupported") options.failOnUnsupported = true;
        else if (arg == "--help") { usage(); std::exit(0); }
        else { error = "unknown option " + arg; return false; }
    }
    if (options.archive.empty()) { error = "--archive is required"; return false; }
    if (!sceneEntry(options.mission)) { error = "--mission must be a scene id from the scene table"; return false; }
    if (options.tolerance < 0) { error = "--tolerance must be >= 0"; return false; }
    return true;
}

uint64_t hashBytes(const std::vector<uint8_t> &data)
{
    Hasher hasher;
    hasher.bytes(data.data(), data.size());
    return hasher.value();
}

std::string toJsonHex(uint64_t value)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

uint64_t hashTable(const Archive &archive)
{
    Hasher hasher;
    for (const auto &entry : archive.entries()) {
        hasher.text(entry.name.c_str());
        hasher.u32(entry.size);
        hasher.u32(entry.offset);
    }
    return hasher.value();
}

void hashThread(Hasher &hasher, const LevelSimulation::Thread &thread)
{
    hasher.i32(thread.script);
    hasher.u64(thread.pc);
    hasher.f32(thread.time);
    hasher.f32(thread.wait);
    for (int value : thread.integers) hasher.i32(value);
    for (float value : thread.floats) hasher.f32(value);
    for (int value : thread.counters) hasher.i32(value);
    for (int value : thread.intArguments) hasher.i32(value);
    for (float value : thread.arguments) hasher.f32(value);
    for (float value : thread.scratch) hasher.f32(value);
    hasher.u32(static_cast<uint32_t>(thread.stack.size()));
}

void buildRecord(const LevelSimulation &sim, int frame, FrameRecord &record)
{
    Hasher state, layout, audio, effects;
    state.f32(sim.playerX);
    state.f32(sim.playerY);
    state.f32(sim.cameraX);
    state.f32(sim.cameraY);
    state.f32(sim.charge);
    state.f32(sim.photoWidth);
    state.f32(sim.photoHeight);
    state.f32(sim.timeScale);
    state.f32(sim.remaining);
    state.f32(sim.failureAge);
    state.f32(sim.successAge);
    state.f32(sim.scoreMultiplier);
    state.i32(sim.frames);
    state.i32(sim.playerAnimation);
    state.i32(sim.cameraState);
    state.i32(sim.photos);
    state.i32(sim.attempts);
    state.i32(sim.requiredPhotos);
    state.i32(sim.score);
    state.i32(sim.highScore);
    state.i32(sim.lastPhotoScore);
    state.i32(sim.photoTargets);
    state.i32(sim.photoBullets);
    state.i32(sim.photoResultTicks);
    state.i32(sim.flash);
    state.flag(sim.photoSuccess);
    state.flag(sim.photoSelf);
    state.flag(sim.cleared);
    state.flag(sim.dead);
    state.flag(sim.paused);
    state.flag(sim.focused);
    state.flag(sim.spellActive);
    state.flag(sim.targetInFrame());
    state.text(sim.spellName.c_str());
    state.u32(sim.collisionEvent);
    state.f32(sim.collisionX);
    state.f32(sim.collisionY);

    layout.f32(sim.cameraX);
    layout.f32(sim.cameraY);
    layout.f32(sim.playerX);
    layout.f32(sim.playerY);
    layout.i32(sim.playerAnimation);
    layout.i32(quantize(sim.charge));
    layout.i32(sim.flash);

    for (const auto &enemy : sim.enemies()) {
        state.u32(enemy.id);
        state.f32(enemy.x);
        state.f32(enemy.y);
        state.f32(enemy.vx);
        state.f32(enemy.vy);
        state.f32(enemy.angle);
        state.f32(enemy.speed);
        state.f32(enemy.angularVelocity);
        state.f32(enemy.acceleration);
        state.f32(enemy.startX);
        state.f32(enemy.startY);
        state.f32(enemy.deltaX);
        state.f32(enemy.deltaY);
        state.f32(enemy.moveTime);
        state.f32(enemy.moveDuration);
        state.f32(enemy.minX);
        state.f32(enemy.minY);
        state.f32(enemy.maxX);
        state.f32(enemy.maxY);
        state.f32(enemy.hitWidth);
        state.f32(enemy.hitHeight);
        state.f32(enemy.cooldown);
        state.f32(enemy.warning);
        state.f32(enemy.warningDuration);
        state.f32(enemy.spawnCircleTimer);
        state.i32(enemy.moveMode);
        state.i32(enemy.ease);
        state.i32(enemy.animation);
        state.i32(enemy.lastDirection);
        state.i32(enemy.photoCount);
        state.i32(enemy.shotSound);
        state.i32(enemy.transformSound);
        for (int threshold : enemy.photoThreshold) state.i32(threshold);
        for (int script : enemy.photoScripts) state.i32(script);
        for (int interrupt : enemy.interrupts) state.i32(interrupt);
        for (const auto &extension : enemy.extensions) {
            state.i32(extension.flags);
            state.i32(extension.a);
            state.i32(extension.b);
            state.f32(extension.x);
            state.f32(extension.y);
            state.i32(extension.concurrent);
        }
        state.flag(enemy.alive);
        state.flag(enemy.boss);
        state.flag(enemy.hidden);
        state.flag(enemy.attackable);
        state.flag(enemy.boundsCheck);
        state.flag(enemy.collision);
        state.flag(enemy.entered);
        state.flag(enemy.boundsEnabled);
        state.flag(enemy.spawnCircle);
        hashThread(state, enemy.main);
        for (const auto &thread : enemy.async) hashThread(state, thread);

        layout.i32(quantize(enemy.x - sim.cameraX));
        layout.i32(quantize(enemy.y - sim.cameraY));
        layout.i32(enemy.animation);
        layout.flag(enemy.alive);
        layout.flag(enemy.boss);
        layout.flag(enemy.hidden);
    }

    for (const auto &bullet : sim.bullets()) {
        state.u32(bullet.id);
        state.f32(bullet.x);
        state.f32(bullet.y);
        state.f32(bullet.angle);
        state.f32(bullet.speed);
        state.f32(bullet.radius);
        state.f32(bullet.age);
        state.f32(bullet.vx);
        state.f32(bullet.vy);
        state.f32(bullet.extensionTime);
        state.f32(bullet.appearance);
        state.f32(bullet.grace);
        state.i32(bullet.type);
        state.i32(bullet.color);
        state.i32(bullet.flags);
        state.i32(bullet.extension);
        state.i32(bullet.spriteRevision);
        state.i32(bullet.sound);
        state.flag(bullet.alive);
        state.u32(static_cast<uint32_t>(bullet.active.size()));

        layout.i32(quantize(bullet.x - sim.cameraX));
        layout.i32(quantize(bullet.y - sim.cameraY));
        layout.i32(bullet.type);
        layout.i32(bullet.color);
        layout.i32(bullet.spriteRevision);
    }

    for (const auto &sound : sim.sounds()) {
        audio.i32(sound.id);
        audio.flag(sound.stop);
        audio.i32(sound.count);
        audio.i32(quantize(sound.x));
        record.sounds.push_back({sound.id, sound.count, sound.stop, sound.x});
    }
    for (const auto &effect : sim.effects()) {
        effects.i32(effect.script);
        effects.u32(effect.follow);
        effects.i32(quantize(effect.x));
        effects.i32(quantize(effect.y));
        layout.i32(effect.script);
        layout.i32(quantize(effect.x - sim.cameraX));
        layout.i32(quantize(effect.y - sim.cameraY));
        record.effectList.push_back({effect.script, effect.follow, effect.x, effect.y});
    }

    record.frame = frame;
    record.state = state.value();
    record.layout = layout.value();
    record.audio = audio.value();
    record.effects = effects.value();
    record.score = sim.score;
    record.photos = sim.photos;
    record.attempts = sim.attempts;
    record.enemies = static_cast<int>(sim.enemies().size());
    record.bullets = static_cast<int>(sim.bullets().size());
}

bool loadGolden(const std::string &path, std::vector<FrameRecord> &records, std::string &error)
{
    std::ifstream file(path);
    if (!file) {
        error = "cannot open golden " + path;
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        FrameRecord record;
        if (!parseJsonLine(line, record)) {
            error = "invalid golden record in " + path;
            return false;
        }
        records.push_back(std::move(record));
    }
    return true;
}

std::string writeReport(const Options &options, const Archive &archive, uint64_t tableHash,
                        const std::vector<std::pair<std::string, uint64_t>> &resources,
                        const Comparison &comparison, int producedFrames,
                        const LevelSimulation &sim, bool passed, bool diagnostic)
{
    std::string json = "{\n";
    json += "  \"archive\": \"" + options.archive + "\",\n";
    json += "  \"archiveBytes\": " + std::to_string(archive.size()) + ",\n";
    json += "  \"entries\": " + std::to_string(archive.entries().size()) + ",\n";
    json += "  \"tableHash\": \"" + toJsonHex(tableHash) + "\",\n";
    json += "  \"mission\": " + std::to_string(options.mission) + ",\n";
    json += "  \"script\": \"" + options.script + "\",\n";
    json += "  \"producedFrames\": " + std::to_string(producedFrames) + ",\n";
    json += "  \"tolerance\": " + std::to_string(options.tolerance) + ",\n";
    json += "  \"diagnostic\": ";
    json += diagnostic ? "true" : "false";
    json += ",\n  \"result\": \"";
    json += passed ? "pass" : "fail";
    json += "\",\n";
    json += "  \"mismatchedFrames\": " + std::to_string(comparison.mismatchedFrames) + ",\n";
    json += "  \"layoutMismatches\": " + std::to_string(comparison.layoutMismatches) + ",\n";
    json += "  \"audioMismatches\": " + std::to_string(comparison.audioMismatches) + ",\n";
    json += "  \"effectMismatches\": " + std::to_string(comparison.effectMismatches) + ",\n";
    json += "  \"unsupported\": {\n";
    const th95::vm::Fault kinds[] = {
        th95::vm::Fault::UnsupportedInstruction, th95::vm::Fault::UnsupportedVariable,
        th95::vm::Fault::InvalidOperand, th95::vm::Fault::MissingService,
        th95::vm::Fault::UnresolvedJump, th95::vm::Fault::MissingResource};
    for (std::size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        json += "    \"";
        json += th95::vm::faultName(kinds[i]);
        json += "\": ";
        json += std::to_string(sim.diagnosticCount(kinds[i]));
        json += i + 1 == sizeof(kinds) / sizeof(kinds[0]) ? "\n" : ",\n";
    }
    json += "  },\n  \"resources\": [\n";
    for (std::size_t i = 0; i < resources.size(); ++i) {
        json += "    {\"name\": \"" + resources[i].first + "\", \"hash\": \"" +
                toJsonHex(resources[i].second) + "\"}";
        json += i + 1 == resources.size() ? "\n" : ",\n";
    }
    json += "  ],\n  \"details\": [\n";
    for (std::size_t i = 0; i < comparison.details.size(); ++i) {
        const auto &detail = comparison.details[i];
        json += "    {\"frame\": " + std::to_string(detail.frame) + ", \"field\": \"" +
                detail.field + "\", \"expected\": \"" + detail.expected +
                "\", \"actual\": \"" + detail.actual + "\"}";
        json += i + 1 == comparison.details.size() ? "\n" : ",\n";
    }
    json += "  ]\n}\n";
    return json;
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        usage();
        return 2;
    }

    Archive archive;
    if (!archive.open(options.archive)) {
        std::fprintf(stderr, "error: cannot open archive %s\n", options.archive.c_str());
        return 2;
    }

    const SceneEntry *scene = sceneEntry(options.mission);
    const char *eclName = scene->ecl;
    const Entry *eclEntry = archive.find(eclName);
    const Entry *shtEntry = archive.find("player.sht");
    if (!eclEntry || !shtEntry) {
        std::fprintf(stderr, "error: archive lacks %s or player.sht\n", eclName);
        return 2;
    }
    const auto eclBytes = archive.extract(*eclEntry);
    const auto shtBytes = archive.extract(*shtEntry);

    std::vector<InputState> inputs;
    if (!options.script.empty()) {
        if (!loadInputScript(options.script, inputs, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
    }
    if (options.frames > 0) {
        inputs.resize(static_cast<std::size_t>(options.frames));
    }
    if (inputs.empty()) {
        std::fprintf(stderr, "error: no frames (supply --script or --frames)\n");
        return 2;
    }

    LevelSimulation simulation;
    if (!simulation.load(eclBytes, shtBytes, options.mission)) {
        std::fprintf(stderr, "error: %s failed to load\n", eclName);
        return 2;
    }

    std::vector<std::pair<std::string, uint64_t>> resources;
    resources.emplace_back(eclName, hashBytes(eclBytes));
    resources.emplace_back("player.sht", hashBytes(shtBytes));
    for (const char *name : {scene->enemy, "player.anm", "bullet.anm", "front.anm"}) {
        if (const Entry *entry = archive.find(name))
            resources.emplace_back(name, hashBytes(archive.extract(*entry)));
    }

    std::ofstream recordFile;
    if (!options.record.empty()) {
        recordFile.open(options.record);
        if (!recordFile) {
            std::fprintf(stderr, "error: cannot write %s\n", options.record.c_str());
            return 2;
        }
    }

    std::vector<FrameRecord> trace;
    trace.reserve(inputs.size());
    int producedFrames = 0;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        simulation.update(inputs[i]);
        FrameRecord record;
        buildRecord(simulation, static_cast<int>(i), record);
        if (recordFile) recordFile << toJsonLine(record, options.verbose) << '\n';
        if (!options.check.empty()) trace.push_back(std::move(record));
        ++producedFrames;
    }
    if (recordFile) recordFile.close();

    Comparison comparison;
    bool checked = false;
    if (!options.check.empty()) {
        std::vector<FrameRecord> golden;
        if (!loadGolden(options.check, golden, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
        const std::size_t count = std::min(golden.size(), trace.size());
        for (std::size_t i = 0; i < count; ++i)
            compareFrame(golden[i], trace[i], comparison);
        if (golden.size() != trace.size()) {
            comparison.mismatchedFrames += static_cast<int>(golden.size() > trace.size()
                ? golden.size() - trace.size() : trace.size() - golden.size());
        }
        checked = true;
    }

    const bool clean = !checked || comparison.mismatchedFrames <= options.tolerance;
    const bool unsupportedClean = !options.failOnUnsupported || simulation.unsupportedCount() == 0;
    const bool passed = clean && unsupportedClean && !simulation.fault;

#ifdef TH95_DIAGNOSTIC
    constexpr bool diagnostic = true;
#else
    constexpr bool diagnostic = false;
#endif

    if (!options.report.empty()) {
        std::ofstream report(options.report);
        if (!report) {
            std::fprintf(stderr, "error: cannot write %s\n", options.report.c_str());
            return 2;
        }
        report << writeReport(options, archive, hashTable(archive), resources, comparison,
                              producedFrames, simulation, passed, diagnostic);
    }

    if (checked) {
        std::printf("frames=%d compared=%d mismatched=%d layout=%d audio=%d effects=%d\n",
                    producedFrames, comparison.frames, comparison.mismatchedFrames,
                    comparison.layoutMismatches, comparison.audioMismatches,
                    comparison.effectMismatches);
        for (const auto &detail : comparison.details)
            std::printf("  frame %d %s expected %s actual %s\n", detail.frame,
                        detail.field.c_str(), detail.expected.c_str(), detail.actual.c_str());
    }
    std::printf("unsupported=%u fault=%d result=%s\n", simulation.unsupportedCount(),
                simulation.fault ? 1 : 0, passed ? "pass" : "fail");
    for (const auto &entry : simulation.diagnostics()) {
        std::printf("  diag kind=%s opcode=%u offset=%u detail=%d script=%d count=%u\n",
                    th95::vm::faultName(entry.kind), unsigned(entry.opcode), entry.offset,
                    entry.detail, entry.script, entry.count);
    }
    return passed ? 0 : 1;
}
