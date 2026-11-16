#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>
#include "mission_data.hpp"
#include "../archive.hpp"
#include "../formats/anm.hpp"
#include "../formats/bestshot.hpp"
#include "../formats/mission.hpp"
#include "../formats/score.hpp"
#include "../platform/audio.hpp"
#include "../platform/input.hpp"

namespace th95::scene {
class MissionSelectScene {
public:
    enum class Result { Running, ReturnToTitle, StartMission, Failed };
    bool load(Archive &archive,IDirect3DDevice9 *device,platform::Audio *audio=nullptr,
              format::ScoreProfile *profile=nullptr);
    // Re-reads the shared profile after a mission commits its record.
    void refreshProfile();
    // bestshot/ sits next to the score file; without it the page keeps the
    // retail black placeholder.
    void setAssetDirectory(const std::filesystem::path &directory) {m_assetDirectory=directory;}
    Result update(const platform::InputState &input);
    void render(IDirect3DDevice9 *device) const;
    void renderLayer(IDirect3DDevice9 *device,int layer) const;
    int selected() const {return m_group*10+m_indices[m_group];}
    int group() const {return m_group;}
    int missionIndex() const {return m_indices[m_group];}
    int visibleGroups() const;
    bool groupUnlocked() const {return groupUnlocked(m_group);}
    bool groupUnlocked(int group) const;
    int captionId() const {return groupUnlocked()?definition().caption:-(m_group+1);}
    bool loaded() const {return m_loaded;}
    bool closing() const {return m_closing;}
    // After the state-5 switch the page keeps drawing its fading animations
    // (logo/rows/104/105 backgrounds) inside the playfield for 60 ticks.
    bool overlayActive() const {return m_loaded&&m_closing&&m_result==Result::StartMission;}
    void renderOverlay(IDirect3DDevice9 *device) const;
    void resume() {m_result=Result::Running;m_ticks=0;}
    // Follows the success page's "next" row: the list must come back on the
    // scene that was started, not on the one that was just cleared.
    void selectMission(int sceneId) {
        const int group=sceneId/10;
        if(group<0||group>=int(kMissionCounts.size()))return;
        m_group=group;
        m_indices[group]=std::clamp(sceneId%10,0,kMissionCounts[group]-1);
    }
    const format::AnmAnimation &animation(int id) const {return m_ui.at(id);}
    const format::MissionMessages &messages() const {return m_messages;}
    const format::AnmAnimation &comment(int row) const {return m_comments.at(row);}
    // Formatted summary rows from retail routine: 0 total, 1 cleared scenes,
    // 2 high score, 3 slow rate.
    const char *statText(int row) const {
        return row==0?m_totalText.data():row==1?m_successText.data():
               row==2?m_highText.data():row==3?m_slowText.data():m_rateText.data();
    }
    bool showRate() const {return m_showRate;}
    bool recordLocked() const {return (m_profile.field(selected(),80)&2)!=0;}
    // Temporary testing cheat: entering the Konami code on this page toggles it.
    bool cheatInvulnerable() const {return m_cheatInvulnerable;}
private:
    const MissionDefinition &definition() const;
    bool start(int script);
    void highlight();
    bool changeGroup(int direction);
    bool refreshDetails();
    void refreshNumbers();
    bool refreshPhoto();
    void toggleLock();
    // retail routine: the mission page summary block (total evaluation, cleared
    // scene count, selected high score, slow rate).
    void refreshStats();
    // Temporary testing cheat: returns true when the B/A step was swallowed.
    bool feedCheatCode(const platform::InputState &input);
    void ascii(IDirect3DDevice9 *device,const char *text,float x,float y,DWORD tint,float scale=1.0f) const;
    void colorBackground(bool initial);
    void animate();
    int cleared(int group) const;
    Archive *m_archive=nullptr;
    IDirect3DDevice9 *m_device=nullptr;
    platform::Audio *m_audio=nullptr;
    format::AnmTexture m_title,m_text,m_caption,m_face,m_faceText,m_blankPhoto,m_ascii;
    format::MissionMessages m_messages;
    format::ScoreProfile m_profile;
    // retail routine toggles the lock bit on the shared profile; retail keeps it in
    // memory until it writes the save on exit.
    format::ScoreProfile *m_profileSource=nullptr;
    format::Bestshot m_bestshot;
    platform::Texture m_commentAtlas,m_photoPage;
    std::filesystem::path m_assetDirectory;
    std::array<char,32> m_totalText{},m_successText{},m_highText{},m_slowText{},m_rateText{};
    std::array<std::string,3> m_displayLines{},m_pendingLines{};
    std::array<format::AnmAnimation,106> m_ui;
    std::array<format::AnmAnimation,3> m_comments;
    std::vector<format::AnmAnimation> m_outgoing;
    std::array<int,12> m_indices{};
    int m_group=0,m_ticks=0,m_commentTicks=-1,m_faceId=-1,m_photoMission=-1;
    COLORREF m_commentColor=0xDF8F8F;
    bool m_loaded=false,m_closing=false,m_profileMounted=false,m_showRate=false;
    // Temporary testing cheat state (Konami code typed on this page).
    int m_cheatProgress=-1;
    bool m_cheatInvulnerable=false;
    Result m_result=Result::Failed;
};
} // namespace th95::scene
