#include "level.hpp"
#include "scene_table.hpp"
#include "../log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace th95::scene {
namespace {
// Button bits stored three times per logic tick: held, pressed, released.
// 0x434830 records word_4BE218[0] / word_4BE21E / word_4BE24C, which
// 0x4353B0 derives from the retail input word:
//   bit0 focus (X; Shift writes 0x5), bit1 shoot (Z), bit3 pause (Esc),
//   bit4..7 directions, bit10 S, bit15 L.
// Gameplay (0x42F190/0x430AB0/0x4328C0/0x433D10) only tests bit0, bit1 and
// the four direction bits, so 0x1 is the focus flag, not cancel.
constexpr uint16_t kFocus = 0x1;
constexpr uint16_t kShoot = 0x2;
constexpr uint16_t kPause = 0x8;
constexpr uint16_t kUp = 0x10, kDown = 0x20, kLeft = 0x40, kRight = 0x80;
constexpr uint16_t kShowRate = 0x400, kLockBest = 0x8000;

uint16_t heldMask(const platform::InputState &input)
{
    uint16_t mask = 0;
    if (input.upHeld || input.up || input.upRepeat) mask |= kUp;
    if (input.downHeld || input.down || input.downRepeat) mask |= kDown;
    if (input.leftHeld || input.left || input.leftRepeat) mask |= kLeft;
    if (input.rightHeld || input.right || input.rightRepeat) mask |= kRight;
    if (input.shootHeld || input.shoot) mask |= kShoot;
    if (input.shiftHeld) mask |= kFocus;
    if (input.pause) mask |= kPause;
    if (input.showRate) mask |= kShowRate;
    if (input.lockBest) mask |= kLockBest;
    return mask;
}

void decodeMask(uint16_t held, uint16_t pressed, platform::InputState &input)
{
    input = platform::InputState{};
    input.upHeld = (held & kUp) != 0;
    input.downHeld = (held & kDown) != 0;
    input.leftHeld = (held & kLeft) != 0;
    input.rightHeld = (held & kRight) != 0;
    input.up = (pressed & kUp) != 0;
    input.down = (pressed & kDown) != 0;
    input.left = (pressed & kLeft) != 0;
    input.right = (pressed & kRight) != 0;
    // Menus read the live word_4BE21E, not the injected 244/24A words, so the
    // recorded confirm/cancel bits must not be treated as menu input.
    input.shoot = (pressed & kShoot) != 0;
    input.shootHeld = (held & kShoot) != 0;
    input.shiftHeld = (held & kFocus) != 0;
    input.pause = (pressed & kPause) != 0;
    input.showRate = (pressed & kShowRate) != 0;
    input.lockBest = (pressed & kLockBest) != 0;
}
} // namespace

void LevelScene::beginReplayRecording()
{
    m_replay = format::Replay{};
    const SceneEntry *entry = sceneEntry(m_mission);
    if (!entry) {
        return;
    }
    auto &header = m_replay.header;
    header.sceneId = uint16_t(entry->id);
    header.level = uint8_t(entry->level);
    header.scene = uint8_t(entry->scene);
    header.seed = m_sim.seed();
    if (m_gameConfig) {
        header.config = m_gameConfig->bytes();
    }
    m_replayTick = 0;
    m_replayHeld = 0;
}

void LevelScene::captureReplayTick(const platform::InputState &input)
{
    if (m_replayMode != ReplayMode::Record) {
        return;
    }
    const uint16_t held = heldMask(input);
    // Retail stores held, pressed and released as three u16 words and samples
    // the FPS byte before the first input word (frame counter % 30 == 0).
    if (m_replayTick % 30 == 0) {
        m_replay.trailer.push_back(60);
    }
    const uint16_t pressed = uint16_t(held & ~m_replayHeld);
    const uint16_t released = uint16_t(m_replayHeld & ~held);
    const std::size_t at = m_replay.input.size();
    m_replay.input.resize(at + 6);
    auto put = [&](std::size_t offset, uint16_t value) {
        m_replay.input[at + offset] = uint8_t(value);
        m_replay.input[at + offset + 1] = uint8_t(value >> 8);
    };
    put(0, held);
    put(2, pressed);
    put(4, released);
    m_replayHeld = held;
    ++m_replayTick;
}

