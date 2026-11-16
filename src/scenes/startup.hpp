#pragma once
#include "title.hpp"
#include "level.hpp"
#include "mission_select.hpp"
#include "fps_meter.hpp"
#include "../formats/anm.hpp"

namespace th95::scene {
class StartupScene {
public:
    enum class Phase { Presentation, Loading, Title, MissionSelect, Level, Failed };
    bool load(Archive &archive,IDirect3DDevice9 *device,const std::filesystem::path &bgmPath,
              bool titleOnly=false,unsigned diagnosticLoadDelay=0,
              const std::filesystem::path &scorePath={});
    void update(double deltaSeconds, const platform::InputState &input = {});
    void render(IDirect3DDevice9 *device);
    void presented();
    Phase phase() const {return m_phase;}
    unsigned loadingFrames() const {return m_loadingFrames;}
    unsigned titleFrames() const {return m_titleFrames;}
    bool quitRequested() const {return m_phase==Phase::Title&&m_title.quitRequested();}
    bool audioHasPlaybackProgress() const {return m_title.audioHasPlaybackProgress();}
    // retail routine calls retail routine on the way out, so a best shot taken in a failed
    // attempt or a locked record survives closing the window.
    void saveOnExit();
    // Fixture hook for --help-test; only valid once the title is running.
    TitleScene &title() {return m_title;}
    const MissionSelectScene &missionSelect() const {return m_missionSelect;}
    void recordFrame(double seconds){if(m_phase==Phase::Level)m_level.recordFrame(seconds);m_fps.addFrame(seconds);}
    // Value the overlay currently shows (0x424720 refreshes it twice a second).
    double fpsRate() const {return m_fps.rate();}
private:
    bool beginLoading();
    void drawFps(IDirect3DDevice9 *device);
    Archive *m_archive=nullptr;
    IDirect3DDevice9 *m_device=nullptr;
    std::filesystem::path m_bgmPath,m_scorePath;
    platform::Texture m_logo;
    format::AnmTexture m_loading;
    std::array<format::AnmAnimation,3> m_loadingAnimations;
    TitleScene m_title;
    LevelScene m_level;
    MissionSelectScene m_missionSelect;
    format::ScoreProfile m_profile;
    Phase m_phase=Phase::Presentation;
    unsigned m_loadingFrames=0,m_titleFrames=0,m_diagnosticLoadDelay=0;
    bool m_loadingPresented=false;
    // The counter itself lives in FpsMeter (0x424720); this is the ascii.anm
    // atlas 0x423790 prints with.
    format::AnmTexture m_ascii;
    FpsMeter m_fps;
};
} // namespace th95::scene
