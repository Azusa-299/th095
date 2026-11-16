#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "../archive.hpp"
#include "../formats/anm.hpp"
#include "../formats/music_cmt.hpp"
#include "../platform/audio.hpp"
#include "../platform/gfx.hpp"

namespace th95::scene {
struct MusicRoomInput {
    bool up=false, down=false;
    bool upRepeat=false, downRepeat=false;
    bool confirm=false, cancel=false;
};
// Title mode 8, retail retail routine. It shares the title animation manager and
// the common layers, exactly like Help (mode 9) and Mission Select (mode 2).
class MusicRoomScene {
public:
    enum class Stage { Closed, Intro, List, Closing };
    bool open(Archive &archive,IDirect3DDevice9 *device,
              const std::filesystem::path &bgmPath,
              format::AnmTexture &title,platform::Audio &audio);
    // Returns true once, on the tick that restores title mode 1.
    bool update(const MusicRoomInput &input={});
    void renderLayer(IDirect3DDevice9 *device,int layer) const;
    void close();
    bool active() const {return m_stage!=Stage::Closed;}
    Stage stage() const {return m_stage;}
    int selected() const {return m_selected;}
    int ticks() const {return m_ticks;}
    int trackCount() const {return int(m_rows.size());}
    const std::string &playing() const {return m_playing;}
    const std::string &track(int index) const {
        return m_comments.tracks()[std::size_t(index)].bgm;
    }
    const format::AnmAnimation &trackRow(int index) const {return m_rows.at(std::size_t(index));}
    const format::AnmAnimation &commentRow(int index) const {return m_commentRows.at(std::size_t(index));}
    bool failed() const {return m_failed;}
private:
    void paintTrackRow(int row);
    void paintCommentRow(int row);
    void highlight();
    void playSelected();
    void exitToTitle();
    void animate();
    Archive *m_archive=nullptr;
    IDirect3DDevice9 *m_device=nullptr;
    format::AnmTexture *m_title=nullptr;
    platform::Audio *m_audio=nullptr;
    std::filesystem::path m_bgmPath;
    format::AnmTexture m_text;
    format::MusicComments m_comments;
    platform::Texture m_trackAtlas,m_commentAtlas;
    std::array<format::AnmAnimation,2> m_panels{};
    format::AnmAnimation m_heading;
    std::array<format::AnmAnimation,8> m_commentRows{};
    std::vector<format::AnmAnimation> m_rows;
    std::vector<std::string> m_trackLines,m_commentLines;
    std::string m_playing;
    unsigned m_codePage=932;
    Stage m_stage=Stage::Closed;
    int m_selected=0,m_ticks=0;
    bool m_failed=false,m_headless=false;
};
} // namespace th95::scene