bool LevelScene::advanceReplay(platform::InputState &input)
{
    if (m_replayMode != ReplayMode::Play) {
        return true;
    }
    const std::size_t at = m_replayTick * 6;
    if (at + 6 > m_replay.input.size()) {
        return false;
    }
    const auto get = [&](std::size_t offset) {
        return uint16_t(m_replay.input[at + offset]) |
               uint16_t(m_replay.input[at + offset + 1]) << 8;
    };
    // Words are held, pressed, released; the pressed word is the second one.
    decodeMask(get(0), get(1), input);
    ++m_replayTick;
    return true;
}

void LevelScene::restartReplayPlayback()
{
    // "再挑戦/再撮影" during playback restarts the scene from the recorded seed
    // and rewinds the stream instead of resuming at the current read position.
    reset();
    m_sim.setSeed(m_replay.header.seed);
    m_replayTick = 0;
    m_replayHeld = 0;
}

void LevelScene::stampReplayResult()
{
    if (m_replayMode != ReplayMode::Record) {
        return;
    }
    auto &header = m_replay.header;
    header.timestamp = uint32_t(std::time(nullptr));
    header.score = uint32_t(std::max(0, m_sim.score));
    const double total = double(m_presentedFrames) + m_slowFrames;
    header.rate = total > 0 ? float(100.0 * m_slowFrames / total) : 0.0f;
    header.inputLength = uint32_t(m_replay.input.size());
    header.trailerLength = uint32_t(m_replay.trailer.size());
}

void LevelScene::rebuildNumberedSlots()
{
    const auto directory = m_dataDirectory.empty() ? std::filesystem::current_path()
                                                   : m_dataDirectory;
    const auto slots = format::scanReplays(directory);
    for (int i = 0; i < format::kReplayNumberedSlots; ++i) {
        m_replaySlots[std::size_t(i)] = slots[std::size_t(i)];
    }
}

void LevelScene::openReplaySaveList()
{
    // Retail state 5/4 stamps the header before opening state 13; the list
    // then shows the freshly recorded slot as empty until it is written.
    stampReplayResult();
    rebuildNumberedSlots();
    m_replaySelection = 0;
    m_menuKind = MenuKind::ReplayList;
    m_menuTicks = 0;
}

