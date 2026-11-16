#pragma once
#include <array>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "../formats/ecl.hpp"
#include "../platform/input.hpp"
#include "../vm_diagnostics.hpp"

namespace th95::scene {
// TH095 enemy VM subset used by all six Level 1 scenes. Addresses in
// docs/lv1.md refer to the JP 1.02a executable; no executable is loaded.
class LevelSimulation {
public:
    struct Extension {int flags=0, a=0, b=0; float x=0,y=0; int concurrent=0;};
    struct Thread {
        int script=-1; std::size_t pc=0; float time=0,wait=0;
        std::array<int,8> integers{};std::array<float,8> floats{};
        std::array<int,4> counters{},intArguments{};std::array<float,4> arguments{};
        std::array<float,4> scratch{};
        struct Return {int script;std::size_t pc;float time;std::array<int,8> integers;std::array<float,8> floats;std::array<int,4> counters,intArguments;std::array<float,4> arguments,scratch;};
        std::vector<Return> stack;
    };
    struct Enemy {
        unsigned id=0;
        bool alive=true,boss=false,hidden=false,collision=false,entered=false;
        // Retail enemy+11252 window: bit 0x40 is cleared by ECL 0x50 bit 0 and
        // set by 0x51 bit 0 (attack/target window; on by default like the
        // constructor sets it); bit 0x4 is the contact-collision flag; bit 0x10
        // hides the enemy from both the render list and the shutter target scan;
        // bit 0x4000000 is the inverse of the "free the slot once it leaves the
        // field" check that ECL 0x51 bit 4 enables.
        bool attackable=true;
        bool boundsEnabled=false;
        bool boundsCheck=false;
        float x=0,y=0,vx=0,vy=0,angle=0,speed=0,angularVelocity=0,acceleration=0;
        float startX=0,startY=0,deltaX=0,deltaY=0,moveTime=0,moveDuration=0;
        float minX=-192,minY=0,maxX=192,maxY=448,shotX=0,shotY=0;
        float hitWidth=24,hitHeight=24;int moveMode=0,ease=0,animation=2,lastDirection=-1;
        int photoCount=0,shotSound=7,transformSound=24;
        // Retail words enemy+10330 (ECL 112) and enemy+566 (ECL 124). Both are
        // read by later engine paths that LV2 does not exercise, so they are
        // stored as-is until those consumers are ported.
        int word10330=0,word566=0;
        // Retail gates the ECL 0x53/0x54 spawn family on enemy+10584 > 0; the
        // constructor leaves it positive and no shipped script lowers it.
        int spawnGate=1;
        std::array<int,10> photoThreshold{{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}},photoScripts{};
        float cooldown=0,warning=0,warningDuration=0;
        // 0x40 flag of enemy+11256 plus the +11264/+11268 timer armed by
        // ECL opcode 143; retail address counts it down each tick.
        bool spawnCircle=false;float spawnCircleTimer=0;
        std::array<int,32> interrupts{};
        std::array<Extension,18> extensions{};
        Thread main;std::array<Thread,16> async;
    };
    struct Bullet {
        unsigned id=0;
        bool alive=true;float x=0,y=0,angle=0,speed=0,radius=4,age=0,vx=0,vy=0;
        int type=0,color=0,flags=0,extension=0;float extensionTime=0;
        int spriteRevision=0,sound=24;
        float appearance=0,grace=0;
        struct Active {Extension effect;float time=0,angle=0,speed=0;int repeats=0;};
        std::vector<Active> active;
        std::array<Extension,18> extensions{};
    };
    bool load(const std::vector<uint8_t>& ecl,const std::vector<uint8_t>& sht,int mission);
    void update(const platform::InputState& input);
    void restart();
    // A replay restores the recorded RNG seed before its first input tick.
    uint16_t seed() const{return uint16_t(m_seed);}
    void setSeed(uint16_t seed){m_seed=seed;}
    // 0x433140 scores each captured bullet from the height of its current
    // bullet.anm sprite: >8 gives 20, >16 gives 40 and >32 gives 150. The
    // sprite depends on the animation phase, so the scene bakes both phase
    // heights per script from the loaded bullet.anm.
    struct BulletTierHeights { float normal=16, appearing=30; };
    void setBulletTierHeights(std::map<int,BulletTierHeights> table){
        m_bulletTierHeights=std::move(table);
    }
    // Retail keeps one global 16-bit stream (word_4BE208) shared by the ECL VM
    // and the ANM VM. Effect creation draws from it too (0x41D460 uses
    // 0x41B4D0/0x41B4A0), so replay determinism requires the same draws.
    float nextUnit(){return unit();}
    float nextSignedUnit(){return unit()*2.0f-1.0f;}
    const std::deque<Enemy>& enemies() const{return m_enemies;}
    const std::vector<Bullet>& bullets() const{return m_bullets;}
    float playerX=0,playerY=480,cameraX=0,cameraY=400,charge=0.5f;
    float photoWidth=256,photoHeight=192,timeScale=1,remaining=0;
    int frames=0,playerAnimation=0,cameraState=0,photos=0,attempts=0,requiredPhotos=3;
    int score=0,highScore=0,lastPhotoScore=0;
    int photoTargets=0,photoBullets=0,photoResultTicks=0;
    bool photoSuccess=false,photoSelf=false;
    float photoDistanceMultiplier=1.2f;
    bool targetInFrame() const;
    int flash=0;bool cleared=false,dead=false,paused=false;
    // Fatal only: a script or table the scene cannot run without is missing.
    // Unsupported opcodes and variables are recorded in diagnostics() and the
    // frame keeps advancing, matching the retail VM's record-and-continue.
    bool fault=false;
    const std::vector<vm::Diagnostic> &diagnostics() const {return m_diagnostics;}
    unsigned diagnosticCount(vm::Fault kind) const;
    unsigned unsupportedCount() const;
    // Incremented when the player is hit so the scene can create the retail
    // bullet.anm death burst exactly once, even after the simulation enters
    // the outcome state.
    unsigned collisionEvent=0;
    // Bumped for every shutter, including the automatic slot-10 discard when the
    // charge runs out. The result page follows this, not attempts (retail does not
    // count an automatic shutter as an attempt).
    unsigned shutterEvent=0;
    float collisionX=0,collisionY=0;
    // ECL opcode 104 stores the declared spell-card name as a 48-byte CP932
    // string XOR 0xAA (retail routine). The scene paints it through text.anm
    // script 30 (CardInf). spellEvent changes on declare and on clear so the
    // scene starts or interrupts the banner exactly once per transition.
    std::string spellName;
    unsigned spellEvent=0;
    bool spellActive=false;
    float failureAge=0,successAge=-20;
    bool outcomeReady()const{return dead?failureAge>=30:cleared&&successAge>=30;}
    // color is the retail ARGB tint (bullet.anm object +544); zero keeps the
    // animation's own colours. Photographed enemies spawn five coloured copies
    // of script 288, one per palette entry.
    // 0x41D460 seeds each script 288 spark slot with a random horizontal
    // drift (-1..1) and downward speed (2..4). The draws happen when the slot
    // is created, so they belong to the global stream and are captured here.
    struct EffectEvent {int script;float x,y;unsigned follow=0;unsigned color=0;
                        float vx=0,vy=0;};
    const auto& effects()const{return m_effects;}
    unsigned emitted=0,executed=0;
    unsigned volleyEvent=0;
    float volleyX=0,volleyY=0;
    // Original retail routines coalesce up to 128 positions per sound/tick.
    struct SoundEvent {int id;float x;bool stop;int count=1;};
    const std::vector<SoundEvent>& sounds()const{return m_sounds;}
    bool charging()const{return m_charging;}
    bool focused=false;
    float scoreMultiplier=1;
    bool invulnerable=false; // Diagnostic fixture only; never enabled by normal gameplay.
private:
    float variable(Enemy&,Thread&,int id);
    void assign(Enemy&,Thread&,int id,float value,bool integer);
    void run(Enemy&,Thread&);
    void move(Enemy&);
    void emit(Enemy&,Thread&,const format::EclCommand&);
    void spawn(int script,float x,float y,const Thread* parent=nullptr);
    void photograph();
    void runShutterErase();
    void tickBullet(Bullet&);
    void sound(int id,float x=0,bool stop=false);
    void advanceActors();
    void recordFault(vm::Fault kind,uint16_t opcode=0,uint32_t offset=0,int detail=0);
    uint32_t random();float unit();
    format::EclFile m_ecl;
    std::deque<Enemy> m_enemies;
    std::vector<Bullet> m_bullets;
    std::vector<SoundEvent> m_sounds;
    std::vector<EffectEvent> m_effects;
    // 0x42AD60 (photo manager, priority 8) runs 0x407C90 three frames after the
    // shutter and then every frame: every bullet still touching the frozen photo
    // rectangle is erased and becomes a 294 burst plus one 288 spark.
    std::map<int,BulletTierHeights> m_bulletTierHeights;
    bool m_erasing=false;
    int m_burstDraws=0;
    int m_eraseTicks=0;
    float m_eraseX=0,m_eraseY=0,m_eraseHalfWidth=0,m_eraseHalfHeight=0;
    std::size_t m_timeline=0;
    float m_clock=0,m_cameraClock=0,m_chargeClock=0,m_cameraDistance=56,m_cameraAngle=-2;
    // 0x430AB0 keeps the player position sampled on the previous camera tick at
    // camera+733..735; the free follow turns toward that real displacement.
    float m_previousPlayerX=0,m_previousPlayerY=0;
    // Retail zeroes the player speed scale for the shutter result sequence
    // (0x42F190 multiplies by player+10776; the memory trace shows the player
    // pinned from the shutter until the frame-35 leaf event).
    int m_shutterHold=0;
    float m_fast=5,m_slow=2.3f,m_fastDiagonal=2.8284271f,m_slowDiagonal=1.4142135f;
    float m_hit=1,m_graze=1.4f;
    int m_chargeHeld=0,m_shiftReleased=0,m_previousHorizontal=0,m_mission=0;
    uint16_t m_seed=1;
    unsigned m_nextId=1;
    std::array<float,4> m_globals{};
    std::array<int,4> m_globalIntegers{};
    std::vector<vm::Diagnostic> m_diagnostics;
    std::map<vm::Fault,unsigned> m_diagnosticCounts;
    bool m_charging=false,m_shootWasHeld=false,m_targetInFrame=false;
};
}
