#include "level_sim.hpp"
#include "../numeric/numeric.hpp"
#include <algorithm>
#include <cmath>

namespace th95::scene {
namespace num = th95::numeric;

namespace {

float floorHundredths(float value)
{
    // Retail stores floor(pos * 100) / 100, not a truncation toward zero.
    const float scaled = num::mul(value, 100.0f);
    if (std::fabs(scaled) >= 8388608.0f) return value;
    return num::div(float(std::floor(double(scaled))), 100.0f);
}

} // namespace

void LevelSimulation::update(const platform::InputState &input)
{
    m_sounds.clear();
    m_effects.clear();
    if (input.pause) {
        paused = !paused;
        sound(32);
        return;
    }
    if (paused || fault) return;
    if (dead) {
        if (failureAge < 30) {
            failureAge = num::add(failureAge, 1.0f);
            advanceActors();
        }
        return;
    }

    // The photo manager (0x42AD60 state 2) reaches its erase step three frames
    // after the release; from then on it runs 0x407C90 every frame until the
    // shutter sequence ends. Each erased bullet takes the two global draws of
    // its 288 spark, so this ordering is replay-visible.
    // One tick later every spawned 294 burst executes bullet.anm 294's t=2 opcode
    // batch, which draws seven global words; the retail seed trace shows exactly
    // 364 rotations there for the 52 erased bullets (52 * 7).
    if (m_burstDraws > 0) {
        for (int i = 0; i < m_burstDraws; ++i) m_seed = num::rotateSeed16(m_seed);
        m_burstDraws = 0;
    }
    if (m_erasing) {
        ++m_eraseTicks;
        // Retail's manager leaves the erase state after two passes (the seed
        // trace shows exactly two draw batches, at release+4 and release+5).
        if (m_eraseTicks >= 3 && m_eraseTicks <= 4)
            runShutterErase();
        else if (m_eraseTicks > 4)
            m_erasing = false;
    }

    if (!cleared) {
        flash = std::max(0, flash - 1);
        photoResultTicks = std::max(0, photoResultTicks - 1);
    } else {
        const float previous = successAge;
        successAge = num::add(previous, 1.0f);
        if (previous < 4.0f) {
            if (successAge >= 4.0f) {
                for (auto &enemy : m_enemies) {
                    const int exitScript = enemy.interrupts[31];
                    enemy.main = Thread{};
                    for (auto &thread : enemy.async) thread = Thread{};
                    enemy.extensions = {};
                    enemy.main.script = exitScript;
                }
                for (auto &enemy : m_enemies)
                    if (!enemy.boss) enemy.alive = false;
            }
        } else if (successAge >= 15.0f) {
            m_bullets.clear();
        }
        flash = std::max(0, flash - 1);
        photoResultTicks = std::max(0, photoResultTicks - 1);
        focused = false;
        m_charging = false;
        m_shootWasHeld = false;
        m_chargeHeld = 0;
        m_targetInFrame = false;
    }

    const bool shooting = input.shoot || input.shootHeld;
    float dx = 0, dy = 0;
    bool diagonal = false;
    if (input.rightHeld || input.right) {
        dx = 1;
    } else if (input.leftHeld || input.left) {
        dx = -1;
    }
    if (input.downHeld || input.down) {
        dy = 1;
    } else if (input.upHeld || input.up) {
        dy = -1;
    }
    diagonal = dx != 0 && dy != 0;

    if (cameraState == 1) {
        ++frames;
        if (dx != 0 || dy != 0) {
            const float step = diagonal ? 2.3334749f : 3.3f;
            cameraX = num::clamp(num::add(cameraX, num::mul(dx, step)), -184.0f, 184.0f);
            cameraY = num::clamp(num::add(cameraY, num::mul(dy, step)), 32.0f, 436.0f);
        }
        photoWidth = num::mulAdd(charge, 160.0f, 96.0f);
        photoHeight = num::mul(photoWidth, .75f);
        if (shooting) {
            // The aiming timer accumulates scaled ticks; once its integer part
            // reaches four the release charge drains by 1/42 per tick.
            if (int(m_cameraClock) >= 4)
                charge = num::max(0.0f, num::sub(charge, 1.0f / 42.0f));
            if (charge > 0.0001f) {
                timeScale = charge >= .35f
                    ? .25f
                    : num::add(num::mul(num::div(num::sub(.35f, charge), .35f), .75f), .25f);
                const bool inFrame = targetInFrame();
                if (inFrame && !m_targetInFrame) sound(46);
                m_targetInFrame = inFrame;
                m_cameraClock = num::add(m_cameraClock, timeScale);
            } else {
                // Charge exhausted: retail discards the shutter into slot 10
                // without counting an attempt, restores half a charge and enters
                // the shutter state with an empty record.
                charge = .5f;
                cameraState = 2;
                timeScale = 1;
                m_targetInFrame = false;
                photoSuccess = false;
                photoSelf = false;
                photoTargets = 0;
                photoBullets = 0;
                photoDistanceMultiplier = 1.2f;
                lastPhotoScore = 0;
                ++shutterEvent;
                photoResultTicks = 60;
                sound(44, 0, true);
                m_cameraClock = 1;
            }
        } else {
            // 0x432D10 runs from the level task (priority 11); the photo manager
            // (8) and the camera run before the enemy (12) and bullet (14) tasks,
            // so the capture sees this frame's pre-move bullet positions.
            photograph();
        }
        goto actors;
    }

    focused = input.shiftHeld;
    m_shiftReleased = focused ? 0 : m_shiftReleased + 1;

    if (frames <= 59) {
        ++frames;
        playerY = num::sub(480.0f, num::div(num::mul(float(frames), 80.0f), 60.0f));
        goto charge_phase;
    }
    ++frames;

    {
        if (m_shutterHold > 0) {
            // 0x42F190 multiplies the step by player+10776; retail holds that
            // scale at zero through the shutter result, so a down/up input in
            // the recorded stream moves nothing until frame 35 of the sequence.
            --m_shutterHold;
        } else {
            // Retail runs the player update in camera states 0, 2 and 3; only
            // the aiming state pins the direction to zero. The runtime speed
            // table stores diagonal values as straight/sqrt(2).
            float speed;
            if (diagonal) speed = focused ? m_slowDiagonal : m_fastDiagonal;
            else speed = focused ? m_slow : m_fast;
            if (focused && shooting) speed = num::mul(speed, .22f);
            playerX = num::clamp(
                floorHundredths(num::add(playerX, num::mul(num::mul(dx, speed), timeScale))),
                -184.0f, 184.0f);
            playerY = num::clamp(
                floorHundredths(num::add(playerY, num::mul(num::mul(dy, speed), timeScale))),
                32.0f, 436.0f);
            // Retail selects script 1/3 while moving left/right and 2/4 when that
            // direction stops; a neutral idle keeps the current script.
            const int direction = dx < 0 ? -1 : dx > 0 ? 1 : 0;
            if (direction < 0) playerAnimation = 1;
            else if (direction > 0) playerAnimation = 3;
            else if (m_previousHorizontal < 0) playerAnimation = 2;
            else if (m_previousHorizontal > 0) playerAnimation = 4;
            if (direction != 0) m_previousHorizontal = direction;
        }

        const float elapsed = timeScale >= .99f ? 1.0f : timeScale;
        if (cameraState == 2) {
            m_cameraClock = num::add(m_cameraClock, elapsed);
            if (m_cameraClock >= 60) {
                cameraState = 3;
                m_cameraClock = 1;
            }
            goto charge_phase;
        }
        if (cameraState == 3) {
            m_cameraClock = num::add(m_cameraClock, elapsed);
            if (m_cameraClock >= 20) {
                cameraState = 0;
                m_cameraClock = 0;
            }
            goto charge_phase;
        }

        float targetX = playerX, targetY = playerY;
        const Enemy *boss = nullptr;
        for (const auto &enemy : m_enemies)
            if (enemy.alive && enemy.boss) {
                boss = &enemy;
                break;
            }
        if (!boss) {
            targetY = num::sub(playerY, 64.0f);
        } else {
            // 0x430AB0 case 0 mirrors the retail camera follow. scene+728 is the
            // focus state: 1 while the focus key is held, 2 while it is held
            // together with the shutter. The camera offset is a polar vector
            // from the player toward the boss; in the free state the angle
            // chases the player's real displacement (or the boss when idle).
            if (focused) {
                if (shooting) {
                    m_cameraDistance = 56;
                } else {
                    const float cameraDistance = num::hypot(num::sub(playerX, cameraX),
                                                            num::sub(playerY, cameraY));
                    if (cameraDistance >= 56.0f) {
                        const float bossDistance = num::hypot(num::sub(boss->x, playerX),
                                                              num::sub(boss->y, playerY));
                        if (cameraDistance >= bossDistance) {
                            if (cameraDistance > bossDistance)
                                m_cameraDistance = num::sub(cameraDistance, 2.0f);
                        } else {
                            m_cameraDistance = num::add(cameraDistance, 2.0f);
                        }
                    } else {
                        m_cameraDistance = 56;
                    }
                    if (m_cameraDistance >= 88.0f) m_cameraDistance = 88;
                }
            } else {
                if (m_cameraDistance < 48.0f) {
                    if (m_cameraDistance >= 44.0f)
                        m_cameraDistance = num::add(m_cameraDistance, 1.0f);
                } else {
                    m_cameraDistance = num::sub(m_cameraDistance, 1.0f);
                }
                const float moveX = num::sub(playerX, m_previousPlayerX);
                const float moveY = num::sub(playerY, m_previousPlayerY);
                const float aim = num::add(num::mul(moveY, moveY), num::mul(moveX, moveX)) >= 0.1f
                    ? num::atan2(moveY, moveX)
                    : num::atan2(num::sub(boss->y, playerY), num::sub(boss->x, playerX));
                m_cameraAngle = num::add(
                    m_cameraAngle, num::mul(num::wrapAngle(num::sub(aim, m_cameraAngle)), .04f));
            }
            const float angle = focused
                ? num::atan2(num::sub(boss->y, playerY), num::sub(boss->x, playerX))
                : m_cameraAngle;
            targetX = num::add(playerX, num::mul(num::cos(angle), m_cameraDistance));
            targetY = num::add(playerY, num::mul(num::sin(angle), m_cameraDistance));
            m_previousPlayerX = playerX;
            m_previousPlayerY = playerY;
        }
        if (!m_charging) {
            // 0x430AB0 clamps the camera aim to [-176,176] x [16,432] after the
            // follow step; the wider bounds belong to the aiming state (0x4328C0).
            cameraX = num::clamp(
                num::add(cameraX, num::mul(num::sub(targetX, cameraX), .4f)), -176.0f, 176.0f);
            cameraY = num::clamp(
                num::add(cameraY, num::mul(num::sub(targetY, cameraY), .4f)), 16.0f, 432.0f);
            if (focused)
                m_cameraAngle = num::atan2(num::sub(cameraY, playerY), num::sub(cameraX, playerX));
        }
    }

charge_phase:
    {
        float baseRate = m_chargeClock >= 60
            ? .001875f
            : num::add(num::div(m_chargeClock, 48000.0f), .000625f);
        // Retail advances this accumulator by the scaled tick, not by one.
        m_chargeClock = num::add(m_chargeClock, timeScale >= .99f ? 1.0f : timeScale);
        const bool full = charge >= 1.0f;
        if (!full) {
            if (focused && shooting) {
                if (!m_charging) {
                    ++m_chargeHeld;
                    if (m_chargeHeld > 4) {
                        m_charging = true;
                        m_chargeClock = 0;
                        sound(42);
                    }
                }
                if (m_charging) {
                    // Retail keeps one frame counter for both the five-frame
                    // press window and the charge ramp; it spawns bullet.anm
                    // script 292 on the player once the counter passes 60.
                    if (m_chargeHeld > 60) {
                        m_effects.push_back({292, playerX, playerY});
                        // 0x433D10 creates a bullet.anm 292 instance, whose
                        // opcode 40 consumes one global word_4BE208 word.
                        (void)random();
                    }
                    ++m_chargeHeld;
                    const float rate = m_chargeHeld >= 70
                        ? .005f
                        : num::add(num::div(num::mul(float(m_chargeHeld), 3.0f), 56000.0f),
                                   .00125f);
                    charge = num::min(1.0f, num::add(charge, num::mul(rate, timeScale)));
                    if (charge >= 1.0f) {
                        charge = 1;
                        m_charging = false;
                        m_chargeHeld = 0;
                        sound(42, 0, true);
                        sound(43);
                    }
                }
            } else {
                if (m_charging) {
                    m_charging = false;
                    sound(42, 0, true);
                }
                // Retail clears the counter whenever focus and shot are not
                // both held, even while the passive charge keeps growing.
                m_chargeHeld = 0;
                const float before = charge;
                charge = num::min(1.0f, num::add(charge, num::mul(baseRate, timeScale)));
                if (before < 1.0f && charge >= 1.0f) sound(43);
            }
        } else {
            if (m_charging) {
                m_charging = false;
                m_chargeHeld = 0;
                sound(42, 0, true);
            }
            m_chargeHeld = 0;
            if (!focused && shooting && (input.shoot || m_shiftReleased > 4)) {
                ++frames;
                cameraState = 1;
                photoWidth = 256;
                photoHeight = 192;
                timeScale = .25f;
                // Retail resets the aiming timer, then the same frame advances it
                // by the scaled tick.
                m_cameraClock = .25f;
                m_targetInFrame = false;
                sound(44);
                if (targetInFrame()) {
                    m_targetInFrame = true;
                    sound(46);
                }
            }
        }
    }

actors:
    const bool shutterFrozenEarly = cameraState == 2 || cameraState == 3;
    while (!shutterFrozenEarly && m_timeline < m_ecl.timeline.size()) {
        const auto &command = m_ecl.timeline[m_timeline];
        if (m_clock < float(command.time)) break;
        ++m_timeline;
        if (command.opcode > 1) {
            recordFault(vm::Fault::UnsupportedInstruction, command.opcode, command.offset);
            continue;
        }
        spawn(command.integer(0), command.number(0), command.number(1), nullptr);
    }

    // Retail raises level+252 bit 0 on the shutter frame (the trace shows
    // 0x101 right after the release), which makes 0x430180, 0x416290 and
    // 0x4059C0 return before running the level, enemy and bullet tasks. The
    // shutter result page therefore moves nothing: the erased set is exactly the
    // frozen capture set. Only the photo manager (0x42C3E0) keeps running.
    const bool shutterFrozen = shutterFrozenEarly;
    if (!shutterFrozen) {
        for (std::size_t i = 0; i < m_enemies.size(); ++i) {
            auto &enemy = m_enemies[i];
            if (!enemy.alive) continue;
            if (enemy.spawnCircle) {
                enemy.spawnCircleTimer = num::sub(enemy.spawnCircleTimer, timeScale);
                if (timeScale >= enemy.spawnCircleTimer) {
                    enemy.spawnCircleTimer = 0;
                    enemy.spawnCircle = false;
                }
            }
            run(enemy, enemy.main);
            for (auto &thread : enemy.async)
                if (enemy.alive) run(enemy, thread);
            if (enemy.alive) move(enemy);
        }
    }

    bool collided = false;
    const bool canCollide = !invulnerable && frames > 60 && !shutterFrozen;
    for (auto &bullet : m_bullets) {
        if (!bullet.alive) continue;
        if (!shutterFrozen) tickBullet(bullet);
        if (!canCollide || bullet.grace > 0 || !bullet.alive) continue;
        const float reach = num::add(m_hit, bullet.radius);
        if (std::fabs(num::sub(bullet.x, playerX)) <= reach &&
            std::fabs(num::sub(bullet.y, playerY)) <= reach)
            collided = true;
    }
    if (canCollide) {
        for (const auto &enemy : m_enemies) {
            if (!enemy.alive || !enemy.collision) continue;
            if (std::fabs(num::sub(enemy.x, playerX)) <=
                    num::add(num::mul(enemy.hitWidth, .5f), m_hit) &&
                std::fabs(num::sub(enemy.y, playerY)) <=
                    num::add(num::mul(enemy.hitHeight, .5f), m_hit)) {
                collided = true;
                break;
            }
        }
    }
    if (collided) {
        dead = true;
        ++collisionEvent;
        timeScale = .5f;
        failureAge = 0;
        collisionX = playerX;
        collisionY = playerY;
        sound(4);
        // 0x4306D0 starts 32 bullet.anm 290 sparks; each one's opcode 40
        // consumes a global draw before the retry (or the demo) continues.
        for (int i = 0; i < 32; ++i) (void)random();
    }

    m_bullets.erase(std::remove_if(m_bullets.begin(), m_bullets.end(),
                                   [](const auto &bullet) { return !bullet.alive; }),
                    m_bullets.end());
    m_enemies.erase(std::remove_if(m_enemies.begin(), m_enemies.end(),
                                   [](const auto &enemy) { return !enemy.alive; }),
                    m_enemies.end());

    if (!dead) {
        if (remaining > 0) {
            const float previous = remaining;
            remaining = num::max(0.0f, num::sub(remaining, timeScale));
            if (int(previous) / 60 != int(remaining) / 60 && int(remaining) <= 659)
                sound(int(remaining) <= 359 ? 36 : 27);
            if (remaining <= 0) {
                dead = true;
                failureAge = 30;
            }
        }
    }
    if (dead || cleared) {
        focused = false;
        m_charging = false;
        sound(42, 0, true);
        sound(44, 0, true);
    }
    m_clock = num::add(m_clock, timeScale);
}

bool LevelSimulation::targetInFrame() const
{
    // Retail 0x4339F0 scans the boss slot table with a fixed four-pixel half
    // box; the aiming reticle warns about a registered, visible, non-declaring
    // boss, not about the satellite enemies.
    for (const auto &enemy : m_enemies) {
        if (!enemy.alive || !enemy.boss || enemy.hidden || enemy.spawnCircle) continue;
        if (std::fabs(num::sub(enemy.x, cameraX)) + 4 <= photoWidth * .5f &&
            std::fabs(num::sub(enemy.y, cameraY)) + 4 <= photoHeight * .5f)
            return true;
    }
    return false;
}

} // namespace th95::scene
