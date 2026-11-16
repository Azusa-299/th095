#include "level.hpp"
#include "level_internal.hpp"
#include "scene_table.hpp"
#include "../log.hpp"
#include "../formats/mission.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
#include <sstream>

namespace th95::scene {
bool LevelScene::load(Archive&a,IDirect3DDevice9*d,int mission,platform::Audio*audio,
                      format::ScoreProfile*profile,const std::filesystem::path&replayPath,
                      const std::filesystem::path&dataDirectory,const format::GameConfig*config){
    m_loaded=false;m_result=Result::Failed;m_audio=audio;
    m_profile=profile;m_profileAttempts=0;m_profileCommitted=false;
    m_device=d;m_gameConfig=config;
    m_dataDirectory=dataDirectory;
    m_replayMode=ReplayMode::None;m_replay=format::Replay{};m_replayTick=0;m_replayHeld=0;
    m_failureComments.clear();m_failureIndex=0;

    // A replay supplies the level and scene pair; retail indexes the scene
    // table with those two bytes rather than trusting the echoed global id.
    if(!replayPath.empty()){
        format::Replay replay;
        if(!format::readReplay(replayPath,replay))return false;
        const SceneEntry*entry=sceneEntryAt(replay.header.level,replay.header.scene);
        if(!entry)return false;
        m_replay=std::move(replay);
        mission=entry->id;
    }
    const SceneEntry*entry=sceneEntry(mission);
    if(!entry)return false;
    m_mission=mission;
    m_sceneBgm=entry->bgm;
    // reset() restarts the recording; playback must keep the loaded stream.
    m_replayMode=replayPath.empty()?ReplayMode::Record:ReplayMode::Play;
    m_profileHigh=profile&&profile->present(mission)?int(profile->field(mission,16)):0;
    m_profileTotal=0;if(profile)for(int i=0;i<120;++i)m_profileTotal+=profile->field(i,16);
    format::MissionMessages messages;if(messages.parse(extract(a,"mission.msg")))m_codePage=messages.codePage();
    const auto help=extract(a,"help.txt");std::istringstream lines(std::string(help.begin(),help.end()));
    std::string line;bool group=false;int row=0;std::array<std::string,3> comment{};
    while(std::getline(lines,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        // Some translated resources use CR CR LF.
        while(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()||line[0]=='#')continue;
        if(line=="end")break;
        if(line.rfind("level:",0)==0){group=line.substr(6)=="0";row=0;comment={};continue;}
        if(group&&row<2){
            for(std::size_t i=0;(i=line.find("%%",i))!=std::string::npos;++i)line.erase(i,1);
            comment[row++]=line;if(row==2)m_failureComments.push_back(comment);
        }
    }
    if(!m_sim.load(extract(a,entry->ecl),extract(a,"player.sht"),mission)||
        !m_enemy.load(a,entry->enemy,d)||!m_player.load(a,"player.anm",d)||
        !m_front.load(a,"front.anm",d)||!m_bullet.load(a,"bullet.anm",d)||!m_ascii.load(a,"ascii.anm",d)||
        !m_photo.load(a,"photo.anm",d)||!m_pause.load(a,"pause.anm",d)||!m_text.load(a,"text.anm",d)||
        !m_spellText.load(a,"text.anm",d)||!m_world.load(a,d,entry->world))return false;
    // 0x433140 pays 20/40/150 points from the height of the bullet's current
    // bullet.anm sprite. The shutter never advances a bullet's animation within
    // the frame, so the two reachable heights are the script's first sprite and
    // the sprite at the appearance interrupt (opcode 64 label 2).
    {
        std::map<int,LevelSimulation::BulletTierHeights> tiers;
        for(int id:m_bullet.scriptIds()){
            const auto*instructions=m_bullet.script(id);
            if(!instructions)continue;
            LevelSimulation::BulletTierHeights heights;
            bool used=false;
            int pending=-1;
            for(const auto&ins:*instructions){
                if(ins.opcode==3&&ins.args.size()>=4){
                    const auto*sprite=m_bullet.sprite(ins.integer(0));
                    if(!sprite)continue;
                    // A normal bullet starts on the op-64 label 2 jump target; the
                    // appearance interrupts 7/8/9 pick the opening sprites.
                    if(pending==2){heights.normal=sprite->h;used=true;pending=-1;}
                    else if(pending==7||pending==8||pending==9){
                        heights.appearing=sprite->h;used=true;pending=-1;
                    }
                }else if(ins.opcode==64&&ins.args.size()>=4){
                    pending=ins.integer(0);
                }
            }
            if(used)tiers.emplace(id,heights);
        }
        m_sim.setBulletTierHeights(std::move(tiers));
    }
    reset();
    if(m_replayMode==ReplayMode::Play){
        // Retail mode 1 copies the recorded seed into the global RNG and
        // leaves recording disabled; the input stream drives every tick.
        m_sim.setSeed(m_replay.header.seed);
        m_replayTick=0;m_replayHeld=0;
    }
    m_loaded=true;m_result=Result::Running;
    log::print("info : LV%d scene %d loaded ECL %s; field=(128,16,384,448)\r\n",
               entry->level+1,entry->scene+1,entry->ecl);return true;
}
void LevelScene::reset(){
    if(m_audio){m_audio->stopEffect(42);m_audio->stopEffect(44);}
    m_hitpoint={};m_deathFlash={};m_deathParticles.clear();m_effects.clear();m_entryEffects.clear();m_leafMarks.clear();m_leafFades.clear();m_markCreated=0;m_markPending=0;m_markDelay=0;m_leafFlashes.clear();m_leafFlashSeen=0;m_leafFlashDelay=0;m_spawnCircles.clear();m_seenCollisionEvent=0;m_wasFocused=false;m_resumeEffects=false;
    m_bestPhoto={};m_bestSlot=-1;m_successPhotos=0;m_successBrowse=false;m_menuCapturePending=false;
    m_failureText={};m_slowFrames=0;m_presentedFrames=0;
    m_pause.bindExternalTexture(0,&m_menuFrame);
    m_text.bindExternalTexture(0,&m_failureAtlas);
    // text.anm page 0 stays unbound until CardInf-like text is painted.
    m_spellText.bindExternalTexture(0,&m_spellAtlas);
    m_spellName={};m_spellBrush={};m_spellAtlas={};m_spellSeenEvent=0;
    m_sessionHighScore=std::max(m_sessionHighScore,m_sim.highScore);
    m_sim.restart();m_sim.highScore=m_sessionHighScore;m_world.restart();m_enemyVisuals.clear();m_bulletVisuals.clear();m_playerScript=-1;m_timerColor=0;m_menuOpen=false;m_menuKind=MenuKind::None;m_menuSelection=0;m_menuTicks=0;m_menuCount=0;m_menuOptionBase=0;m_menuOptionCount=0;m_oldCameraState=0;m_photoResult.clear();m_playbackOutcome=false;m_menuAction=MenuAction::None;m_menuExitTicks=0;m_bestOverwriteTicks=0;
    // A retail retry reconstructs the level object, so the replay-name edit
    // state (buffer, length, cursor) starts fresh rather than leaking from the
    // previous attempt.
    m_replayName.fill(' ');m_replayNameLength=0;m_replayNameCursor=0;m_replaySelection=0;
    m_photographs={};m_resultDigits={};m_cameraStatus={};m_shotNotice={};m_capturePending=false;m_worldFrameReady=false;m_wasBossInFrame=false;m_resultAge=0;m_photoReadbackSlot=-1;
    for(int i=0;i<11;++i)m_photo.bindExternalTexture(i,&m_photographs[i].texture);
    m_aim={};m_aimOutside={};m_cameraChargeBranch=0;
    for(int i=0;i<4;++i){m_borders[i].start(m_front,i);m_percent[i].start(m_photo,36);}
    for(int i=0;i<6;++i)m_timer[i].start(m_front,4+i);
    for(int i=0;i<2;++i)m_camera[i].start(m_photo,22+i);
    m_intro.start(m_front,13);syncVisuals();
    if(m_replayMode==ReplayMode::Record)beginReplayRecording();
}
}