void LevelScene::updateReplaySave(const platform::InputState &input)
{
    ++m_menuTicks;
    if (m_menuKind == MenuKind::ReplayList) {
        // Retail state 13 walks the vertical list with 0x10/0x20 (up/down).
        if (input.up || input.upRepeat || input.down || input.downRepeat) {
            const int step = (input.up || input.upRepeat) ? -1 : 1;
            m_replaySelection =
                (m_replaySelection + step + format::kReplayNumberedSlots) %
                format::kReplayNumberedSlots;
            playMenuSound(12);
        }
        if (input.confirm) {
            // Retail state 13 accepts every slot, empty or occupied: the number
            // page only chooses a file name, and the write happens in state 15.
            // Gating on an empty slot made saving impossible on a fresh install.
            playMenuSound(10);
            std::string name = m_profile ? m_profile->replayName() : std::string();
            m_replayName.fill(' ');
            std::copy_n(name.begin(), std::min<std::size_t>(name.size(), 8),
                        m_replayName.begin());
            // 0x426BF0 state 0xD strcpy's the last name into the edit buffer
            // but never resets the length counter at +4536; only a name that
            // is not eight spaces moves the cursor onto the confirm cell.
            if (!name.empty()) {
                m_replayNameCursor = 95;
            }
            m_menuKind = MenuKind::ReplayName;
            m_menuTicks = 0;
            return;
        }
        // Retail tests confirm first and cancel in the else branch.
        if (input.cancel || input.pause) {
            playMenuSound(11);
            // 0x426BF0 state 0xD cancel re-creates the title and option rows of
            // the result page (the panel stays). Success restarts 11/14/15/12/13
            // and re-runs 0x4292D0 for the best-photo cursor; failure restarts
            // 7/9/10/8. Without this the rows stay on their label-1 fade-out.
            if (m_menuCount == 6) {
                const int scripts[] = {11, 12, 13, 14, 15};
                for (int i = 0; i < 5; ++i) {
                    m_menu[std::size_t(i + 1)].start(m_pause, scripts[i]);
                    m_menu[std::size_t(i + 1)].update();
                }
                m_bestSlot = runtimeBestSlot();
                m_successPhotos = std::clamp(m_sim.photos, 0, 10);
                m_successBrowse = m_bestSlot >= 0 && m_bestSlot < m_successPhotos;
                if (m_bestSlot >= 0) {
                    m_bestPhoto.start(m_photo, 2 * m_bestSlot + 1);
                    m_bestPhoto.update();
                }
                m_menuKind = MenuKind::Success;
            } else {
                const int scripts[] = {7, 8, 9, 10};
                for (int i = 0; i < 4; ++i) {
                    m_menu[std::size_t(i + 1)].start(m_pause, scripts[i]);
                    m_menu[std::size_t(i + 1)].update();
                }
                // State 0xD's failure branch also clears the name-length
                // counter; the success branch leaves it alone.
                m_replayNameLength = 0;
                m_menuKind = MenuKind::Failure;
            }
            m_menuTicks = 0;
            m_menuAction = MenuAction::None;
            m_menuExitTicks = 0;
            return;
        }
        return;
    }

    // Replay Name Regist. The palette is drawn as 16 columns by 6 rows
    // (index = col + 16 * row at 0x429C80), so up/down move exactly one row
    // and left/right wrap inside the row. Retail used +-4 with a 24 step
    // (0x426BF0 case 0xE), which skips cells and lands off the drawn grid.
    if (input.up || input.upRepeat) {
        m_replayNameCursor -= 16;
        if (m_replayNameCursor < 0) m_replayNameCursor += 96;
    }
    if (input.down || input.downRepeat) {
        m_replayNameCursor += 16;
        if (m_replayNameCursor >= 96) m_replayNameCursor -= 96;
    }
    if (input.left || input.leftRepeat) {
        m_replayNameCursor =
            m_replayNameCursor % 16 != 0 ? m_replayNameCursor - 1 : m_replayNameCursor + 15;
    }
    if (input.right || input.rightRepeat) {
        ++m_replayNameCursor;
        if (m_replayNameCursor % 16 == 0) m_replayNameCursor -= 16;
    }
    if (input.confirm) {
        if (m_replayNameCursor == 95) {
            playMenuSound(10);
            char file[24];
            std::snprintf(file, sizeof(file), "th95_%.2d.rpy", m_replaySelection + 1);
            const auto directory = m_dataDirectory.empty() ? std::filesystem::current_path()
                                                           : m_dataDirectory;
            const std::string name(m_replayName.begin(), m_replayName.end());
            if (format::writeReplay(directory / "replay" / file, m_replay, name)) {
                if (m_profile) m_profile->setReplayName(name);
            } else {
                log::print("warning : replay write failed: %s\r\n", file);
            }
            rebuildNumberedSlots();
            m_menuKind = MenuKind::ReplayList;
            m_menuTicks = 0;
        } else if (m_replayNameCursor == 94) {
            playMenuSound(11);
            if (m_replayNameLength != 7 || m_replayName[7] == ' ') {
                --m_replayNameLength;
                if (m_replayNameLength < 0) m_replayNameLength = 0;
                m_replayName[std::size_t(m_replayNameLength)] = ' ';
            } else {
                m_replayName[7] = ' ';
            }
        } else {
            playMenuSound(10);
            m_replayName[std::size_t(m_replayNameLength)] =
                kReplayCharacters[m_replayNameCursor];
            ++m_replayNameLength;
            if (m_replayNameLength >= 8) {
                m_replayNameLength = 7;
                m_replayNameCursor = 95;
            }
        }
    }
    if (input.cancel || input.pause) {
        playMenuSound(11);
        if (m_replayNameLength <= 0) {
            m_menuKind = MenuKind::ReplayList;
            m_menuTicks = 0;
        } else {
            --m_replayNameLength;
            m_replayName[std::size_t(m_replayNameLength)] = ' ';
        }
    }
}

} // namespace th95::scene
