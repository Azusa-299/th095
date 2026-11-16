#include "option.hpp"
#include <algorithm>
#include "../log.hpp"

namespace th95::scene {
namespace {
constexpr int kDigitBase = 0x66;   // sprite 102 = digit '0', 112 = '%'
constexpr int kGate = 30;          // retail routine's 30 tick interaction gate
constexpr int kExitTicks = 45;     // 20 tick slide-out plus settle
constexpr int kVolumeStep = 5;
constexpr int kVolumeMax = 100;
// retail routine sends interrupt 2/3 to all 23 animations grouped by row.
constexpr std::array<int, 23> kGroups{{0, 1, 2, 3, 4, 5, 6,
                                       0, 0, 1, 1, 2, 2, 3, 3,
                                       4, 4, 4, 4, 5, 5, 5, 5}};
constexpr std::array<int, 4> kBgmDigits{{15, 16, 17, 18}}; // scripts 122..125
constexpr std::array<int, 4> kSeDigits{{19, 20, 21, 22}};  // scripts 126..129
// The scanned slot table starts at DIK 1 (Esc) and includes DIK 28 (Enter).
// Mode 7 uses those two as the cancel/confirm edges, so they must never be
// captured into an editable slot.
constexpr int kMenuKeyEscape = 1;
constexpr int kMenuKeyEnter = 28;
}

void OptionScene::close()
{
    m_panels = {};
    m_heading = {};
    m_rows = {};
    m_title = nullptr;
    m_audio = nullptr;
    m_config = nullptr;
    m_device = nullptr;
    m_configPath.clear();
    m_stage = Stage::Closed;
    m_selected = 0;
    m_ticks = 0;
    m_frames = 0;
    m_lastKey = OptionInput::kNoKey;
    m_failed = false;
    m_headless = false;
    // m_restart survives close() so the caller can observe the request.
}

bool OptionScene::open(IDirect3DDevice9 *device, format::AnmTexture &title,
                       platform::Audio &audio, format::GameConfig &config,
                       const std::filesystem::path &configPath)
{
    close();
    m_device = device;
    m_title = &title;
    m_audio = &audio;
    m_config = &config;
    m_configPath = configPath;
    m_headless = (device == nullptr);
    m_restart = false;
    // retail routine state 0: panels 104/105 (second half +512), heading 106,
    // then 23 scripts 107..129 on layer 7.
    if (!m_panels[0].start(title, 104) || !m_panels[1].start(title, 105, 512) ||
        !m_heading.start(title, 106)) { close(); return false; }
    for (int i = 0; i < kRowScripts; ++i)
        if (!m_rows[std::size_t(i)].start(title, 107 + i)) { close(); return false; }
    // FUN_0041b660 pushes the previous list widget; mode 7 uses seven
    // wrapping rows starting at selection 0.
    m_selected = 0;
    m_ticks = 0;
    m_frames = 0;
    m_lastKey = OptionInput::kNoKey;
    m_failed = false;
    m_savedDisplayMode = config.displayMode();
    updateModeRows();
    redrawKeys();
    redrawVolume(config.bgmVolume(), 0);
    redrawVolume(config.seVolume(), 1);
    m_stage = Stage::Intro;
    log::print("info : option mode 7: 7 rows, scripts 104..129, display=%u bgm=%u se=%u keys=%u/%u/%u\r\n",
               unsigned(config.displayMode()), unsigned(config.bgmVolume()),
               unsigned(config.seVolume()), unsigned(config.keyBinding(0)),
               unsigned(config.keyBinding(1)), unsigned(config.keyBinding(2)));
    return true;
}

void OptionScene::updateModeRows()
{
    if (!m_config) return;
    const bool windowed = m_config->displayMode() != 0;
    // Interrupts 4/5 are alpha 255 / 64 on scripts 120 and 121.
    m_rows[13].interrupt(windowed ? 5 : 4);
    m_rows[14].interrupt(windowed ? 4 : 5);
}

void OptionScene::redrawKeys()
{
    if (!m_config) return;
    for (int slot = 0; slot < 3; ++slot) {
        const int value = m_config->keyBinding(slot);
        m_rows[std::size_t(7 + slot * 2)].setSprite(kDigitBase + value / 10);
        m_rows[std::size_t(8 + slot * 2)].setSprite(kDigitBase + value % 10);
    }
}

void OptionScene::redrawVolume(int value, int group)
{
    const auto &digits = group == 0 ? kBgmDigits : kSeDigits;
    // retail routine hides the hundreds/tens digit by clearing bit 1 of the
    // animation flags, and only rebinds the sprite when the digit is shown.
    if (value < 100) m_rows[std::size_t(digits[0])].setEnabled(false);
    else {
        m_rows[std::size_t(digits[0])].setEnabled(true);
        m_rows[std::size_t(digits[0])].setSprite(kDigitBase + (value / 100) % 10);
    }
    if (value < 10) m_rows[std::size_t(digits[1])].setEnabled(false);
    else {
        m_rows[std::size_t(digits[1])].setEnabled(true);
        m_rows[std::size_t(digits[1])].setSprite(kDigitBase + (value / 10) % 10);
    }
    m_rows[std::size_t(digits[2])].setSprite(kDigitBase + value % 10);
    // digits[3] keeps its retail sprite 112 ('%').
}

void OptionScene::adjust(int direction)
{
    if (!m_config) return;
    // Retail tests the 0x40 branch and the 0x80 branch independently, so a
    // tick with both held runs this twice (-5 then +5, or two mode toggles).
    if (m_selected == 3) {
        m_config->setDisplayMode(uint8_t(1 - m_config->displayMode()));
        updateModeRows();
        return;
    }
    if (m_selected != 4 && m_selected != 5) return;
    const int group = m_selected - 4;
    int value = group == 0 ? m_config->bgmVolume() : m_config->seVolume();
    if (direction > 0) {
        // if (value < 100) { value += 5; if (value > 100) value = 100; }
        if (value < kVolumeMax) value = std::min(value + kVolumeStep, kVolumeMax);
    } else {
        // if (value > 0) { value -= 5; if (value < 0) value = 0; }
        if (value > 0) value = std::max(value - kVolumeStep, 0);
    }
    if (group == 0) m_config->setBgmVolume(uint8_t(value));
    else m_config->setSeVolume(uint8_t(value));
    redrawVolume(value, group);
}

void OptionScene::highlight()
{
    for (int i = 0; i < kRowScripts; ++i)
        m_rows[std::size_t(i)].interrupt(kGroups[std::size_t(i)] == m_selected ? 2 : 3);
}

void OptionScene::animate()
{
    for (auto &panel : m_panels) panel.update();
    m_heading.update();
    for (auto &row : m_rows) row.update();
}

bool OptionScene::exitToTitle()
{
    for (auto &panel : m_panels) panel.interrupt(1);
    m_heading.interrupt(1);
    for (auto &row : m_rows) row.interrupt(1);
    // The shared exit branch always plays se 11. Pressing X on row 6 also
    // plays the cancel-branch se 11 in update(), matching retail's double.
    m_audio->playCancel();
    // The 18 edit bytes and the volume/mode bytes already live in the config
    // block; retail writes the file from the main loop, not from this task.
    m_restart = m_savedDisplayMode != m_config->displayMode();
    m_stage = Stage::Closing;
    m_ticks = 0;
    log::print("info : option exit: display=%u bgm=%u se=%u restart=%u\r\n",
               unsigned(m_config->displayMode()), unsigned(m_config->bgmVolume()),
               unsigned(m_config->seVolume()), unsigned(m_restart));
    return true;
}

bool OptionScene::update(const OptionInput &input)
{
    if (m_stage == Stage::Closed) return false;
    if (m_stage == Stage::Closing) {
        animate();
        if (++m_ticks >= kExitTicks) close();
        return false;
    }
    bool returned = false;
    if (m_ticks >= kGate) {
        if (m_stage == Stage::Intro) {
            m_stage = Stage::List;
            highlight();
        }
        const int before = m_selected;
        // Retail uses only DAT_004be21e bits 0x10/0x20 here: no repeat mask.
        if (input.up) m_selected = (m_selected + kRowCount - 1) % kRowCount;
        if (input.down) m_selected = (m_selected + 1) % kRowCount;
        if (before != m_selected) {
            m_audio->playSelect();
            highlight();
        } else {
            // retail routine evaluates the 0x40 and 0x80 masks as two separate
            // branches; both can fire on the same tick (-5 then +5, or two
            // display-mode toggles). A merged branch is not equivalent.
            if (input.left || input.leftRepeat) adjust(-1);
            if (input.right || input.rightRepeat) adjust(+1);
            // DAT_004ca1ac / DAT_004ca1b0 are refreshed every tick in this
            // branch, whether or not a volume key was pressed.
            m_audio->setVolumes(m_config->bgmVolume(), m_config->seVolume());
            // SE preview every 40 secondary ticks while on the SE row.
            if (m_selected == 5 && m_frames % 40 == 0) m_audio->playEffect(41);
            // FUN_00419910(0) returns the button state; scan for the first
            // 0x80 entry. DAT_004a5894 starts at 32 and is stored back on
            // every tick, including when no button is held. Retail's three
            // slots are gamepad button indices; the port samples the
            // keyboard slot table of the same layout, so Esc/Enter are
            // excluded to keep the menu edges out of the cfg.
            const int key = input.keyIndex;
            if (key >= 0 && key < OptionInput::kNoKey &&
                key != kMenuKeyEscape && key != kMenuKeyEnter &&
                key != m_lastKey && m_selected <= 2) {
                m_audio->playConfirm(); // se 10, only on rows 0/1/2
                const uint16_t old = m_config->keyBinding(m_selected);
                for (int slot = 0; slot < 3; ++slot)
                    if (slot != m_selected && m_config->keyBinding(slot) == key)
                        m_config->setKeyBinding(slot, old);
                m_config->setKeyBinding(m_selected, uint16_t(key));
                redrawKeys();
            }
            m_lastKey = (key >= 0 && key < OptionInput::kNoKey)
                            ? key : OptionInput::kNoKey;
            // Confirm (0x1002) takes priority over cancel (9) and is silent
            // on rows 0..5; cancel always plays se 11. Only row 6 exits,
            // and the shared exit path plays se 11 once more.
            if (input.confirm) {
                if (m_selected == 6) returned = exitToTitle();
            } else if (input.cancel) {
                m_audio->playCancel();
                if (m_selected == 6) returned = exitToTitle();
            }
        }
    }
    animate();
    ++m_ticks;
    ++m_frames;
    return returned;
}

void OptionScene::renderLayer(IDirect3DDevice9 *device, int layer) const
{
    if (m_stage == Stage::Closed) return;
    for (const auto &panel : m_panels) if (panel.layer() == layer) panel.draw(device);
    if (m_heading.layer() == layer) m_heading.draw(device);
    for (const auto &row : m_rows) if (row.layer() == layer) row.draw(device);
}
} // namespace th95::scene
