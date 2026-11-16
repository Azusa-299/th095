#include "level_sim.hpp"
#include "bullet_catalog.hpp"
#include "../numeric/numeric.hpp"
#include <algorithm>
#include <cmath>

namespace th95::scene {
namespace num = th95::numeric;

void LevelSimulation::photograph()
{
    const float halfWidth = num::mul(photoWidth, .5f);
    const float halfHeight = num::mul(photoHeight, .5f);
    photoTargets = 0;
    photoBullets = 0;
    // Retail scores the shutter frame (0x407820 builds the list, 0x433140 scores
    // it) but erases the bullets later: the photo manager (0x42AD60 state 2)
    // runs 0x407C90 three frames after the release and then every frame, so the
    // shutter itself takes no global RNG draws.
    std::vector<unsigned> photographed;
    float closest = 999.0f;
    // 0x4339F0 returns the highest remaining-warning fraction among the enemies
    // inside the shutter; +11304 is the countdown that 0x41B830 advances and
    // +11316 is the duration ECL opcode 0x90 stored, so the fraction falls from
    // one to zero while the warning ring is drawn.
    float warningRatio = 0;
    for (auto &enemy : m_enemies) {
        // Retail 0x4339F0 walks the eight boss slots (ECL 109), rejecting an
        // enemy whose +11252 bit 0x10 is set, whose +11252 bit 0x20 is set, or
        // that still owns a declaration circle (+11256 bit 0x40).
        if (!enemy.alive || !enemy.boss || enemy.hidden || enemy.spawnCircle)
            continue;
        if (num::add(std::fabs(num::sub(enemy.x, cameraX)),
                     num::mul(enemy.hitWidth, .5f)) > halfWidth ||
            num::add(std::fabs(num::sub(enemy.y, cameraY)),
                     num::mul(enemy.hitHeight, .5f)) > halfHeight)
            continue;
        ++photoTargets;
        photographed.push_back(enemy.id);
        if (enemy.boss) ++enemy.photoCount;
        closest = std::min(closest, num::hypot(num::sub(enemy.x, cameraX),
                                              num::sub(enemy.y, cameraY)));
        // 0x4339F0 only samples enemies whose countdown integer (+11308) is
        // still positive.
        if (int(enemy.warning) > 0 && enemy.warningDuration > 0)
            warningRatio = std::max(warningRatio, num::div(enemy.warning, enemy.warningDuration));
    }
    std::vector<size_t> erased;
    for (size_t index = 0; index < m_bullets.size(); ++index) {
        const Bullet &bullet = m_bullets[index];
        // 0x407820/0x407C90 test AABB overlap, not containment: the bullet box
        // (half size radius) only has to touch the shutter rectangle.
        if (!bullet.alive) continue;
        if (num::sub(std::fabs(num::sub(bullet.x, cameraX)), bullet.radius) > halfWidth ||
            num::sub(std::fabs(num::sub(bullet.y, cameraY)), bullet.radius) > halfHeight)
            continue;
        ++photoBullets;
        erased.push_back(index);
    }
    photoSuccess = photoTargets > 0;
    photoSelf = num::add(std::fabs(num::sub(playerX, cameraX)), 8.0f) <= halfWidth &&
                num::add(std::fabs(num::sub(playerY, cameraY)), 8.0f) <= halfHeight;
    if (closest >= 96)
        photoDistanceMultiplier = 1.2f;
    else if (closest > 8)
        photoDistanceMultiplier = num::sub(
            2.0f, num::mul(num::div(num::sub(closest, 8.0f), 88.0f), .8f));
    else
        photoDistanceMultiplier = 2.0f;

    // 0x433140 walks the captured list and scores each bullet from the height of
    // its current bullet.anm sprite (object +584 -> +52): above 8 it pays 20,
    // above 16 it pays 40 and above 32 up to 64 it pays 150. The base is then
    // scaled by the speed at object +756: 2..6 grows it by (speed-1x), 6 and up
    // quadruples it, and the result is floored to ten. Incidental: retail leaves
    // v21 untouched when the height exceeds 64, so that branch carries the
    // previous bullet's base forward.
    int carried = 10;
    int64_t total = 0;
    for (size_t index : erased) {
        const Bullet &bullet = m_bullets[index];
        const int script = bulletScript(bullet.type, bullet.color);
        float height = 16.0f;
        const auto tier = m_bulletTierHeights.find(script);
        if (tier != m_bulletTierHeights.end())
            height = bullet.appearance > 0 ? tier->second.appearing : tier->second.normal;
        else if (bullet.appearance > 0)
            height = 30.0f;
        int points = carried;
        if (height > 8.0f) {
            if (height > 16.0f) {
                if (height > 32.0f) {
                    if (height <= 64.0f) points = 150;
                } else {
                    points = 40;
                }
            } else {
                points = 20;
            }
        } else {
            points = 10;
        }
        carried = points;
        if (bullet.speed < 6.0f) {
            if (bullet.speed >= 2.0f)
                points += int(double(points) * double(bullet.speed - 2.0f) * 4.0 / 4.0);
        } else {
            points *= 4;
        }
        points -= points % 10;
        total += points;
    }
    // 0x433140 passes the target count and captured-bullet count separately, but
    // the shutter call passes zero for both: the retail record for this scene
    // stores exactly the per-bullet sum (52 bullets -> 2480, no 170/10 terms).
    // 0x433140 pays the empty-frame bonus only when exactly one of the two
    // frame flags is set; a frame holding both the player and a target gets no
    // bonus even when it contains no bullets.
    if (photoBullets == 0 && (photoTargets > 0) != photoSelf) total += 100;
    // 0x433140 also groups the captured bullets by colour family (only types
    // 0..11 are inspected): 300 for every family holding 100 bullets, 900 when
    // three families hold twenty each, 2100 when all seven are represented.
    int families[7] = {};
    for (size_t index : erased) {
        const Bullet &bullet = m_bullets[index];
        if (bullet.type < 0 || bullet.type > 11) continue;
        int family = -1;
        if (bullet.color == 1 || bullet.color == 2) family = 0;
        else if (bullet.color == 3 || bullet.color == 4) family = 1;
        else if (bullet.color == 5 || bullet.color == 6) family = 2;
        else if (bullet.color == 7 || bullet.color == 8) family = 3;
        else if (bullet.color >= 9 && bullet.color <= 11) family = 4;
        else if (bullet.color == 12 || bullet.color == 13) family = 5;
        else if (bullet.color == 14) family = 6;
        if (family >= 0) ++families[family];
    }
    for (int count : families) if (count >= 100) total += 300;
    int groups = 0;
    for (int count : families) if (count >= 20) ++groups;
    if (groups >= 3) total += 900;
    groups = 0;
    for (int count : families) if (count >= 1) ++groups;
    if (groups >= 7) total += 2100;
    if (photoTargets > 0 && photoSelf) {
        total = int64_t(double(total) * photoDistanceMultiplier * 1.5 * 1.2f);
    } else if (photoTargets > 0) {
        total = int64_t(double(total) * photoDistanceMultiplier);
    } else if (photoSelf) {
        total = int64_t(double(total) * 1.2f);
    }
    // 0x433140 sets flag bit 3 when 0x4339F0 returned a warning fraction above
    // zero and then multiplies by a3[6]: 1.2 up to 1.5 as the fraction rises to
    // 0.8, and a flat 1.5 afterwards.
    if (warningRatio > 0) {
        const float factor = warningRatio < 0.8f
            ? num::add(num::div(num::mul(warningRatio, .3f), .8f), 1.2f)
            : 1.5f;
        total = int64_t(double(total) * double(factor));
    }
    total = int64_t(double(total) * double(scoreMultiplier));
    int64_t awarded64 = total - total % 10;
    if (awarded64 < INT32_MIN || awarded64 > INT32_MAX) awarded64 = 0;
    const int awarded = int(awarded64);

    ++attempts;
    if (photoSuccess) {
        ++photos;
        lastPhotoScore = awarded;
        score = std::max(0, numeric::wrapAdd(score, awarded));
    } else {
        // Retail stores a rejected shutter in slot 10 with a zeroed record.
        lastPhotoScore = 0;
    }
    highScore = std::max(highScore, score);

    for (auto &enemy : m_enemies) {
        if (std::find(photographed.begin(), photographed.end(), enemy.id) == photographed.end())
            continue;
        for (int slot = 0; slot < 10; ++slot) {
            if (enemy.photoThreshold[slot] < 0 || enemy.photoThreshold[slot] > photos) continue;
            enemy.main = Thread{};
            for (auto &thread : enemy.async) thread = Thread{};
            enemy.extensions = {};
            enemy.main.script = enemy.photoScripts[slot];
            enemy.photoThreshold[slot] = -1;
        }
    }

    // 0x407C90 erases the bullets captured by the shutter rectangle: each one
    // becomes explosion particle 294 plus a tinted copy of bullet.anm 288. The
    // photo manager only reaches that state three frames after the release, so
    // the erasure (and its two global draws per bullet) is deferred to
    // runShutterErase() instead of happening on the shutter tick.
    m_erasing = true;
    m_eraseTicks = 0;
    m_eraseX = cameraX;
    m_eraseY = cameraY;
    m_eraseHalfWidth = halfWidth;
    m_eraseHalfHeight = halfHeight;

    charge = 0;
    flash = 7;
    timeScale = 1;
    // The retail player speed scale is zero for the shutter result sequence;
    // the player stays put until the photo event sequence reaches frame 35.
    m_shutterHold = 35;
    cameraState = 2;
    // Retail resets the aiming timer, then the shutter frame advances it once.
    m_cameraClock = 1;
    photoResultTicks = 60;
    ++shutterEvent;
    sound(44, 0, true);
    sound(15);
    sound(41);
    if (photos >= requiredPhotos) {
        cleared = true;
        successAge = -20;
    }
}

void LevelSimulation::runShutterErase()
{
    // 0x407C90 walks the bullet slots and erases every one whose box still
    // overlaps the frozen shutter rectangle (0x407820 uses the same AABB test).
    // Each bullet spawns explosion particle 294 and one tinted bullet.anm 288
    // spark; 0x41D460 seeds that spark with vx = signed unit and vy = unit*2+2,
    // so each erased bullet advances the global stream by two draws.
    // The tint comes from the small-size table 0x4A41E0 indexed by the bullet
    // colour word; colours 16..23 read into the medium table at 0x4A4220.
    static const unsigned bulletTints[24] = {
        0xFF808080u, 0xFFFF1010u, 0xFFFF1010u, 0xFF801080u, 0xFF801080u,
        0xFF1010FFu, 0xFF1010FFu, 0xFF108080u, 0xFF108080u, 0xFF10FF10u,
        0xFF10FF10u, 0xFF10FF10u, 0xFF808010u, 0xFF808010u, 0xFF808010u,
        0xFF808080u,
        0xFF808080u, 0xFFFF1010u, 0xFF801080u, 0xFF1010FFu, 0xFF108080u,
        0xFF10FF10u, 0xFF808010u, 0xFF808080u};
    for (auto &bullet : m_bullets) {
        if (!bullet.alive) continue;
        if (num::sub(std::fabs(num::sub(bullet.x, m_eraseX)), bullet.radius) > m_eraseHalfWidth ||
            num::sub(std::fabs(num::sub(bullet.y, m_eraseY)), bullet.radius) > m_eraseHalfHeight)
            continue;
        const unsigned tint = bulletTints[std::clamp(bullet.color, 0, 23)];
        m_effects.push_back({294, bullet.x, bullet.y, 0, tint});
        m_effects.push_back({288, bullet.x, bullet.y, 0, tint});
        auto &spark = m_effects.back();
        spark.vx = nextSignedUnit();
        spark.vy = nextUnit() * 2.0f + 2.0f;
        // The 294 explosion script's t=2 opcode batch consumes seven global
        // words on the following tick.
        m_burstDraws += 7;
        bullet.alive = false;
    }
}

} // namespace th95::scene
