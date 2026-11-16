#include "startup.hpp"
#include "scene_table.hpp"
#include <algorithm>
#include <cstdio>
#include "../log.hpp"

namespace th95::scene {
namespace {
// retail "next" walks the flat 48-byte scene table, so a group boundary simply
// continues with the first scene of the following level.
int nextMissionId(int mission) {
    const auto *entry=sceneEntry(mission);
    if(!entry)return -1;
    if(entry->scene+1<sceneCount(entry->level))return entry->level*10+entry->scene+1;
    for(int level=entry->level+1;level<kSceneLevels;++level)
        if(sceneCount(level)>0)return level*10;
    return -1;
}
} // namespace
bool StartupScene::load(Archive &archive,IDirect3DDevice9 *device,const std::filesystem::path &bgmPath,
                        bool titleOnly,unsigned diagnosticLoadDelay,const std::filesystem::path &requestedScorePath)
{
    m_archive=&archive;m_device=device;m_bgmPath=bgmPath;
    // The retail debug printer owns ascii.anm for the whole run; the fps
    // overlay is the only consumer here, so failing to mount it costs nothing
    // but the counter instead of the scene.
    if(!m_ascii.load(archive,"ascii.anm",device))
        log::print("warning : ascii.anm unavailable, fps overlay disabled\r\n");
    const auto scorePath=requestedScorePath.empty()?bgmPath.parent_path()/L"scoreth095.dat":requestedScorePath;
    m_scorePath=scorePath;
    if(m_profile.load(scorePath))log::print("info : scoreth095.dat loaded read-only\r\n");
    else log::print("info : no valid score profile; using empty in-memory records\r\n");
    m_diagnosticLoadDelay=diagnosticLoadDelay;
    if(titleOnly) {
        m_phase=Phase::Title;
        return m_title.load(archive,device,bgmPath);
    }
    const auto *entry=archive.find("th08logo.jpg");
    if(!entry){log::print("error : missing startup th08logo.jpg\r\n");return false;}
    const auto jpeg=archive.extract(*entry);
    if(jpeg.size()!=entry->size||!m_logo.fromMemory(device,jpeg.data(),jpeg.size())||
       m_logo.width()!=640||m_logo.height()!=480||!m_loading.load(archive,"nowloading.anm",device)) {
        log::print("error : startup resources could not be loaded\r\n");return false;
    }
    log::print("info : startup presentation: original th08logo.jpg; no calibration or minimum dwell\r\n");
    if(m_diagnosticLoadDelay)log::print("info : startup-test only: simulating %u ticks of slow resource IO\r\n",m_diagnosticLoadDelay);
    return true;
}
bool StartupScene::beginLoading()
{
    for(int i=0;i<3;++i)
        if(!m_loadingAnimations[i].start(m_loading,i,500,440))return false;
    m_phase=Phase::Loading;
    log::print("info : startup loading: nowloading scripts 0/1/2 at (500,440); BGM silent\r\n");
    return true;
}
void StartupScene::presented()
{
    if(m_phase==Phase::Presentation) {
        // Show the startup image once, then start preparing resources. The
        // legacy D3D8 cadence benchmark is not a minimum animation duration.
        if(!beginLoading())m_phase=Phase::Failed;
    } else if(m_phase==Phase::Loading) {
        m_loadingPresented=true;
    }
}
void StartupScene::update(double deltaSeconds, const platform::InputState &input)
{
    if(m_phase==Phase::Title) {
        if(m_missionSelect.closing())m_missionSelect.update({});
        m_title.update(deltaSeconds,input);++m_titleFrames;
        const auto replay=m_title.consumeReplayPath();
        if(!replay.empty()) {
            // The recorded header supplies the level/scene/seed; the file itself
            // is read by the level when it resets.
            if(m_level.load(*m_archive,m_device,0,&m_title.audio(),&m_profile,replay,
                            m_bgmPath.parent_path(),&m_title.config())) {
                const char *bgm=m_level.bgmName();
                if(bgm&&*bgm&&m_title.audio().load(*m_archive,m_bgmPath,bgm))m_title.audio().start();
                m_phase=Phase::Level;
                log::print("info : title replay -> %s\r\n",replay.filename().string().c_str());
            } else {
                m_phase=Phase::Failed;
            }
            return;
        }
        if(m_title.consumeMissionSelect()) {
            m_missionSelect.setAssetDirectory(m_bgmPath.parent_path());
            if(m_missionSelect.load(*m_archive,m_device,&m_title.audio(),&m_profile)) {
                m_title.enterMissionSelect();
                m_phase = Phase::MissionSelect;
                log::print("info : title -> Mission Select\r\n");
            } else {
                m_phase = Phase::Failed;
            }
        }
    } else if(m_phase==Phase::MissionSelect) {
        m_title.update(deltaSeconds,{});
        const auto result = m_missionSelect.update(input);
        if(result==MissionSelectScene::Result::ReturnToTitle) {
            m_title.leaveMissionSelect();
            m_phase = Phase::Title;
            log::print("info : Mission Select -> title\r\n");
        } else if(result==MissionSelectScene::Result::StartMission) {
            if(m_level.load(*m_archive,m_device,m_missionSelect.selected(),&m_title.audio(),&m_profile)) {
                // Temporary testing cheat: the mission-select Konami code arms
                // invulnerability for the mission it starts.
                m_level.simulation().invulnerable=m_missionSelect.cheatInvulnerable();
                // Each scene-table entry carries the retail BGM name, so the
                // later mission groups no longer depend on a hard-coded track.
                const auto *entry=sceneEntry(m_missionSelect.selected());
                if(entry&&m_title.audio().load(*m_archive,m_bgmPath,entry->bgm))m_title.audio().start();
                m_phase = Phase::Level;
                log::print("info : Mission Select -> mission=%d\r\n", m_missionSelect.selected());
            } else {
                m_phase = Phase::Failed;
            }
        } else if(result==MissionSelectScene::Result::Failed)m_phase=Phase::Failed;
    } else if(m_phase==Phase::Level) {
        // Keep the title-owned music stream serviced while the level owns the
        // foreground renderer.
        m_title.update(deltaSeconds, {});
        // The mission page's 104/105 backgrounds outlive the state-5 switch for
        // 60 ticks; keep advancing their fade-out as an overlay.
        if(m_missionSelect.overlayActive())m_missionSelect.update({});
        const auto result = m_level.update(input);
        if(result==LevelScene::Result::ReturnToMissionSelect) {
            // retail routine writes the container when the result menu leaves the
            // level; retail routine flushes the bestshot staged by retail routine first.
            if(!m_scorePath.empty()) {
                m_profile.setSavedSelection(m_missionSelect.group(),m_missionSelect.missionIndex());
                if(!m_profile.save(m_scorePath))log::print("error : scoreth095.dat write failed\r\n");
                else log::print("info : scoreth095.dat written\r\n");
            }
            if(m_title.audio().load(*m_archive,m_bgmPath))m_title.audio().start();
            // Starting the mission released the select page (retail disposes the
            // scene at the state-5 switch), so the result menu cannot hand the
            // old instance back: rebuild it before restoring control.
            if(!m_missionSelect.load(*m_archive,m_device,&m_title.audio(),&m_profile)){m_phase=Phase::Failed;return;}
            m_missionSelect.resume();
            // The committed record has to reach the list before it redraws.
            m_missionSelect.refreshProfile();
            m_phase=Phase::MissionSelect;
        } else if(result==LevelScene::Result::StartNextMission) {
            const int current=m_level.mission();
            const int next=nextMissionId(current);
            if(next<0) {
                // No further scene exists: fall back to the result menu's Select row.
                if(!m_scorePath.empty()) {
                    m_profile.setSavedSelection(m_missionSelect.group(),m_missionSelect.missionIndex());
                    if(!m_profile.save(m_scorePath))log::print("error : scoreth095.dat write failed\r\n");
                }
                if(!m_missionSelect.load(*m_archive,m_device,&m_title.audio(),&m_profile)){m_phase=Phase::Failed;return;}
                m_missionSelect.resume();
                m_missionSelect.refreshProfile();
                m_phase=Phase::MissionSelect;
            } else {
                if(!m_scorePath.empty()) {
                    m_profile.setSavedSelection(next/10,next%10);
                    if(!m_profile.save(m_scorePath))log::print("error : scoreth095.dat write failed\r\n");
                    else log::print("info : scoreth095.dat written\r\n");
                }
                if(!m_level.load(*m_archive,m_device,next,&m_title.audio(),&m_profile)){m_phase=Phase::Failed;return;}
                m_level.simulation().invulnerable=m_missionSelect.cheatInvulnerable();
                const auto *entry=sceneEntry(next);
                if(entry&&m_title.audio().load(*m_archive,m_bgmPath,entry->bgm))m_title.audio().start();
                // The list must resume on the scene that was started, not the cleared one.
                m_missionSelect.selectMission(next);
                m_phase=Phase::Level;
                log::print("info : mission %d -> next mission %d\r\n",current,next);
            }
        } else if(result==LevelScene::Result::Failed)m_phase=Phase::Failed;
        else if(result==LevelScene::Result::ReturnToTitle) {
            m_title.leaveMissionSelect();
            // A replay replaces the title track with the scene's cue; restore it.
            if(m_title.audio().load(*m_archive,m_bgmPath))m_title.audio().start();
            m_phase = Phase::Title;
            log::print("info : LV1 -> title\r\n");
        }
    }
    if(m_phase==Phase::Loading||m_phase==Phase::Title)
        for(auto &a:m_loadingAnimations)a.update();
    if(m_phase!=Phase::Loading||!m_loadingPresented)return;
    ++m_loadingFrames;
    // The optional delay only belongs to --startup-test. It simulates slow
    // storage to exercise a full loading pulse; ordinary startup has no delay.
    if(m_loadingFrames<=m_diagnosticLoadDelay)return;
    const auto result=m_title.prepareNext(*m_archive,m_device,m_bgmPath);
    // Finish at most one resource batch per presentation even if a slow read
    // made the main loop accumulate several logical updates.
    m_loadingPresented=false;
    if(result==TitleScene::Preparation::Failed) {
        log::print("error : startup title preparation failed\r\n");m_phase=Phase::Failed;return;
    }
    if(result==TitleScene::Preparation::Ready) {
        if(!m_title.start()){m_phase=Phase::Failed;return;}
        // retail routine sends interrupt 1. The JPEG is removed immediately;
        // only the loading overlay fades/slides out over the new title.
        for(auto &a:m_loadingAnimations)a.interrupt(1);
        m_logo=platform::Texture{};
        m_phase=Phase::Title;
        log::print("info : startup -> title: splash released, loading interrupt 1, title BGM starts\r\n");
    }
}
void StartupScene::saveOnExit()
{
    // retail routine runs retail routine before releasing the game object. The status block
    // must carry whatever the mission page currently shows, exactly like the
    // live struct retail routine serializes.
    if(m_scorePath.empty()||!m_profile.hasData())return;
    if(m_phase==Phase::MissionSelect)
        m_profile.setSavedSelection(m_missionSelect.group(),m_missionSelect.missionIndex());
    if(!m_profile.save(m_scorePath))log::print("error : scoreth095.dat write failed on exit\r\n");
    else log::print("info : scoreth095.dat written on exit\r\n");
}
void StartupScene::drawFps(IDirect3DDevice9 *device)
{
    if(!device)return;
    const double fps=m_fps.rate();
    const DWORD tint=FpsMeter::tint(fps);
    char text[16];
    std::snprintf(text,sizeof(text),"%2.1ffps",fps);
    // The 3D scenes toggle scissoring; the retail printer draws straight into
    // the backbuffer after everything else, so clear the state here too.
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    float x=288.0f;
    for(const char *p=text;*p;++p,x+=9.0f)m_ascii.draw(device,unsigned(*p)-32,x,0.0f,tint);
}
void StartupScene::render(IDirect3DDevice9 *device)
{
    if(m_phase==Phase::Title||m_phase==Phase::MissionSelect) {
        for(int layer=0;layer<=15;++layer) {
            m_title.renderLayer(device,layer);
            if(m_phase==Phase::MissionSelect||m_missionSelect.closing())m_missionSelect.renderLayer(device,layer);
        }
    }
    else if(m_phase==Phase::Level){
        m_level.render(device);
        m_missionSelect.renderOverlay(device);
    }
    else if(m_logo.get())platform::drawQuad(device,m_logo.get(),0,0,640,480);
    if(m_phase==Phase::Loading||m_phase==Phase::Title)
        for(const auto &a:m_loadingAnimations)a.draw(device);
    drawFps(device);
}
} // namespace th95::scene
