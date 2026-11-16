#include "level_sim.hpp"
#include "mission_data.hpp"
#include "../log.hpp"
#include "../numeric/numeric.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace th95::scene {

uint32_t LevelSimulation::random()
{
    const auto next = [&]() {
        m_seed = numeric::rotateSeed16(m_seed);
        return m_seed;
    };
    const uint32_t high = uint32_t(next()) << 16;
    return high | uint32_t(next());
}

float LevelSimulation::unit()
{
    return numeric::unitFromUint32(random());
}

bool LevelSimulation::load(const std::vector<uint8_t> &ecl, const std::vector<uint8_t> &sht,
                           int mission)
{
    if (mission < 0 || sht.size() < 36 || !m_ecl.parse(ecl)) return false;
    const auto number = [&](int offset) {
        float value;
        std::memcpy(&value, sht.data() + offset, sizeof(value));
        return value;
    };
    m_mission = mission;
    m_fast = number(20);
    m_slow = number(24);
    // Retail's runtime speed table (player+1040, read from 0x42F190) stores
    // diagonal speeds as straight/sqrt(2): 5.0 -> 3.5355339 and 2.3 ->
    // 1.6263455. The raw player.sht fields 28/32 (2.828/1.414) are not these
    // values and must not be used for player movement.
    const float diagonal = std::sqrt(0.5f);
    m_fastDiagonal = numeric::mul(m_fast, diagonal);
    m_slowDiagonal = numeric::mul(m_slow, diagonal);
    m_hit = numeric::div(number(4), 2);
    m_graze = numeric::div(number(8), 2);
    restart();
    return true;
}

void LevelSimulation::restart()
{
    m_enemies.clear();
    m_bullets.clear();
    m_sounds.clear();
    m_effects.clear();
    m_diagnostics.clear();
    m_diagnosticCounts.clear();
    failureAge = 0;
    successAge = -20;
    focused = false;
    m_timeline = 0;
    m_clock = 0;
    m_seed = 1;
    m_nextId = 1;
    volleyEvent = 0;
    volleyX = volleyY = 0;
    playerX = 0;
    playerY = 480;
    cameraX = 0;
    cameraY = 400;
    charge = .5f;
    cameraState = 0;
    frames = 0;
    playerAnimation = 0;
    photos = attempts = 0;
    score = highScore = lastPhotoScore = 0;
    photoTargets = photoBullets = photoResultTicks = 0;
    photoSuccess = photoSelf = false;
    photoDistanceMultiplier = 1.2f;
    requiredPhotos = missionShots(m_mission);
    remaining = 0;
    flash = 0;
    cleared = dead = paused = fault = false;
    collisionEvent = 0;
    shutterEvent = 0;
    collisionX = collisionY = 0;
    emitted = executed = 0;
    scoreMultiplier = 1;
    m_globals = {};
    m_globalIntegers = {};
    timeScale = 1;
    m_cameraClock = m_chargeClock = 0;
    m_shutterHold = 0;
    m_erasing = false;
    m_eraseTicks = 0;
    m_burstDraws = 0;
    m_chargeHeld = m_shiftReleased = m_previousHorizontal = 0;
    m_cameraDistance = 56;
    // 0x4307D0 initialises the camera angle to -pi/2 (0xBFC90FDB) and the level
    // start leaves the aim and the previous player sample on the intro end point.
    m_cameraAngle = numeric::neg(float(numeric::kPi) / 2.0f);
    m_previousPlayerX = 0;
    m_previousPlayerY = 400;
    m_charging = m_shootWasHeld = m_targetInFrame = false;
    spellName.clear();
    spellEvent = 0;
    spellActive = false;
}

void LevelSimulation::recordFault(vm::Fault kind, uint16_t opcode, uint32_t offset, int detail)
{
    if (kind == vm::Fault::None) return;
    ++m_diagnosticCounts[kind];
    if (vm::fatal(kind)) fault = true;
    for (auto &entry : m_diagnostics) {
        if (entry.kind == kind && entry.opcode == opcode && entry.offset == offset &&
            entry.detail == detail) {
            ++entry.count;
            return;
        }
    }
    vm::Diagnostic entry;
    entry.kind = kind;
    entry.opcode = opcode;
    entry.offset = offset;
    entry.detail = detail;
    entry.count = 1;
    m_diagnostics.push_back(entry);
}

unsigned LevelSimulation::diagnosticCount(vm::Fault kind) const
{
    const auto it = m_diagnosticCounts.find(kind);
    return it == m_diagnosticCounts.end() ? 0u : it->second;
}

unsigned LevelSimulation::unsupportedCount() const
{
    return diagnosticCount(vm::Fault::UnsupportedInstruction) +
           diagnosticCount(vm::Fault::UnsupportedVariable) +
           diagnosticCount(vm::Fault::InvalidOperand) +
           diagnosticCount(vm::Fault::MissingService) +
           diagnosticCount(vm::Fault::UnresolvedJump);
}

void LevelSimulation::sound(int id, float x, bool stop)
{
    if (id < 0 || id >= 47) return;
    for (auto &event : m_sounds) {
        if (event.id != id) continue;
        if (stop || event.stop) {
            event = {id, x, stop};
            return;
        }
        if (event.count < 128) {
            event.x = numeric::div(numeric::add(numeric::mul(event.x, float(event.count)), x),
                                   float(event.count + 1));
            ++event.count;
        }
        return;
    }
    m_sounds.push_back({id, x, stop});
}

void LevelSimulation::spawn(int script, float x, float y, const Thread *parent)
{
    if (m_enemies.size() >= 128) return;
    Enemy enemy;
    enemy.interrupts.fill(-1);
    enemy.id = m_nextId++;
    enemy.x = x;
    enemy.y = y;
    enemy.main.script = script;
    if (parent) {
        enemy.main.integers = parent->integers;
        enemy.main.floats = parent->floats;
        enemy.main.counters = parent->counters;
        enemy.main.arguments = parent->arguments;
        enemy.main.intArguments = parent->intArguments;
        enemy.main.scratch = parent->scratch;
    }
    m_enemies.push_back(enemy);
}

void LevelSimulation::advanceActors()
{
    for (std::size_t i = 0; i < m_enemies.size(); ++i) {
        auto &enemy = m_enemies[i];
        if (!enemy.alive) continue;
        run(enemy, enemy.main);
        for (auto &thread : enemy.async)
            if (enemy.alive) run(enemy, thread);
        if (enemy.alive) move(enemy);
    }
    for (auto &bullet : m_bullets)
        if (bullet.alive) tickBullet(bullet);
    m_enemies.erase(std::remove_if(m_enemies.begin(), m_enemies.end(),
                                   [](const auto &enemy) { return !enemy.alive; }),
                    m_enemies.end());
    m_bullets.erase(std::remove_if(m_bullets.begin(), m_bullets.end(),
                                   [](const auto &bullet) { return !bullet.alive; }),
                    m_bullets.end());
}

} // namespace th95::scene
