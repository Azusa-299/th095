#pragma once
#include <filesystem>
#include <map>
#include "level_sim.hpp"
#include "level_world.hpp"
#include "../formats/replay.hpp"
#include "../platform/audio.hpp"
#include "../formats/score.hpp"
#include "../formats/config.hpp"

namespace th95::scene {
class LevelScene {
public:
    enum class Result {Running,ReturnToTitle,ReturnToMissionSelect,StartNextMission,Failed};
    // When replayPath is not empty the header supplies the level, scene and
    // seed; mission is ignored. dataDirectory is the folder holding replay/.
    bool load(Archive&,IDirect3DDevice9*,int mission=0,platform::Audio* audio=nullptr,
              format::ScoreProfile* profile=nullptr,
              const std::filesystem::path& replayPath={},
              const std::filesystem::path& dataDirectory={},
              const format::GameConfig* config=nullptr);
    Result update(platform::InputState input);
    void render(IDirect3DDevice9*);
    bool loaded() const {return m_loaded;}
    int score() const {return m_sim.score;}
    int mission() const {return m_mission;}
    const char* bgmName() const {return m_sceneBgm;}
    bool playingReplay() const {return m_replayMode==ReplayMode::Play;}
    LevelSimulation& simulation(){return m_sim;}
    const LevelSimulation& simulation()const{return m_sim;}
    const format::AnmAnimation& hitpoint()const{return m_hitpoint;}
    const auto& reticle()const{return m_camera;}
    const auto& aimAnimations()const{return m_aim;}
    const auto& aimOutside()const{return m_aimOutside;}
    const auto& chargeDigits()const{return m_percent;}
    const format::AnmAnimation& deathFlash()const{return m_deathFlash;}
    const format::AnmAnimation& spellName()const{return m_spellName;}
    const format::AnmAnimation& spellBrush()const{return m_spellBrush;}
    const auto& deathParticles()const{return m_deathParticles;}
    const auto& spawnCircles()const{return m_spawnCircles;}
    const auto& leafMarks()const{return m_leafMarks;}
    const auto& leafFades()const{return m_leafFades;}
    const auto& leafFlashes()const{return m_leafFlashes;}
    int lastMenuSound()const{return m_menuSound;}
    void recordFrame(double seconds){if(!m_menuOpen){++m_presentedFrames;if(seconds>1.0/60)m_slowFrames+=seconds*60-1;}}
private:
    struct Visual {format::AnmAnimation animation;int script=-1,revision=-1;};
    void reset();
    // Playback retry: restart the simulation and rewind the recorded stream.
    void restartReplayPlayback();
    Result leaveResult() const {return m_replayMode==ReplayMode::Play
        ?Result::ReturnToTitle:Result::ReturnToMissionSelect;}
    void syncVisuals();
    void openMenu(bool failed);
    void openOutcome(bool success);
    // Retail closes every level menu through a state that waits eight ticks
    // after the confirm/pause edge (0x426BF0 states 2/4/6/8/10/12). During
    // that wait the option rows run their label-1 fade-out and no input is
    // accepted. Keeping the action here reproduces the delay.
    enum class MenuAction {None,Resume,Restart,Leave,NextScene,SaveReplay};
    void beginMenuExit(MenuAction action,int fadeFrom);
    void applyMenuAction();
    int runtimeBestSlot() const;
    void commitRecord();
    void playMenuSound(int id);
    // Replay record/playback and the two result-page save screens.
    void beginReplayRecording();
    void captureReplayTick(const platform::InputState&);
    bool advanceReplay(platform::InputState&);
    void stampReplayResult();
    void openReplaySaveList();
    void updateReplaySave(const platform::InputState&);
    void rebuildNumberedSlots();
    void drawReplaySave(IDirect3DDevice9*);
    void ascii(IDirect3DDevice9*,const char*,float,float,DWORD=0xFFFFFFFF);
    void beginPhoto();
    void drawWorld(IDirect3DDevice9*,bool aimingEffects=false);
    void drawAimOutside(IDirect3DDevice9*);
    void drawPhotos(IDirect3DDevice9*);
    void capturePhoto(IDirect3DDevice9*);
    void renderGameplay(IDirect3DDevice9*);
    format::AnmTexture m_enemy,m_player,m_bullet,m_front,m_ascii,m_photo,m_pause;
    format::AnmTexture m_text;
    // CardInf paints the ECL spell-card name into text.anm page 0 and plays
    // script 30. A second AnmTexture keeps page 0 independent from the
    // failure-comment atlas, which shares the same page layout.
    format::AnmTexture m_spellText;
    platform::Texture m_spellAtlas;
    // Retail CardInf (0x408670) starts two animations on ECL 104: text.anm
    // script 30 for the name and ascii.anm script slot 1 for the blue sumi-e
    // brush (sprite 110, 256x40) behind it. ECL 105 interrupts both with
    // label 1. The ascii script is addressed by descriptor slot because its
    // two script ids are -2/-1 rather than 0/1.
    format::AnmAnimation m_spellName,m_spellBrush;
    unsigned m_spellSeenEvent=0;
    platform::Texture m_failureAtlas;
    IDirect3DDevice9* m_device=nullptr;
    unsigned m_codePage=932,m_failureIndex=0;
    std::vector<std::array<std::string,3>> m_failureComments;
    std::array<format::AnmAnimation,2> m_failureText;
    LevelWorld m_world;
    LevelSimulation m_sim;
    std::map<unsigned,Visual> m_enemyVisuals,m_bulletVisuals;
    format::AnmAnimation m_playerVisual,m_intro,m_hitpoint;
    format::AnmAnimation m_deathFlash;
    std::vector<format::AnmAnimation> m_deathParticles;
    struct Effect {
        format::AnmAnimation animation;
        unsigned follow=0;
        // bullet.anm 288 sparks created by the retail effect manager (0x407C90
        // shutter erase and 0x4168D0 rainbow burst) are moved and freed by that
        // manager, not by the script: their op 4 loops back forever, so the
        // animation never reports itself dead. 0x41CE60 drifts them for sixteen
        // ticks, then homes them into the player sprite box, where it is freed.
        bool managed=false,dead=false;
        float x=0,y=0,vx=0,vy=0,speed=0,age=0;
    };
    std::vector<Effect> m_effects;
    struct EntryEffect {unsigned follow=0;format::AnmAnimation rows[2];};
    std::vector<EntryEffect> m_entryEffects;
    // 0x42AD60 creates one front.anm script 12 leaf per required photo: five
    // down the left border at x=64, five down the right at x=576, y descending
    // from 400 by 80. The leaf is additive art (sprite 16) that fades in over
    // 60 ticks and then wiggles forever, so the markers persist for the scene.
    // case 0xA (sequence 10 of the photo event run) interrupts the marker at
    // index photos-1 with label 1: the bottom marker starts a 20-tick alpha
    // fade-out first and is moved to m_leafFades while it dies.
    std::vector<format::AnmAnimation> m_leafMarks,m_leafFades;
    int m_markCreated=0,m_markPending=0,m_markDelay=0;
    void syncLeafMarks();
    // 0x42AD60 case 0x23 (sequence 35) replaces the consumed gutter slot with
    // front.anm script 10 (orange leaf, sprite 15) and drops script 11 (the
    // additive white flash, sprite 17) 6 pixels above it, both anchored to the
    // photo frame animation's settled translation minus 0.2 * frame height.
    // Nothing ever interrupts the pair again: the retail handle is parked in
    // the per-photo slot (script 10 stops at t=1) and the dropped flash loops,
    // so every taken photo keeps its leaf for the rest of the scene.
    std::vector<format::AnmAnimation> m_leafFlashes;
    int m_leafFlashSeen=0,m_leafFlashDelay=0;
    void syncLeafFlashes();
    // bullet.anm script 295 circles kept per boss while its spawn-circle
    // flag is armed (retail retail address creates/releases the anim each frame).
    std::map<unsigned,format::AnmAnimation> m_spawnCircles;
    bool m_wasFocused=false,m_resumeEffects=false;
    std::array<format::AnmAnimation,4> m_borders,m_percent;
    std::array<format::AnmAnimation,6> m_timer;
    std::array<format::AnmAnimation,2> m_camera;
    std::array<format::AnmAnimation,5> m_aim;
    std::array<format::AnmAnimation,3> m_aimOutside;
    int m_cameraChargeBranch=0;
    struct Photograph {
        platform::Texture texture;
        format::AnmAnimation animation;
        std::array<format::AnmAnimation,6> digits;
        bool captured=false;
        int score=0;
        // retail routine stamps both on the slot before comparing it with +24, so the
        // success page can re-pick any shot with its own time and rate.
        uint32_t timestamp=0;
        float rate=0;
        // CPU copy including the three-pixel white border, BGR triples.
        std::vector<uint8_t> pixels;
        int pixelWidth=0,pixelHeight=0;
    };
    void stageShotRecord(Photograph &photo);
    void applyBestShot(int slot);
    std::array<Photograph,11> m_photographs;
    platform::Texture m_worldFrame;
    platform::Texture m_gameFrame,m_menuFrame;
    format::AnmAnimation m_bestPhoto;
    int m_bestSlot=-1;
    // retail routines start the success page on the highest-scoring slot and
    // only enable retail routine's browse cursor while it is inside the photo strip.
    int m_successPhotos=0;
    bool m_successBrowse=false;
    bool m_menuCapturePending=false;
    // Retail replay playback (dword_4BDEC8+288 != 0) opens its own result
    // states 7/9 with just the two replay rows (19/20), so the photo strip and
    // score panel of the live success page must stay hidden.
    bool m_playbackOutcome=false;
    std::vector<format::AnmAnimation> m_photoResult;
    std::array<format::AnmAnimation,6> m_resultDigits;
    format::AnmAnimation m_cameraStatus,m_shotNotice;
    bool m_capturePending=false,m_worldFrameReady=false,m_wasBossInFrame=false;
    int m_photoSlot=10,m_resultAge=0;
    // GetRenderTargetData runs outside BeginScene, so the shutter only marks
    // the slot and the next tick copies it into Photograph::pixels.
    int m_photoReadbackSlot=-1;
    float m_captureX=0,m_captureY=0,m_captureWidth=256,m_captureHeight=192;
    // pause.anm contains the retail pause, failed-result and success-result
    // layouts.  Keep the background/title rows together with their options so
    // the original animation coordinates and selection transitions are used.
    std::array<format::AnmAnimation,8> m_menu;
    platform::Audio* m_audio=nullptr;
    enum class MenuKind {None,Pause,Failure,Success,ReplayList,ReplayName};
    enum class ReplayMode {None,Record,Play};
    // 0x496398: six rows of sixteen cells for the Replay Name Regist page.
    // Row five's 93/94/95 cells are drawn as the 0x81/0x7F/0x80 icons.
    static constexpr char kReplayCharacters[97] =
        "ABCDEFGHIJKLMNOP"
        "QRSTUVWXYZ.,:;_@"
        "abcdefghijklmnop"
        "qrstuvwxyz+-/*=%"
        "0123456789#!?'\"$"
        "(){}[]<>&\\|~^ --";
    ReplayMode m_replayMode=ReplayMode::None;
    format::Replay m_replay;
    std::size_t m_replayTick=0;
    uint16_t m_replayHeld=0;
    std::filesystem::path m_dataDirectory;
    std::array<format::ReplaySlot,format::kReplayNumberedSlots> m_replaySlots;
    int m_replaySelection=0;
    std::array<char,8> m_replayName{};
    int m_replayNameLength=0,m_replayNameCursor=0;
    const char* m_sceneBgm="bgm/th095_01.wav";
    const format::GameConfig* m_gameConfig=nullptr;
    int m_mission=0,m_playerScript=-1,m_timerColor=0,m_menuSelection=0,m_menuTicks=0,m_menuCount=0,m_menuOptionBase=0,m_menuOptionCount=0,m_oldCameraState=0;
    MenuAction m_menuAction=MenuAction::None;
    int m_menuExitTicks=0;
    // 0x429C80 draws "BestShot was overwrited!" at (212,224) for 120 frames
    // after S promotes the displayed shot (0x4294C0 sets +28192 = 120).
    int m_bestOverwriteTicks=0;
    // Last sound id requested by the gameplay menus (0 when none yet).
    int m_menuSound=0;
    unsigned m_seenCollisionEvent=0;
    int m_sessionHighScore=0;
    // retail routine commits the cleared record; retail routine counts every shot.
    format::ScoreProfile *m_profile=nullptr;
    int m_profileAttempts=0;
    bool m_profileCommitted=false;
    uint32_t m_profileTotal=0;
    int m_profileHigh=0;
    // retail routine stored 100 - a/b*100 from two counters that are reset to zero
    // on entry; the port keeps its own frame-time based figure instead.
    double m_slowFrames=0;
    unsigned m_presentedFrames=0;
    bool m_loaded=false,m_menuOpen=false;
    MenuKind m_menuKind=MenuKind::None;
    Result m_result=Result::Failed;
};
}
