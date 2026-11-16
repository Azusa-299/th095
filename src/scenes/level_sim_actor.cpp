#include "level_sim.hpp"
#include "bullet_catalog.hpp"
#include "../numeric/numeric.hpp"
#include <algorithm>
#include <cmath>

namespace th95::scene {
namespace {

float easing(float x, int mode)
{
    x = numeric::clamp(x, 0.0f, 1.0f);
    if (mode >= 1 && mode <= 3) return numeric::pow(x, float(mode + 1));
    if (mode >= 4 && mode <= 6) return numeric::sub(1.0f, numeric::pow(numeric::sub(1.0f, x),
                                                                      float(mode - 2)));
    return x;
}

} // namespace

void LevelSimulation::move(Enemy &enemy)
{
    const float oldX = enemy.x, oldY = enemy.y;
    if (enemy.moveMode == 1) {
        enemy.angle = numeric::wrapAngle(
            numeric::add(enemy.angle, numeric::mul(enemy.angularVelocity, timeScale)));
        enemy.speed = numeric::add(enemy.speed, numeric::mul(enemy.acceleration, timeScale));
        enemy.x = numeric::add(enemy.x,
                               numeric::mul(numeric::mul(numeric::cos(enemy.angle), enemy.speed),
                                            timeScale));
        enemy.y = numeric::add(enemy.y,
                               numeric::mul(numeric::mul(numeric::sin(enemy.angle), enemy.speed),
                                            timeScale));
    }
    if (enemy.moveMode == 2) {
        enemy.moveTime = numeric::add(enemy.moveTime, timeScale);
        const float progress = easing(enemy.moveDuration > 0
                                          ? numeric::div(enemy.moveTime, enemy.moveDuration)
                                          : 1.0f,
                                      enemy.ease);
        enemy.x = numeric::add(enemy.startX, numeric::mul(enemy.deltaX, progress));
        enemy.y = numeric::add(enemy.startY, numeric::mul(enemy.deltaY, progress));
        if (enemy.moveTime >= enemy.moveDuration) enemy.moveMode = 0;
    }
    if (enemy.boundsEnabled) {
        enemy.x = numeric::clamp(enemy.x, enemy.minX, enemy.maxX);
        enemy.y = numeric::clamp(enemy.y, enemy.minY, enemy.maxY);
    }
    enemy.vx = numeric::sub(enemy.x, oldX);
    enemy.vy = numeric::sub(enemy.y, oldY);
    if (enemy.boss) {
        const int direction = enemy.vx < -.01f ? 1 : enemy.vx > .01f ? 2 : 0;
        if (direction != enemy.lastDirection) {
            enemy.animation = direction == 1 ? 5 : direction == 2 ? 6
                : enemy.lastDirection == 1 ? 3 : enemy.lastDirection == 2 ? 4 : 2;
            enemy.lastDirection = direction;
        }
    }
    enemy.cooldown = numeric::max(0.0f, numeric::sub(enemy.cooldown, timeScale));
    enemy.warning = numeric::max(0.0f, numeric::sub(enemy.warning, timeScale));
    if (enemy.x > -220 && enemy.x < 220 && enemy.y > -32 && enemy.y < 480) enemy.entered = true;
    // Retail only runs this "left the field, free the slot" test while the
    // enemy's +11252 bit 0x4000000 is clear, which ECL 0x51 bit 4 enables.
    // Satellite enemies (LV1-3 scripts 5/6) set it so they disappear once they
    // fly out; the boss keeps the check disabled.
    if (enemy.boundsCheck && !enemy.boss && enemy.entered &&
        (enemy.x < -256 || enemy.x > 256 || enemy.y < -80 || enemy.y > 528))
        enemy.alive = false;
}

void LevelSimulation::tickBullet(Bullet &bullet)
{
    bullet.age = numeric::add(bullet.age, timeScale);
    bool appearing = false;
    if (bullet.appearance > 0) {
        bullet.x = numeric::add(bullet.x,
                                numeric::mul(numeric::mul(bullet.vx, timeScale), .5f));
        bullet.y = numeric::add(bullet.y,
                                numeric::mul(numeric::mul(bullet.vy, timeScale), .5f));
        bullet.appearance = numeric::sub(bullet.appearance, timeScale);
        // 0x405120 moves a state-2 bullet at half speed, then checks the
        // appearance counter at +284: while it is still positive the state flips
        // to 1 and the same frame also runs the normal extension step and the
        // full move, so the transition frame advances 1.5 steps.
        appearing = bullet.appearance > 0;
    }
    if (!appearing) {
        // A zero concurrency field waits for all running effects. Timed
        // effects start one per tick; immediate operations can chain.
        for (int budget = 0; budget < 18 && bullet.extension < 18; ++budget) {
            const auto extension = bullet.extensions[bullet.extension];
            if (!extension.flags || (!extension.concurrent && !bullet.active.empty())) break;
            ++bullet.extension;
            if (!(extension.flags & bullet.flags)) continue;
            if (extension.flags == 8192) {
                bullet.grace = float(extension.a);
                continue;
            }
            if (extension.flags == 16384) {
                bullet.type = extension.a;
                bullet.color = extension.b;
                ++bullet.spriteRevision;
                continue;
            }
            if (extension.flags == 131072) {
                sound(extension.a, bullet.x);
                continue;
            }
            if ((extension.flags == 16 || extension.flags == 32) && bullet.extension > 1)
                sound(bullet.sound);
            Bullet::Active active;
            active.effect = extension;
            active.angle = extension.y <= -990 ? bullet.angle : extension.y;
            active.speed = extension.y <= -999 ? bullet.speed : extension.y;
            bullet.active.erase(
                std::remove_if(bullet.active.begin(), bullet.active.end(),
                               [&](const auto &entry) {
                                   return entry.effect.flags == extension.flags;
                               }),
                bullet.active.end());
            bullet.active.push_back(active);
            break;
        }
        for (auto &active : bullet.active) {
            const auto &effect = active.effect;
            if (effect.flags == 1) {
                const float speed = numeric::sub(
                    numeric::add(bullet.speed, 5.0f),
                    numeric::div(numeric::mul(5.0f, active.time), 16.0f));
                bullet.vx = numeric::mul(numeric::cos(bullet.angle), speed);
                bullet.vy = numeric::mul(numeric::sin(bullet.angle), speed);
            } else if (effect.flags == 16 && active.time < effect.a) {
                bullet.speed = numeric::add(bullet.speed, numeric::mul(effect.x, timeScale));
                bullet.vx = numeric::add(bullet.vx,
                                         numeric::mul(numeric::mul(numeric::cos(active.angle),
                                                                   effect.x), timeScale));
                bullet.vy = numeric::add(bullet.vy,
                                         numeric::mul(numeric::mul(numeric::sin(active.angle),
                                                                   effect.x), timeScale));
                if (std::abs(bullet.vx) > .0001f || std::abs(bullet.vy) > .0001f)
                    bullet.angle = numeric::atan2(bullet.vy, bullet.vx);
            } else if (effect.flags == 32 && active.time < effect.a) {
                bullet.speed = numeric::add(bullet.speed, numeric::mul(effect.x, timeScale));
                bullet.angle = numeric::wrapAngle(
                    numeric::add(bullet.angle, numeric::mul(effect.y, timeScale)));
                bullet.vx = numeric::mul(numeric::cos(bullet.angle), bullet.speed);
                bullet.vy = numeric::mul(numeric::sin(bullet.angle), bullet.speed);
            } else if (effect.flags == 64) {
                float speed = bullet.speed;
                if (active.time < effect.a)
                    speed = numeric::sub(speed,
                                         numeric::div(numeric::mul(active.time, bullet.speed),
                                                      float(std::max(1, effect.a))));
                else {
                    sound(bullet.sound);
                    ++active.repeats;
                    bullet.angle = numeric::wrapAngle(numeric::add(bullet.angle, effect.x));
                    bullet.speed = active.speed;
                    speed = bullet.speed;
                    active.time = 0;
                }
                bullet.vx = numeric::mul(numeric::cos(bullet.angle), speed);
                bullet.vy = numeric::mul(numeric::sin(bullet.angle), speed);
            }
            active.time = numeric::add(active.time, timeScale);
        }
        bullet.active.erase(
            std::remove_if(bullet.active.begin(), bullet.active.end(), [](const auto &active) {
                return active.effect.flags == 1 ? active.time > 16
                    : active.effect.flags == 64 ? active.repeats >= active.effect.b
                                                : active.time >= active.effect.a;
            }),
            bullet.active.end());
        bullet.x = numeric::add(bullet.x, numeric::mul(bullet.vx, timeScale));
        bullet.y = numeric::add(bullet.y, numeric::mul(bullet.vy, timeScale));
    }
    const bool grace = std::any_of(bullet.active.begin(), bullet.active.end(),
                                   [](const auto &active) {
                                       return active.effect.flags == 32768;
                                   });
    const float margin = bullet.type == 17 ? 32.0f : bullet.type >= 12 ? 16.0f : 8.0f;
    if (!grace && (bullet.x + margin <= -192 || bullet.x - margin >= 192 ||
                   bullet.y + margin <= 0 || bullet.y - margin >= 448))
        bullet.alive = false;
}

} // namespace th95::scene
