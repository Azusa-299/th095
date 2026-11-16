#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "../archive.hpp"
#include "../formats/anm.hpp"
#include "../formats/config.hpp"
#include "../formats/replay.hpp"
#include "../platform/audio.hpp"
#include "help.hpp"
#include "music_room.hpp"
#include "option.hpp"
#include "../platform/input.hpp"

namespace th95::scene {
class TitleScene {
public:
    enum class Preparation { Loading, Ready, Failed };
    bool load(Archive &archive, IDirect3DDevice9 *device, const std::filesystem::path &bgmPath);
    Preparation prepareNext(Archive &archive, IDirect3DDevice9 *device,
                            const std::filesystem::path &bgmPath);
    bool start();
    void update(double deltaSeconds, const platform::InputState &input = {});
    void render(IDirect3DDevice9 *device);
    void renderLayer(IDirect3DDevice9 *device,int layer);
    bool quitRequested() const { return m_quit; }
    bool startLevelRequested() const { return m_startLevel; }
    bool consumeStartLevel()
    {
        const bool requested = m_startLevel;
        m_startLevel = false;
        return requested;
    }
    bool missionSelectRequested() const { return m_missionSelect; }
    bool consumeMissionSelect()
    {
        const bool requested = m_missionSelect;
        m_missionSelect = false;
        return requested;
    }
    bool audioHasPlaybackProgress() const { return m_audio.hasPlaybackProgress(); }
    platform::Audio &audio() {return m_audio;}
    // retail routine reads th095.cfg at boot; retail routine writes it when the
    // retail main loop exits. The port keeps both in TitleScene.
    void configure(const std::filesystem::path &cfgPath,
                   const std::filesystem::path &bgmPath = {});
    bool saveConfig();
    format::GameConfig &config() { return m_config; }
    bool restartRequested() const { return m_restartRequested; }
    // Retail consumes the return-5 the moment retail routine handles it. Without
    // consuming here the flag stays latched and the main loop re-applies the
    // display mode on every later display-row toggle inside Option.
    void consumeRestartRequest() { m_restartRequested = false; }
    void enterMissionSelect();
    void leaveMissionSelect();
    // Fixture hooks for --help-test: the help task otherwise takes its input
    // from the same key sampling as the menu.
    bool openHelp();
    bool helpActive() const { return m_help.active(); }
    bool updateHelp(const HelpInput &input);
    int helpPage() const { return m_help.page(); }
    HelpScene::Stage helpStage() const { return m_help.stage(); }
    bool helpFailed() const { return m_help.failed(); }
    // Fixture hooks for --music-test: mode 8 is another title sub-mode.
    bool openMusicRoom();
    bool musicRoomActive() const {return m_musicRoom.active();}
    MusicRoomScene::Stage musicRoomStage() const {return m_musicRoom.stage();}
    int musicRoomSelected() const {return m_musicRoom.selected();}
    int musicRoomTicks() const {return m_musicRoom.ticks();}
    int musicRoomTrackCount() const {return m_musicRoom.trackCount();}
    const std::string &musicRoomTrack() const {return m_musicRoom.playing();}
    bool updateMusicRoom(const MusicRoomInput &input);
    // Fixture hooks for --option-test: mode 7 is another title sub-mode.
    bool openOption();
    bool optionActive() const {return m_option.active();}
    OptionScene::Stage optionStage() const {return m_option.stage();}
    int optionSelected() const {return m_option.selected();}
    int optionTicks() const {return m_option.ticks();}
    uint8_t optionDisplayMode() const {return m_option.displayMode();}
    uint8_t optionBgmVolume() const {return m_option.bgmVolume();}
    uint8_t optionSeVolume() const {return m_option.seVolume();}
    uint16_t optionKeyBinding(int slot) const {return m_option.keyBinding(slot);}
    bool optionRestartRequested() const {return m_option.restartRequested();}
    bool updateOption(const OptionInput &input);
    // Title menu item 1 is retail state 3: four pages of twenty recorded runs.
    bool openReplayBrowser();
    bool replayBrowserActive() const {return m_replayActive;}
    int replayPage() const {return m_replayPage;}
    int replayCursor() const {return m_replayCursor;}
    std::filesystem::path consumeReplayPath()
    {
        std::filesystem::path path;
        path.swap(m_replayPath);
        return path;
    }
private:
    static constexpr int kItemCount=6;
    void select(int index);
    void activate();
    void returnFromHelp();
    void status(const wchar_t *message);
    void updateReplayBrowser(const platform::InputState &input);
    void drawReplayBrowser(IDirect3DDevice9 *device);
    format::AnmTexture m_title, m_version;
    HelpScene m_help;
    MusicRoomScene m_musicRoom;
    OptionScene m_option;
    format::GameConfig m_config;
    std::array<format::AnmAnimation,18> m_menuAnimations;
    std::array<format::AnmAnimation,5> m_background;
    format::AnmAnimation m_versionAnimation;
    std::vector<format::AnmAnimation> m_particles;
    platform::Audio m_audio;
    platform::Texture m_status;
    // Retail state 3: the replay browser owns title.anm 104/105 and 31/71 and
    // prints through ascii.anm like the level result pages.
    format::AnmTexture m_ascii;
    std::array<format::AnmAnimation,4> m_replayBackground;
    std::array<format::ReplaySlot,format::kReplaySlots> m_replaySlots;
    std::filesystem::path m_replayPath,m_dataDirectory;
    int m_replayPage=0,m_replayCursor=0,m_replayTicks=0,m_replayRemember=0;
    bool m_replayActive=false,m_asciiReady=false;
    IDirect3DDevice9 *m_device=nullptr;
    int m_menuDelay=0;
    int m_selected=0, m_frames=0, m_repeat=0;
    int m_prepareStage=0;
    double m_statusSeconds=0;
    bool m_quit=false, m_active=false, m_started=false, m_startLevel=false;
    bool m_missionSelect=false,m_inMissionSelect=false,m_restartRequested=false;
    Archive *m_archive=nullptr;
    std::filesystem::path m_bgmPath, m_configPath;
};
} // namespace th95::scene
