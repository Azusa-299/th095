#include "music_room.hpp"
#include <algorithm>
#include <utility>
#include "../log.hpp"
#include "../formats/mission.hpp"

namespace th95::scene {
namespace {
// retail routine sets both animation text bytes to 15; retail routine maps size <18
// to a 30 pixel MS Gothic / SimHei font. The 512x17 strips sit 18 pixels
// apart in title.anm page 13 and text.anm page 0.
constexpr int kCommentRows=8;
constexpr int kTextHeight=15;
constexpr int kFontHeight=30;
constexpr int kRowStep=18;
constexpr COLORREF kTextColor=0xDFDFFF; // literal at 0x4513xx / 0x4515xx
constexpr int kIntroTicks=30;           // state 1 -> state 2
constexpr int kCommentFirst=10, kCommentLast=26;
constexpr int kExitTicks=60;
}

void MusicRoomScene::close()
{
    if(m_title)m_title->bindExternalTexture(13,nullptr);
    m_text.bindExternalTexture(0,nullptr);
    m_trackAtlas=platform::Texture{};
    m_commentAtlas=platform::Texture{};
    m_panels={};m_heading={};m_commentRows={};
    m_rows.clear();m_trackLines.clear();m_commentLines.clear();
    m_title=nullptr;m_archive=nullptr;m_audio=nullptr;m_device=nullptr;
    m_stage=Stage::Closed;m_selected=0;m_ticks=0;m_failed=false;m_playing.clear();
}

bool MusicRoomScene::open(Archive &archive,IDirect3DDevice9 *device,
                          const std::filesystem::path &bgmPath,
                          format::AnmTexture &title,platform::Audio &audio)
{
    close();
    m_archive=&archive;m_device=device;m_title=&title;m_audio=&audio;m_bgmPath=bgmPath;
    // A null device is the headless fixture path: keep the state machine and
    // resource parsing, but do not touch waveOut/DirectSound.
    m_headless=(device==nullptr);
    const Entry *entry=archive.find("musiccmt.txt");
    if(!entry)entry=archive.find("sprt/musiccmt.txt");
    if(!entry){log::print("error : music room missing musiccmt.txt\r\n");return false;}
    const auto data=archive.extract(*entry);
    if(data.size()!=entry->size||!m_comments.parse(data)){
        log::print("error : music room could not parse musiccmt.txt\r\n");return false;}
    if(!m_text.load(archive,"text.anm",device))return false;
    // The archive has no locale field; reuse the same status-string probe as
    // Mission Select so CN text renders with SimHei / CP936.
    format::MissionMessages messages;
    if(const auto *msg=archive.find("mission.msg")) {
        const auto raw=archive.extract(*msg);
        if(messages.parse(raw))m_codePage=messages.codePage();
    }
    // State 0 clears both dynamic pages (0x453B0) before any text is painted.
    if(device) {
        const std::vector<std::string> blank;
        if(!m_trackAtlas.fromGameTextRows(device,blank,kTextColor,m_codePage,kTextHeight,kRowStep,512,kFontHeight)||
           !m_commentAtlas.fromGameTextRows(device,blank,kTextColor,m_codePage,kTextHeight,kRowStep,256,kFontHeight)) {
            log::print("error : music room dynamic text pages unavailable\r\n");close();return false;
        }
        title.bindExternalTexture(13,&m_trackAtlas);
        m_text.bindExternalTexture(0,&m_commentAtlas);
    }
    if(!m_panels[0].start(title,104)||!m_panels[1].start(title,105,512)||
       !m_heading.start(title,23)){close();return false;}
    m_rows.resize(m_comments.size());
    for(std::size_t i=0;i<m_rows.size();++i)
        if(!m_rows[i].start(title,131+int(i))){close();return false;}
    for(int i=0;i<kCommentRows;++i)
        if(!m_commentRows[i].start(m_text,1+i)){close();return false;}
    m_trackLines.assign(m_rows.size(),std::string{});
    m_commentLines.assign(kCommentRows,std::string{});
    m_selected=0;m_ticks=0;m_failed=false;m_stage=Stage::Intro;
    log::print("info : music room mode 8: %zu tracks, title scripts 131..%d, text.anm 1..8, codepage %u\r\n",
               m_comments.size(),130+int(m_rows.size()),m_codePage);
    return true;
}

void MusicRoomScene::paintTrackRow(int row)
{
    if(row<0||row>=int(m_trackLines.size()))return;
    m_trackLines[row]=m_comments.tracks()[std::size_t(row)].title;
    if(!m_device)return;
    platform::Texture fresh;
    if(!fresh.fromGameTextRows(m_device,m_trackLines,kTextColor,m_codePage,
                               kTextHeight,kRowStep,512,kFontHeight)){m_failed=true;return;}
    m_trackAtlas=std::move(fresh);
}

void MusicRoomScene::paintCommentRow(int row)
{
    if(row<0||row>=kCommentRows)return;
    m_commentLines[std::size_t(row)]=m_comments.tracks()[std::size_t(m_selected)].comments[std::size_t(row)];
    if(!m_device)return;
    platform::Texture fresh;
    if(!fresh.fromGameTextRows(m_device,m_commentLines,kTextColor,m_codePage,
                               kTextHeight,kRowStep,256,kFontHeight)){m_failed=true;return;}
    m_commentAtlas=std::move(fresh);
}

void MusicRoomScene::highlight()
{
    for(std::size_t i=0;i<m_rows.size();++i)m_rows[i].interrupt(int(i)==m_selected?2:3);
}

void MusicRoomScene::playSelected()
{
    m_playing=m_comments.tracks()[std::size_t(m_selected)].bgm;
    if(!m_archive||!m_audio||m_headless)return;
    if(!m_audio->load(*m_archive,m_bgmPath,m_playing.c_str()))
        log::print("warning : music room BGM %s unavailable\r\n",m_playing.c_str());
    else m_audio->start();
}

void MusicRoomScene::exitToTitle()
{
    m_audio->playCancel(); // se 11, retail address in the retail teardown branch
    for(auto &panel:m_panels)panel.interrupt(1);
    m_heading.interrupt(1);
    for(auto &row:m_rows)row.interrupt(1);
    for(auto &row:m_commentRows)row.interrupt(1);
    m_stage=Stage::Closing;m_ticks=0;m_playing.clear();
    // Retail reloads the title track on the same path (retail addresses).
    // Audio::load only replaces the music stream; effect voices survive.
    if(m_archive&&m_audio&&!m_headless) {
        if(!m_audio->load(*m_archive,m_bgmPath,"th095_00.wav"))
            log::print("warning : music room could not restore title BGM\r\n");
        else m_audio->start();
    }
}

void MusicRoomScene::animate()
{
    for(auto &panel:m_panels)panel.update();
    m_heading.update();
    for(auto &row:m_rows)row.update();
    for(auto &row:m_commentRows)row.update();
}

bool MusicRoomScene::update(const MusicRoomInput &input)
{
    if(m_stage==Stage::Closed)return false;
    bool returned=false;
    if(m_stage==Stage::Intro) {
        // One list caption every other tick, starting at tick 2.
        if(m_ticks>=2&&(m_ticks&1)==0) {
            const int row=(m_ticks-2)/2;
            if(row<int(m_rows.size())) {
                paintTrackRow(row);
                m_rows[std::size_t(row)].interrupt(row==m_selected?2:3);
            }
        }
        if(m_ticks>=kIntroTicks){m_stage=Stage::List;m_ticks=0;}
    } else if(m_stage==Stage::List) {
        // Eight comment rows between ticks 10 and 25. The retail code reads
        // the current selection per row, so a mid-fade cursor change can mix
        // lines until the next confirm redraws them.
        if(m_ticks>=kCommentFirst&&m_ticks<kCommentLast&&(m_ticks&1)==0) {
            const int row=(m_ticks-kCommentFirst)/2;
            if(row<kCommentRows){paintCommentRow(row);m_commentRows[std::size_t(row)].interrupt(2);}
        }
        const int before=m_selected;
        const int count=std::max(1,int(m_rows.size()));
        // 0x10/0x20 are up/down; both pressed edges and repeat masks move
        // the shared list widget, which inherits wrap from retail address.
        if(input.up||input.upRepeat)m_selected=(m_selected+count-1)%count;
        if(input.down||input.downRepeat)m_selected=(m_selected+1)%count;
        if(before!=m_selected){m_audio->playSelect();highlight();}
        if(input.confirm) {
            // No sound on play: only Z/Enter (0x1002) reloads the BGM and
            // restarts the eight-row fade by resetting the tick counter.
            for(auto &row:m_commentRows)row.interrupt(3);
            m_ticks=0;
            playSelected();
        } else if(input.cancel) {
            exitToTitle();
            returned=true;
        }
    } else if(m_stage==Stage::Closing) {
        if(m_ticks>=kExitTicks)close();
    }
    animate();
    ++m_ticks;
    return returned;
}

void MusicRoomScene::renderLayer(IDirect3DDevice9 *device,int layer) const
{
    for(const auto &panel:m_panels)if(panel.layer()==layer)panel.draw(device);
    if(m_heading.layer()==layer)m_heading.draw(device);
    for(const auto &row:m_rows)if(row.layer()==layer)row.draw(device);
    for(const auto &row:m_commentRows)if(row.layer()==layer)row.draw(device);
}
} // namespace th95::scene
