#pragma once
#include <array>
#include <filesystem>
#include "../formats/anm.hpp"
#include "../formats/config.hpp"
#include "../platform/audio.hpp"

namespace th95::scene {
// One tick of mode-7 input. keyIndex is the first pressed slot of the
// retail 32-button state array (retail routine): retail fills it from a gamepad,
// while the port fills the same layout from its keyboard table. kNoKey means
// no button held; DAT_004a5894 is initialised to 32 in the PE image.
struct OptionInput {
    static constexpr int kNoKey = 32;
    bool up = false, down = false, left = false, right = false;
    bool leftRepeat = false, rightRepeat = false;
    // Retail mode 7 ignores the up/down repeat mask (only DAT_004be21e
    // 0x10/0x20 are tested). Kept so fixtures can assert that behaviour.
    bool upRepeat = false, downRepeat = false;
    bool confirm = false, cancel = false;
    int keyIndex = kNoKey;
};
// Title mode 7, retail retail routine. Shares the title animation manager and
// common layers with Help (mode 9) and Music Room (mode 8).
class OptionScene {
public:
    enum class Stage { Closed, Intro, List, Closing };
    static constexpr int kRowScripts = 23;
    static constexpr int kRowCount = 7;

    bool open(IDirect3DDevice9 *device, format::AnmTexture &title,
              platform::Audio &audio, format::GameConfig &config,
              const std::filesystem::path &configPath);
    // Returns true once, on the tick that restores title mode 1.
    bool update(const OptionInput &input = {});
    void renderLayer(IDirect3DDevice9 *device, int layer) const;
    void close();
    bool active() const { return m_stage != Stage::Closed; }
    Stage stage() const { return m_stage; }
    int selected() const { return m_selected; }
    int ticks() const { return m_ticks; }
    bool restartRequested() const { return m_restart; }
    bool failed() const { return m_failed; }
    uint8_t displayMode() const { return m_config ? m_config->displayMode() : 0; }
    uint8_t bgmVolume() const { return m_config ? m_config->bgmVolume() : 0; }
    uint8_t seVolume() const { return m_config ? m_config->seVolume() : 0; }
    uint16_t keyBinding(int slot) const { return m_config ? m_config->keyBinding(slot) : 0; }
    const format::AnmAnimation &panel(int index) const { return m_panels.at(std::size_t(index)); }
    const format::AnmAnimation &heading() const { return m_heading; }
    const format::AnmAnimation &row(int index) const { return m_rows.at(std::size_t(index)); }

private:
    void highlight();
    void updateModeRows();
    void redrawKeys();
    void redrawVolume(int value, int group);
    void adjust(int direction);
    void animate();
    bool exitToTitle();
    IDirect3DDevice9 *m_device = nullptr;
    format::AnmTexture *m_title = nullptr;
    platform::Audio *m_audio = nullptr;
    format::GameConfig *m_config = nullptr;
    std::filesystem::path m_configPath;
    std::array<format::AnmAnimation, 2> m_panels{};
    format::AnmAnimation m_heading;
    std::array<format::AnmAnimation, kRowScripts> m_rows{};
    Stage m_stage = Stage::Closed;
    int m_selected = 0, m_ticks = 0, m_frames = 0;
    int m_lastKey = OptionInput::kNoKey;
    uint8_t m_savedDisplayMode = 0;
    bool m_restart = false, m_failed = false, m_headless = false;
};
} // namespace th95::scene
