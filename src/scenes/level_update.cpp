#include "level.hpp"
#include "level_internal.hpp"
#include "../log.hpp"
#include "../formats/mission.hpp"
#include "../numeric/numeric.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
#include <sstream>

namespace th95::scene {
void LevelScene::playMenuSound(int id){
    // Every gameplay menu sample comes from the same 47-entry retail table;
    // retail address merges repeats inside one tick, so one dispatch per action.
    m_menuSound=id;if(m_audio)m_audio->playEffect(id);
}
void LevelScene::openMenu(bool failed){
    if(failed){openOutcome(false);return;}
    if(m_audio){m_audio->stopEffect(42);m_audio->stopEffect(44);}
    playMenuSound(32);   // retail routine requests se 32 as the pause page opens.
    m_resumeEffects=true;
    m_menuCapturePending=true;
    m_menuOpen=true;m_menuKind=MenuKind::Pause;m_menuSelection=0;m_menuTicks=0;m_menuCount=5;m_menuOptionBase=2;m_menuOptionCount=3;m_sim.paused=true;
    m_menuAction=MenuAction::None;m_menuExitTicks=0;
    // Live pause (retail sub_428590 slots 4/5/6): 4 = resume, 5 = retry,
    // 6 = give up. Replay playback takes the other branch of that routine
    // (slots 16/17/18): 16 = resume, 17 = replay, 18 = end.
    const int liveScripts[]={0,3,4,5,6};
    const int playbackScripts[]={0,3,16,17,18};
    const int *scripts=m_replayMode==ReplayMode::Play?playbackScripts:liveScripts;
    for(int i=0;i<m_menuCount;++i){
        m_menu[i].start(m_pause,scripts[i]);
        // Retail creates the page and advances every slot once at the end of
        // the same frame. The selected/unselected labels (2/3) are applied by
        // sub_429AB0 ten ticks later, not at creation.
        m_menu[i].update();
    }
}
void LevelScene::commitRecord(){
    // retail routine (success page open): stamp the record header, set the cleared bit
    // and keep the best accumulated score plus the slow rate. The best shot
    // itself was already staged per photograph by stageShotRecord (retail routine).
    // Playback never writes the profile: only a live run can clear a mission.
    if(m_replayMode!=ReplayMode::Record)return;
    if(!m_profile||m_profileCommitted)return;
    m_profileCommitted=true;
    const int mission=m_mission;
    // retail routine creates the record before touching any of its fields.
    m_profile->createRecord(mission);
    const uint32_t flags=m_profile->field(mission,80)|1u;
    m_profile->setField(mission,80,flags);
    const double slow=m_presentedFrames?100*m_slowFrames/(m_presentedFrames+m_slowFrames):0;
    float rate=float(slow);
    uint32_t rateBits=0;std::memcpy(&rateBits,&rate,sizeof(rateBits));
    const int sceneScore=std::max(0,m_sim.score);
    if(uint32_t(sceneScore)>m_profile->field(mission,16)) {
        m_profile->setField(mission,16,uint32_t(sceneScore));
        m_profile->setField(mission,72,rateBits);
    }
}
void LevelScene::openOutcome(bool success){
    if(m_audio){m_audio->stopEffect(42);m_audio->stopEffect(44);}
    m_resumeEffects=false;m_menuOpen=true;m_menuKind=success?MenuKind::Success:MenuKind::Failure;
    m_menuTicks=0;m_sim.paused=true;m_menuCapturePending=true;
    m_menuAction=MenuAction::None;m_menuExitTicks=0;
    if(m_replayMode==ReplayMode::Play){
        // Retail sub_4288B0/sub_428E90 branch on dword_4BDEC8+288: replay
        // playback never opens the live pages (states 3/5). It opens states 7/9
        // with the same full-screen panel and success/failure title as the live
        // pages (slot 1 + script 1/7 or slot 2 + script 2/11) plus pause.anm
        // slots 19/20 ("再生" / "終了"); the best-shot and comment work is
        // skipped entirely.
        m_playbackOutcome=true;m_menuSelection=1;
        m_menuCount=4;m_menuOptionBase=2;m_menuOptionCount=2;
        const int scripts[]={success?2:1,success?11:7,19,20};
        for(int i=0;i<m_menuCount;++i){
            m_menu[i].start(m_pause,scripts[i]);
            m_menu[i].update();
        }
        return;
    }
    m_playbackOutcome=false;
    m_menuSelection=success?2:0;
    if(success){
        commitRecord();
        // retail routine scans all eleven slots (not the captured flag, which the
        // renderer only sets after this tick) and keeps the highest score.
        for(int i=0;i<11;++i)
            if(m_bestSlot<0||m_photographs[i].score>m_photographs[m_bestSlot].score)m_bestSlot=i;
        // retail routine enables retail routine's cursor only while the initial slot is inside
        // the strip; a rejected slot 10 can be shown but not browsed away.
        m_successPhotos=std::clamp(m_sim.photos,0,10);
        m_successBrowse=m_bestSlot>=0&&m_bestSlot<m_successPhotos;
        if(m_bestSlot>=0){m_bestPhoto.start(m_photo,2*m_bestSlot+1);m_bestPhoto.update();}
        m_menuCount=6;m_menuOptionBase=2;m_menuOptionCount=4;
        const int scripts[]={2,11,12,13,14,15};
        for(int i=0;i<m_menuCount;++i){
            m_menu[i].start(m_pause,scripts[i]);
            m_menu[i].update();
        }
    }else{
        if(!m_failureComments.empty()){
            // 0x4288B0 picks the comment with the per-level index at profile
            // +34+level, then increments and wraps it (the count comes from
            // the level's comment table). Keeping it in the score profile makes
            // the rotation persist across sessions like the retail build.
            std::size_t index=0;
            if(m_profile){
                const int level=std::clamp(m_mission/10,0,11);
                index=std::size_t(m_profile->failureCommentIndex(level));
                if(index>=m_failureComments.size())index%=m_failureComments.size();
                m_profile->setFailureCommentIndex(
                    level,uint8_t((index+1)%m_failureComments.size()));
            }else index=std::size_t(m_failureIndex++)%m_failureComments.size();
            const auto&comment=m_failureComments[index];
            if(m_device)m_failureAtlas.fromGameTextAtlas(m_device,comment,RGB(192,224,255),m_codePage,18);
            for(int i=0;i<2;++i)m_failureText[i].start(m_text,9+i);
        }
        m_menuCount=5;m_menuOptionBase=2;m_menuOptionCount=3;
        const int scripts[]={1,7,8,9,10};
        for(int i=0;i<m_menuCount;++i){
            m_menu[i].start(m_pause,scripts[i]);
            m_menu[i].update();
        }
    }
}
void LevelScene::beginMenuExit(MenuAction action,int fadeFrom){
    // Retail writes label 1 into the rows it wants to retire before the state
    // waits eight ticks. Success/failure keep the full-screen panel (slot 0/1
    // or 0/2) while the title and option rows fade; the pause page fades all
    // five of its slots.
    m_menuAction=action;m_menuExitTicks=0;
    for(int i=std::max(0,fadeFrom);i<m_menuCount;++i)m_menu[i].interrupt(1);
    if(m_menuKind==MenuKind::Success&&!m_playbackOutcome)m_bestPhoto.interrupt(1);
}
void LevelScene::applyMenuAction(){
    const MenuAction action=m_menuAction;
    m_menuAction=MenuAction::None;m_menuExitTicks=0;
    switch(action){
    case MenuAction::None:break;
    case MenuAction::Resume:m_menuOpen=false;m_menuKind=MenuKind::None;m_sim.paused=false;break;
    case MenuAction::Restart:
        if(m_replayMode==ReplayMode::Play)restartReplayPlayback();else reset();
        break;
    case MenuAction::Leave:m_result=leaveResult();break;
    case MenuAction::NextScene:
        m_result=m_replayMode==ReplayMode::Play?Result::ReturnToTitle:Result::StartNextMission;
        break;
    case MenuAction::SaveReplay:openReplaySaveList();break;
    }
}
int LevelScene::runtimeBestSlot() const{
    // 0x429450 scans all eleven shot slots and returns the first highest score.
    int best=-1;
    for(int i=0;i<int(m_photographs.size());++i)
        if(best<0||m_photographs[std::size_t(i)].score>m_photographs[std::size_t(best)].score)best=i;
    return best;
}
LevelScene::Result LevelScene::update(platform::InputState input){
    // 0x435350/0x434830 run once per logic frame even while a menu is open:
    // record mode appends the live input word, playback mode consumes one
    // record. The menu task (priority 5) reads the live word_4BE21E, so a
    // recorded Esc/confirm must never drive the page; only gameplay uses the
    // injected 244/24A/24C words.
    const platform::InputState live=input;
    if(m_replayMode==ReplayMode::Play){
        platform::InputState recorded;
        if(!advanceReplay(recorded)&&!m_menuOpen){
            m_result=Result::ReturnToTitle;return m_result;
        }
        if(m_menuOpen)input=live;
        else{input=recorded;input.pause=false;input.confirm=false;input.cancel=false;}
    }
    if(m_replayMode==ReplayMode::Record)captureReplayTick(live);
    // Must run before the outcome guard: the boss-killing shutter can flip
    // m_result to Ready on the same tick its texture needs to be copied.
    if(m_photoReadbackSlot>=0) {
        // The previous render left the shutter texture on the GPU; copy it
        // outside BeginScene and keep BGR triples for the bestshot file.
        auto &photo=m_photographs[std::size_t(m_photoReadbackSlot)];
        std::vector<uint8_t> bgra;
        if(m_device&&photo.pixelWidth>0&&photo.pixelHeight>0&&
           photo.texture.readPixels(m_device,photo.pixelWidth,photo.pixelHeight,bgra)) {
            const std::size_t count=std::size_t(photo.pixelWidth)*std::size_t(photo.pixelHeight);
            photo.pixels.resize(count*3);
            for(std::size_t i=0;i<count;++i) {
                photo.pixels[i*3]=bgra[i*4];
                photo.pixels[i*3+1]=bgra[i*4+1];
                photo.pixels[i*3+2]=bgra[i*4+2];
            }
        } else {
            log::print("error : photo readback failed (%dx%d)\r\n",
                       photo.pixelWidth,photo.pixelHeight);
        }
        // retail routine state 2 stores the best shot while the photo sequence runs,
        // not when the success page opens: a personal best taken in a failed
        // attempt still replaces the record's +24/+60/+64.
        stageShotRecord(photo);
        m_photoReadbackSlot=-1;
    }
    if(m_menuOpen){
        // The replay save pages own their own input and skip the result rows.
        if(m_menuKind==MenuKind::ReplayList||m_menuKind==MenuKind::ReplayName){
            updateReplaySave(input);
            return m_result;
        }
        // Retail states 2/4/6/8/10/12 ignore every key for eight ticks after a
        // confirm or pause edge; the rows play their label-1 fade-out in the
        // meantime and the action is applied when the wait expires.
        if(m_menuAction!=MenuAction::None){
            ++m_menuExitTicks;
            if(m_menuKind==MenuKind::Success)m_bestPhoto.update();
            for(auto&a:m_failureText)a.update();
            for(int i=0;i<m_menuCount;++i)m_menu[i].update();
            if(m_menuExitTicks>=8)applyMenuAction();
            return m_result;
        }
        ++m_menuTicks;
        // sub_429AB0 applies the selected/unselected labels ten ticks after the
        // page opens, not while the rows are still fading in.
        if(m_menuTicks==10){
            for(int i=0;i<m_menuOptionCount;++i)m_menu[m_menuOptionBase+i].interrupt(3);
            m_menu[m_menuOptionBase+m_menuSelection].interrupt(2);
        }
        if(m_menuTicks>=10){
            if(m_menuKind==MenuKind::Success&&!m_playbackOutcome){
                // retail routine keeps the photo cursor separate from the option rows:
                // left/right wrap through the strip, S makes the shown photo
                // the record's Best Shot even when its score is lower.
                if(m_successBrowse&&(input.left||input.right||input.leftRepeat||input.rightRepeat)){
                    const int step=(input.left||input.leftRepeat)?-1:1;
                    const int slot=(m_bestSlot+step+m_successPhotos)%m_successPhotos;
                    if(slot!=m_bestSlot){
                        m_bestSlot=slot;m_bestPhoto.start(m_photo,2*m_bestSlot+1);m_bestPhoto.update();playMenuSound(12);
                    }
                }
                if(input.showRate&&m_bestSlot>=0&&!m_photographs[std::size_t(m_bestSlot)].pixels.empty()){
                    applyBestShot(m_bestSlot);playMenuSound(41);
                }
            }
            if(input.up||input.down||input.upRepeat||input.downRepeat){
                m_menu[m_menuOptionBase+m_menuSelection].interrupt(3);m_menuSelection=(m_menuSelection+((input.up||input.upRepeat)?m_menuOptionCount-1:1))%m_menuOptionCount;m_menu[m_menuOptionBase+m_menuSelection].interrupt(2);playMenuSound(12);
            }
            if(m_menuKind==MenuKind::Pause){
                // Retail state 1/11 tests the pause key before confirm: Esc always
                // resumes (selection is forced to row zero). X is not handled on
                // this page at all, so it must not apply the highlighted row.
                if(input.pause){
                    playMenuSound(11);
                    m_menuSelection=0;
                    beginMenuExit(MenuAction::Resume,0);
                }else if(input.confirm){
                    if(m_menuSelection==0){playMenuSound(11);beginMenuExit(MenuAction::Resume,0);}
                    else if(m_menuSelection==1){playMenuSound(10);beginMenuExit(MenuAction::Restart,m_menuCount);}
                    else{playMenuSound(10);beginMenuExit(MenuAction::Leave,m_menuCount);}
                }
            }else if(m_playbackOutcome){
                // Retail states 7/9: row 0 rewinds the replay, row 1 ends it.
                if(input.confirm){
                    playMenuSound(10);
                    beginMenuExit(m_menuSelection==0?MenuAction::Restart:MenuAction::Leave,m_menuCount);
                }
            }else if(input.confirm&&m_menuKind==MenuKind::Success){
                // Success rows: retake / next scene / return / save replay.
                playMenuSound(10);
                const MenuAction action=m_menuSelection==0?MenuAction::Restart:
                    m_menuSelection==1?MenuAction::NextScene:
                    m_menuSelection==2?MenuAction::Leave:MenuAction::SaveReplay;
                beginMenuExit(action,1);
            }else if(input.confirm){
                // Failure rows: retry / return / save replay. Only retry uses se 11.
                playMenuSound(m_menuSelection==0?11:10);
                const MenuAction action=m_menuSelection==0?MenuAction::Restart:
                    m_menuSelection==1?MenuAction::Leave:MenuAction::SaveReplay;
                beginMenuExit(action,1);
            }
        }
        if(m_menuKind==MenuKind::Success&&!m_playbackOutcome&&m_bestOverwriteTicks>0)--m_bestOverwriteTicks;
        // retail routines freeze gameplay and display a tinted capture.
        if(m_menuKind==MenuKind::Success)m_bestPhoto.update();
        for(auto&a:m_failureText)a.update();
        for(int i=0;i<m_menuCount;++i)m_menu[i].update();
        return m_result;
    }
    // Recording already appended this frame's live word at the top of update.
    platform::InputState effective=input;
    if(effective.pause){openMenu(false);return m_result;}
    const auto shutters=m_sim.shutterEvent;
    const auto oldCameraState=m_sim.cameraState;
    m_sim.update(effective);
    if(m_sim.fault){
        if(m_audio){m_audio->stopEffect(42);m_audio->stopEffect(44);}
        m_result=m_replayMode==ReplayMode::Play?Result::ReturnToTitle:Result::Failed;
        return m_result;
    }
    if(m_profile&&m_replayMode==ReplayMode::Record&&m_sim.attempts!=m_profileAttempts) {
        // retail routine bumps the scene shot counter once per shutter release, capped
        // at 999999 in the retail code.
        const int delta=std::max(0,m_sim.attempts-m_profileAttempts);
        m_profileAttempts=m_sim.attempts;
        if(delta) {
            const uint32_t shots=m_profile->field(m_mission,68);
            m_profile->setField(m_mission,68,std::min<uint32_t>(shots+uint32_t(delta),0xF423Fu));
        }
    }
    if(m_audio){
        if(m_resumeEffects){
            if(m_sim.charging())m_audio->playEffect(42);
            if(m_sim.cameraState==1&&oldCameraState==1)m_audio->playEffect(44);
            m_resumeEffects=false;
        }
        for(const auto&e:m_sim.sounds()){if(e.stop)m_audio->stopEffect(e.id);else m_audio->playEffect(e.id,e.x);}
    }
    // The automatic slot-10 discard also opens the result page in retail, even
    // though it never counts as an attempt.
    if(m_sim.shutterEvent!=shutters) {
        beginPhoto();
    }
    if(m_resultAge>0&&++m_resultAge==21){
        if(m_audio)m_audio->playEffect(m_sim.photoSuccess?33:37,m_captureX-320);
        m_shotNotice.start(m_photo,m_sim.photoSuccess?33:34,320+m_sim.playerX,16+m_sim.playerY);
    }
    m_world.update(m_sim.timeScale);syncVisuals();
    // Playback shows the same result pages: the recorded keys then select one of
    // the rows exactly as they did in the source run.
    if(m_sim.outcomeReady()&&!m_menuOpen)openOutcome(m_sim.cleared&&!m_sim.dead);
    return m_result;
}
void LevelScene::syncLeafMarks(){
    // 0x42AD60 init builds the markers once per scene with k ascending from
    // the bottom gutter slot (y=400). Every recorded shot schedules case 0xA
    // at sequence number 10 of the photo event run: sub_445170 interrupts the
    // marker at index photos-1 with label 1 (script 12's 20-tick alpha fade),
    // so the bottom-most marker starts fading first and the row shrinks
    // upward. The fading marker keeps ticking until op1 removes it.
    const int required=std::max(0,m_sim.requiredPhotos);
    while(m_markCreated<required){
        const int k=m_markCreated;
        format::AnmAnimation leaf;
        if(!leaf.start(m_front,12,k<5?64.0f:576.0f,400.0f-float(k%5)*80.0f))break;
        ++m_markCreated;
        m_leafMarks.push_back(std::move(leaf));
    }
    const int target=std::max(0,required-m_sim.photos);
    if(int(m_leafMarks.size())-m_markPending>target)++m_markPending;
    if(m_markPending>0){
        if(m_markDelay==0)m_markDelay=10;
        else if(--m_markDelay==0){
            m_markDelay=0;--m_markPending;
            m_leafMarks.front().interrupt(1);
            m_leafFades.push_back(std::move(m_leafMarks.front()));
            m_leafMarks.erase(m_leafMarks.begin());
        }
    }
    for(auto&leaf:m_leafMarks)leaf.update(m_sim.timeScale);
    for(auto&fade:m_leafFades)fade.update(m_sim.timeScale);
    m_leafFades.erase(std::remove_if(m_leafFades.begin(),m_leafFades.end(),
        [](const format::AnmAnimation&fade){return !fade.alive();}),m_leafFades.end());
}
void LevelScene::syncLeafFlashes(){
    // 0x42AD60 runs one photo-event sequence number per frame after a shot is
    // counted (the trigger frame runs sequence 0). case 0x23 fires at sequence
    // 35 — once photo.anm's 30-tick op56 position tween has settled — and
    // reads the photo frame animation's translation slot (kernel +328) plus
    // (0,-0.4*frameH/2); frameH is the bordered capture height the retail
    // capture step stored at kernel +68 (= Photograph::pixelHeight). It parks
    // front.anm script 10 (orange leaf) in the per-photo slot and drops
    // script 11 (white flash) 6 pixels above; nothing ever interrupts either
    // one again, so every taken photo keeps its leaf for the rest of the
    // scene. A new shot only re-arms the sequence toward the newest photo.
    if(m_leafFlashSeen!=m_sim.photos){
        if(m_sim.photos>m_leafFlashSeen&&m_sim.photos>0)m_leafFlashDelay=35;
        m_leafFlashSeen=m_sim.photos;
    }else if(m_leafFlashDelay>0&&--m_leafFlashDelay==0&&m_sim.photos>0){
        const auto&p=m_photographs[std::size_t(std::clamp(m_sim.photos-1,0,9))];
        const float h=p.pixelHeight>0?float(p.pixelHeight):m_captureHeight;
        const float x=p.animation.x();
        const float y=p.animation.y()-h*0.2f;
        format::AnmAnimation leaf,flash;
        if(leaf.start(m_front,10,x,y))m_leafFlashes.push_back(std::move(leaf));
        if(flash.start(m_front,11,x,y-6.0f))m_leafFlashes.push_back(std::move(flash));
    }
    for(auto&flash:m_leafFlashes)flash.update(m_sim.timeScale);
    m_leafFlashes.erase(std::remove_if(m_leafFlashes.begin(),m_leafFlashes.end(),
        [](const format::AnmAnimation&flash){return !flash.alive();}),m_leafFlashes.end());
}
void LevelScene::syncVisuals(){
    syncLeafMarks();
    syncLeafFlashes();
    if(m_seenCollisionEvent!=m_sim.collisionEvent){
        m_seenCollisionEvent=m_sim.collisionEvent;
        const float x=320+m_sim.collisionX,y=16+m_sim.collisionY;
        m_deathFlash.start(m_bullet,289,x,y);
        m_deathParticles.clear();m_deathParticles.reserve(32);
        for(unsigned i=0;i<32;++i){
            format::AnmAnimation a;a.start(m_bullet,290,x,y,0x9E3779B9u*(i+1));
            m_deathParticles.push_back(std::move(a));
        }
    }
    // Retail CardInf (0x408850 -> 0x408670) is created on ECL 104 and starts
    // TWO animations: text.anm script 30 (the painted spell name) and
    // ascii.anm descriptor slot 1 (sprite 110, the blue sumi-e brush used as
    // the banner backdrop). Both get the field origin (128+192,16) from
    // sub_4186D0. ECL 105 calls sub_4087D0, which interrupts BOTH with label 1.
    // The ascii file's two script ids are -2/-1, so the retail index (1) must
    // be resolved through the descriptor table rather than used as an id.
    if(m_spellSeenEvent!=m_sim.spellEvent){
        m_spellSeenEvent=m_sim.spellEvent;
        if(m_sim.spellActive&&!m_sim.spellName.empty()){
            if(m_device){
                // text.anm places the spell strip on row 6 (sprite 5, v=108);
                // the atlas helper paints row i at i*rowStep. Retail retail address
                // starts script 30 with a zero offset (retail routine(...,30,vec0)),
                // and retail routine adds the field origin (128+192,16) when the
                // anim is drawn. Script 30 itself sets op48=(176,400) with
                // op65 right/top anchors, so the strip lands on x [176,496],
                // y [416,433].
                std::vector<std::string> rows(7);
                rows[6]=m_sim.spellName;
                platform::Texture atlas;
                if(atlas.fromGameTextRows(m_device,rows,RGB(255,255,255),
                                          m_codePage,15,18,256,30,320,true))
                    m_spellAtlas=std::move(atlas);
            }
            m_spellName.start(m_spellText,30,320,16);
            if(const auto brush=m_ascii.scriptIdAt(1))
                m_spellBrush.start(m_ascii,*brush,320,16);
        }else{
            m_spellName.interrupt(1);
            m_spellBrush.interrupt(1);
        }
    }
    m_spellName.update(m_sim.timeScale);
    m_spellBrush.update(m_sim.timeScale);
    // 0x4089F0 overwrites the ARGB alpha byte (+547) of both CardInf anims on
    // every frame from the player's vertical position: 64 near the top of the
    // field, ramping to 255 at y=128. Apply it as a draw-time modulation so
    // the scripts' own fade-in/fade-out tweens are preserved.
    // Retail retail routine advances visual interpolation every render/update
    // call using the fractional global scale. Do not accumulate integer
    // ticks: at 0.5/0.25 that produces visible two/four-frame holds.
    m_deathFlash.update(m_sim.timeScale);
    for(auto&a:m_deathParticles)a.update(m_sim.timeScale);
    // 0x41D460 owns 150 effect-manager slots for the bullet sparks.  When all
    // of them are busy the request is dropped and only the 294 explosion of
    // that bullet survives.
    size_t sparkSlots=size_t(std::count_if(m_effects.begin(),m_effects.end(),[](const auto&v){return v.managed;}));
    for(const auto&e:m_sim.effects()){
        if(e.script>=1000){
            if(e.script==1002){
                // ECL 0x69: retail routine retires the text, retail routine the enm
                // rows and retail routine frees the declaration streak instance.
                m_entryEffects.erase(std::remove_if(m_entryEffects.begin(),m_entryEffects.end(),[&](const auto&v){return v.follow==e.follow;}),m_entryEffects.end());
                m_effects.erase(std::remove_if(m_effects.begin(),m_effects.end(),[&](const auto&v){return v.animation.scriptId()==210;}),m_effects.end());
                continue;
            }
            const int row=e.script-1000;
            auto it=std::find_if(m_entryEffects.begin(),m_entryEffects.end(),[&](const auto&v){return v.follow==e.follow;});
            if(it==m_entryEffects.end()){EntryEffect v;v.follow=e.follow;m_entryEffects.push_back(std::move(v));it=std::prev(m_entryEffects.end());}
            // ECL 0x68 creates both declaration backdrops through the background
            // manager with a zero position: the anim origin stays at the field
            // centre and the 384x448 sprite anchors (enm1 op65) put it over
            // (128,16,384,448). It never follows the boss.
            if(row<2&&!it->rows[row].alive())it->rows[row].start(m_enemy,row,320,16);
            continue;
        }
        Effect effect;effect.follow=e.follow;
        if(e.script==210){
            // ECL 0x68 frees the previous streak (retail routine) before the new
            // instance starts, so only one declaration streak is ever alive.
            m_effects.erase(std::remove_if(m_effects.begin(),m_effects.end(),[&](const auto&v){return v.animation.scriptId()==210;}),m_effects.end());
            // The ring starts on the enemy and is dragged toward it afterwards.
            effect.x=e.x;effect.y=e.y;
        }
        if(effect.animation.start(m_bullet,e.script,320+e.x,16+e.y,0x9E3779B9u*unsigned(m_sim.frames+m_effects.size()+1))){effect.animation.update();
            if(e.script==210){
                // ECL 0x68 copies player +272 into ANM integer variable 10008
                // (+312). ECL 0x72 initialises +272 to the scene time limit and
                // it counts down, so the value is the remaining scene frames,
                // not the elapsed ones. Script 210's second 0x3C uses it as the
                // shrink duration, which keeps the declaration ring at full
                // radius for effectively the rest of the scene.
                effect.animation.setInteger(10008,static_cast<int>(m_sim.remaining));
            }
            if(e.color)effect.animation.setColor(e.color);
            if(e.script==288){
                if(sparkSlots>=150)continue;
                ++sparkSlots;
                // 0x41D460 seeded the slot when the gameplay module created
                // it (0x4168D0/0x407C90), so reuse those global-stream draws
                // instead of inventing a per-frame hash.
                effect.managed=true;effect.x=e.x;effect.y=e.y;
                effect.vx=e.vx;
                effect.vy=e.vy;
            }
            m_effects.push_back(std::move(effect));}
    }
    for(auto&e:m_effects){
        if(e.animation.scriptId()==210){
            // Retail sub_415970 keeps the declaration streak (script 210) on the
            // enemy that declared the card: every tick it moves the animation
            // 7% of the remaining distance toward enemy position + (320,16), so
            // the film ring trails the boss instead of staying at its birth
            // point. The script's own radius/band tween is untouched.
            // Retail sub_415970 moves the declaration streak 7% of the
            // remaining distance toward enemy position + (320,16) on every
            // update, independent of the global slowdown scale.
            if(e.follow){
                const auto i=std::find_if(m_sim.enemies().begin(),m_sim.enemies().end(),[&](const auto&enemy){return enemy.id==e.follow;});
                if(i!=m_sim.enemies().end()){
                    const float step=0.07f;
                    e.x+=(i->x-e.x)*step;e.y+=(i->y-e.y)*step;
                }
            }
            e.animation.setOffset(320+e.x,16+e.y);
        }else if(e.follow){
            const auto i=std::find_if(m_sim.enemies().begin(),m_sim.enemies().end(),[&](const auto&enemy){return enemy.id==e.follow;});
            if(i==m_sim.enemies().end()||i->warning<=0){e.animation={};continue;}
            e.animation.setOffset(320+i->x,16+i->y);
            const float scale=2*i->warning/std::max(1.0f,i->warningDuration);e.animation.setScale(scale,scale);
        }
        if(e.managed){
            // 0x41CE60: the first four ticks only advance the animation, the
            // next sixteen drift with a (20-time)/20 speed decay, and after that
            // the spark chases the player at 0.1..8 pixels per tick. A spark
            // inside the player sprite box is freed by the manager.
            e.age+=m_sim.timeScale;
            if(e.age>=20.0f){
                const float dx=m_sim.playerX-e.x,dy=m_sim.playerY-e.y;
                const float length=std::sqrt(dx*dx+dy*dy);
                if(length>0){e.vx=dx/length;e.vy=dy/length;}
                e.x+=e.vx*e.speed;e.y+=e.vy*e.speed;
                if(e.speed<8.0f)e.speed+=0.1f;
            }else if(e.age>=4.0f){
                const float factor=(20.0f-e.age)/20.0f;
                e.x+=e.vx*factor;e.y+=e.vy*factor;
            }
            e.animation.setOffset(320+e.x,16+e.y);
            // retail player +1008/+1012 half extents, modelled as the eight
            // pixel half box the scene already uses for the player.
            constexpr float half=8.0f;
            if(e.age>=4.0f&&std::fabs(e.x-m_sim.playerX)<=half&&std::fabs(e.y-m_sim.playerY)<=half){
                e.dead=true;
                // 0x41CE60 frees the slot inside the player box and queues se 20
                // at the spark position (0x439030).
                if(m_audio)m_audio->playEffect(20,e.x);
            }
        }
        e.animation.update(m_sim.timeScale);
    }
    m_effects.erase(std::remove_if(m_effects.begin(),m_effects.end(),[](const auto&e){return !e.animation.alive()||e.dead;}),m_effects.end());
    for(auto&e:m_entryEffects){
        const auto i=std::find_if(m_sim.enemies().begin(),m_sim.enemies().end(),[&](const auto&enemy){return enemy.id==e.follow;});
        if(i==m_sim.enemies().end()){e.rows[0]={};e.rows[1]={};continue;}
        // Background rows keep the field origin; they are not parented to the
        // boss that declared the card.
        for(auto&a:e.rows)a.update();
    }
    m_entryEffects.erase(std::remove_if(m_entryEffects.begin(),m_entryEffects.end(),[](const auto&e){return !e.rows[0].alive()&&!e.rows[1].alive();}),m_entryEffects.end());
    // Retail retail address keeps one bullet.anm script 295 anim per boss while
    // its spawn-circle flag (ECL 143) is armed, re-creating it when the
    // handle is gone and releasing it as soon as the flag drops.
    for(const auto&e:m_sim.enemies()){
        const bool active=e.alive&&e.boss&&e.spawnCircle;
        auto it=m_spawnCircles.find(e.id);
        if(!active){if(it!=m_spawnCircles.end())m_spawnCircles.erase(it);continue;}
        if(it==m_spawnCircles.end()){
            format::AnmAnimation circle;circle.start(m_bullet,295,320+e.x,16+e.y);circle.update();
            it=m_spawnCircles.emplace(e.id,std::move(circle)).first;
        }
        it->second.setOffset(320+e.x,16+e.y);
        it->second.update(m_sim.timeScale);
    }
    std::set<unsigned> ids;
    for(const auto&e:m_sim.enemies()){
        ids.insert(e.id);auto&v=m_enemyVisuals[e.id];
        if(v.script!=e.animation){v.animation.start(m_enemy,e.animation);v.script=e.animation;}
        v.animation.setOffset(320+e.x,16+e.y);v.animation.update(m_sim.timeScale);
    }
    for(auto i=m_enemyVisuals.begin();i!=m_enemyVisuals.end();)if(!ids.count(i->first))i=m_enemyVisuals.erase(i);else ++i;
    ids.clear();
    for(const auto&b:m_sim.bullets()){
        ids.insert(b.id);auto&v=m_bulletVisuals[b.id];const int script=bulletScripts[std::clamp(b.type,0,23)]+b.color;
        if(v.script!=script||v.revision!=b.spriteRevision){v.animation.start(m_bullet,script);v.animation.interrupt(b.appearance>0?(b.flags&8?9:b.flags&4?8:7):2);v.script=script;v.revision=b.spriteRevision;}
        v.animation.update(m_sim.timeScale);
        v.animation.setOffset(320+b.x,16+b.y);v.animation.setRotation(b.angle+1.5707963f);
    }
    for(auto i=m_bulletVisuals.begin();i!=m_bulletVisuals.end();)if(!ids.count(i->first))i=m_bulletVisuals.erase(i);else ++i;
    const int playerScript=m_sim.cameraState==1?5:m_sim.playerAnimation;
    if(m_playerScript!=playerScript){m_playerVisual.start(m_player,playerScript);m_playerScript=playerScript;}
    m_playerVisual.setOffset(320+m_sim.playerX,16+m_sim.playerY);m_playerVisual.update();
    // retail routine uses the bullet manager's script 287 on layer 6.
    if(m_sim.focused&&!m_wasFocused)m_hitpoint.start(m_bullet,287);
    if(!m_sim.focused&&m_wasFocused)m_hitpoint.interrupt(1);
    m_wasFocused=m_sim.focused;
    m_hitpoint.setOffset(320+m_sim.playerX,16+m_sim.playerY);m_hitpoint.update();
    if(m_sim.dead)m_hitpoint={};
    if(playerScript==5){
        // retail routine derives the pose from the compass angle of the vector from the
        // player to the viewfinder centre: 0 = up, advancing clockwise. Sprite 24 is the
        // upward pose, so sprite = 24 + sector with an eighth-turn bias for rounding.
        const float dx=m_sim.cameraX-m_sim.playerX,dy=m_sim.cameraY-m_sim.playerY;
        float a=std::atan2(dx,-dy)+0.3926991f;
        if(a<0.0f)a+=6.2831855f;
        m_playerVisual.setSprite(24+int(a/0.7853982f)%8);
    }
    for(auto&a:m_borders)a.update();
    m_intro.update();
    const int ticks=std::max(0,int(m_sim.remaining)),seconds=ticks/60,hundredths=ticks%60*100/60;
    const int digits[]={seconds/100,seconds/10%10,seconds%10,10,hundredths/10,hundredths%10};
    const int timerColor=ticks<300?8:ticks<600?7:0;
    for(int i=0;i<6;++i){auto&a=m_timer[i];if(timerColor&&timerColor!=m_timerColor)a.interrupt(timerColor);a.update();a.setSprite(4+digits[i]);a.setAlpha(m_sim.remaining>0?255:0);}m_timerColor=timerColor;
    // retail routine creates only 22/23 for the ordinary reticle. Keep their own
    // fade, rotation and charge pulse instead of overwriting scale/alpha.
    if(m_sim.frames>=60&&(m_sim.cameraState==0||m_sim.cameraState==3)){
        const int branch=m_sim.charge>=1?2:3;
        if(branch!=m_cameraChargeBranch){
            for(auto&a:m_camera)a.interrupt(branch);
            for(auto&a:m_percent)a.interrupt(branch);
            m_cameraChargeBranch=branch;
        }
    }
    const bool bossInFrame=m_sim.targetInFrame();
    m_camera[0].animateColor((m_sim.cameraState==1||m_sim.charge>=1)&&bossInFrame?0xFFFF2020:0xFFFFFFFF,1);
    for(auto&a:m_camera){a.update();a.setOffset(320+m_sim.cameraX,16+m_sim.cameraY);a.setEnabled(!m_sim.dead);}
    for(auto&a:m_photoResult)a.update();
    for(auto&a:m_resultDigits)a.update();
    for(auto&p:m_photographs){p.animation.update();for(auto&a:p.digits)a.update();}
    m_shotNotice.update();
    const int percent=std::clamp(int(m_sim.charge*100),0,100);const int p[]={percent/100,percent/10%10,percent%10,10};
    for(int i=0;i<4;++i){auto&a=m_percent[i];a.update();a.setOffset(320+m_sim.cameraX-36+9*i,16+m_sim.cameraY-12);a.setSprite(15+p[i]);a.setEnabled(!m_sim.dead&&(i>=2||(i==0?percent>=100:percent>=10)));}
    if(m_sim.cameraState==1){
        if(m_oldCameraState!=1){
            for(int i=0;i<5;++i)m_aim[i].start(m_photo,24+i);
            // retail routine binds these inline VMs only on their first activation;
            // later photographs resume their existing UV scroll phase.
            for(int i=0;i<3;++i){if(!m_aimOutside[i].alive())m_aimOutside[i].start(m_photo,37+i);m_aimOutside[i].setEnabled(true);}
        }
        if(m_oldCameraState!=1||bossInFrame!=m_wasBossInFrame)for(auto&a:m_aim)a.interrupt(bossInFrame?2:3);
        // retail routine orders corners bottom-left, top-left, top-right, bottom-right.
        for(int i=0;i<4;++i){auto&a=m_aim[i];a.update();const float x=(i<2?-1:1)*m_sim.photoWidth/2,y=(i==0||i==3?1:-1)*m_sim.photoHeight/2;a.setOffset(320+m_sim.cameraX+x,16+m_sim.cameraY+y);}
        m_aim[4].update();m_aim[4].setOffset(320+m_sim.cameraX,16+m_sim.cameraY);m_aim[4].setScale(m_sim.charge*2,m_sim.charge*2);
        // retail routine temporarily sets timeScale=1 for these three animations.
        for(auto&a:m_aimOutside)a.update();
        m_cameraStatus={};
    }else if(m_oldCameraState==1){m_aim={};for(auto&a:m_aimOutside)a.setEnabled(false);}
    if(m_sim.frames>=60&&(m_sim.cameraState==0||m_sim.cameraState==3)){
        const int script=m_sim.charge>=1?31:32;
        if(m_oldCameraState!=0||!m_cameraStatus.alive()||m_cameraStatus.spriteId()!=(script==31?27:28))m_cameraStatus.start(m_photo,script);
        m_cameraStatus.setOffset(320+m_sim.cameraX,16+m_sim.cameraY);m_cameraStatus.update();
    }
    m_wasBossInFrame=bossInFrame;
    m_oldCameraState=m_sim.cameraState;
}
}
