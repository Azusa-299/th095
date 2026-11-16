#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>

#include <windows.h>
#include <shellapi.h>

#include "archive.hpp"
#include "log.hpp"
#include "platform/renderer.hpp"
#include "platform/input.hpp"
#include "platform/window.hpp"
#include "scenes/startup.hpp"

namespace {

constexpr int kScreenWidth = 640;
constexpr int kScreenHeight = 480;
constexpr double kFrameStep = 1.0 / 60.0;
constexpr const wchar_t *kDefaultDat = L"th095.dat";

std::vector<std::wstring> commandLineArgs()
{
    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return args;
    }
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}

std::filesystem::path exeDirectory()
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer).parent_path();
}

// th095.dat is looked up next to the executable first, so dropping th95.exe
// into the original game folder works without any command line.
std::filesystem::path resolveDatPath(const std::wstring &requested)
{
    const std::filesystem::path candidate(requested);
    if (candidate.is_absolute()) {
        return candidate;
    }

    std::error_code ec;
    const std::filesystem::path besideExe = exeDirectory() / candidate;
    if (std::filesystem::exists(besideExe, ec)) {
        return besideExe;
    }
    const std::filesystem::path inCwd = std::filesystem::current_path(ec) / candidate;
    if (!ec && std::filesystem::exists(inCwd, ec)) {
        return inCwd;
    }
    return candidate;
}

void usage()
{
    std::printf("th95 port (stage 1)\n");
    std::printf("  th95.exe                        run the game window\n");
    std::printf("  th95.exe --list  [dat]          list archive entries\n");
    std::printf("  th95.exe --verify [dat]         verify decoded entry sizes\n");
    std::printf("  th95.exe --extract <dir> [dat]  dump archive to <dir>\n");
    std::printf("  th95.exe --smoke-test <png> [dat]  capture title at frames 120/600\n");
    std::printf("  th95.exe --startup-test <dir> [dat] [--slow-loading]  capture boot checkpoints\n");
    std::printf("    default: no delay; --slow-loading explicitly simulates 90 loading ticks\n");
    std::printf("  th95.exe --help-test <dir> [dat]  verify and capture the nine-page Help flow\n");
    std::printf("  th95.exe --mission-test <dir> [dat]  capture Mission Select navigation and returns\n");
    std::printf("  th95.exe --music-test <dir> [dat]    capture Music Room tracks, comments and returns\n");
    std::printf("  th95.exe --option-test <dir> [dat] [--config <cfg>]  capture Option rows and config edits\n");
    std::printf("  th95.exe --audio-test [dat]      verify all effect buffers and independent stops\n");
    std::printf("  th95.exe --level-test <dir> [dat]  capture six LV1 attacks, hits and final-photo results\n");
    std::printf("  th95.exe --frame-test <dir> [dat]  measure 600 real-time LV1 frames with audio\n");
    std::printf("    --score <file> uses a read-only score sample and captures every visible group\n");
    std::printf("  default dat: th095.dat\n");
}

int cmdList(const std::filesystem::path &datPath)
{
    th95::Archive archive;
    if (!archive.open(datPath)) {
        return 1;
    }

    std::printf("%-22s %10s %10s %10s\n", "name", "offset", "raw_size", "comp_size");
    unsigned long long declared = 0;
    for (const th95::Entry &entry : archive.entries()) {
        std::printf("%-22s %10u %10u %10u\n", entry.name.c_str(), entry.offset, entry.size,
                    entry.compSize);
        declared += entry.size;
    }
    std::printf("-- %zu entries, %llu bytes declared\n", archive.entries().size(), declared);
    return 0;
}

int cmdVerify(const std::filesystem::path &datPath)
{
    th95::Archive archive;
    if (!archive.open(datPath)) {
        return 1;
    }

    std::size_t exact = 0;
    std::size_t mismatch = 0;
    for (const th95::Entry &entry : archive.entries()) {
        const std::vector<uint8_t> data = archive.extract(entry);
        if (data.size() == entry.size) {
            ++exact;
            continue;
        }

        ++mismatch;
        std::printf("mismatch: %-22s expected=%u actual=%zu\n", entry.name.c_str(), entry.size,
                    data.size());
    }

    std::printf("-- verified: exact=%zu mismatch=%zu total=%zu\n", exact, mismatch,
                archive.entries().size());
    return mismatch == 0 ? 0 : 2;
}

int cmdExtract(const std::filesystem::path &datPath, const std::filesystem::path &outDir)
{
    th95::Archive archive;
    if (!archive.open(datPath)) {
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    int exact = 0;
    int shortCount = 0;
    int failed = 0;

    for (const th95::Entry &entry : archive.entries()) {
        if (entry.name.empty() || entry.size == 0) {
            continue;
        }
        if (entry.name.find("..") != std::string::npos) {
            continue;
        }

        std::vector<uint8_t> data = archive.extract(entry);
        if (data.empty()) {
            ++failed;
            continue;
        }

        const std::filesystem::path target = outDir / entry.name;
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path(), ec);
        }
        std::FILE *fp = _wfopen(target.c_str(), L"wb");
        if (!fp) {
            ++failed;
            continue;
        }
        std::fwrite(data.data(), 1, data.size(), fp);
        std::fclose(fp);

        if (data.size() == entry.size) {
            ++exact;
        } else {
            ++shortCount;
        }
    }

    std::printf("-- extracted: exact=%d short=%d failed=%d\n", exact, shortCount, failed);
    return 0;
}

int levelTest(const std::filesystem::path&datPath,const std::filesystem::path&directory){
    std::filesystem::create_directories(directory);th95::log::open((directory/"level-test.log").string().c_str());
    th95::Archive archive;if(!archive.open(datPath))return 1;
    th95::platform::Window window;if(!window.create("th95 LV1 verification",640,480))return 1;
    th95::platform::Renderer renderer;if(!renderer.init(window.handle(),640,480)||!th95::platform::gfxInit())return 1;
    bool passed=true;
    for(int mission=0;mission<6;++mission){
        th95::scene::LevelScene level;if(!level.load(archive,renderer.device(),mission))return 1;
        auto&sim=level.simulation();sim.invulnerable=true;
        const auto capture=[&](const char*label){
            if(!renderer.beginFrame()){passed=false;return;}level.render(renderer.device());renderer.endFrame();
            const auto name=std::string("lv1-")+std::to_string(mission+1)+"-"+label+".png";
            passed=renderer.capture(directory/name)&&passed;renderer.present();
        };
        bool warningCaptured=false;
        for(int t=0;t<1000;++t){
            window.poll();level.update({});
            if(!warningCaptured&&!sim.enemies().empty()&&sim.enemies().front().warning>10){capture("attack-circle");warningCaptured=true;}
            if(t==60)capture("spawn-circle");
            if(t==131)capture("entry");
            // probe frames let a retail frame with a known countdown be
            // compared 1:1; enabled only while diagnosing.
            if(const char*probe=std::getenv("TH95_PROBE")){
                std::istringstream values(probe);std::string item;
                while(std::getline(values,item,',')){
                    if(item.empty())continue;
                    const int frame=std::atoi(item.c_str());
                    if(frame==t){char label[32];std::snprintf(label,sizeof(label),"probe-%d",frame);capture(label);
                        int hist[24]={};for(const auto&b:sim.bullets())if(b.type>=0&&b.type<24)++hist[b.type];
                        std::string desc;for(int i=0;i<24;++i)if(hist[i])desc+=" t"+std::to_string(i)+"x"+std::to_string(hist[i]);
                        th95::log::print("info : probe %d bullets=%zu%s\r\n",frame,sim.bullets().size(),desc.c_str());}
                }
            }
            if(t==399)capture("attack");
            if(t==899)capture("attack-late");
            if(sim.fault){passed=false;break;}
        }
        th95::platform::InputState shift;shift.shiftHeld=true;
        for(int t=0;t<24;++t)level.update(shift);
        capture("shift-hitpoint");
        sim.charge=.5f;shift.shootHeld=true;
        for(int t=0;t<20;++t)level.update(shift);
        capture("shift-charge");
        if(!level.hitpoint().alive()||!sim.charging()){th95::log::print("error : LV1-%d shift-charge check hitpoint=%d charging=%d\r\n",mission+1,level.hitpoint().alive()?1:0,sim.charging()?1:0);passed=false;}
        for(int t=0;t<12;++t)level.update({});
        capture("shift-released");
        if(level.hitpoint().alive()||sim.charging()){th95::log::print("error : LV1-%d shift-release check hitpoint=%d charging=%d\r\n",mission+1,level.hitpoint().alive()?1:0,sim.charging()?1:0);passed=false;}
        sim.charge=1;level.update({});capture("ready-reticle");
        sim.charge=1;th95::platform::InputState aim;aim.shoot=aim.shootHeld=true;level.update(aim);aim.shoot=false;aim.upHeld=true;
        capture("aim-start");
        for(int t=0;t<12;++t)level.update(aim);
        capture("aim");
        for(int t=0;t<22;++t)level.update(aim);
        capture("aim-late");
        for(int t=0;t<30&&sim.cameraState==1;++t)level.update(aim);
        capture("aim-exhausted");
        if(sim.cameraState!=2||level.aimAnimations()[4].alive()){th95::log::print("error : LV1-%d aim-exhaust check cameraState=%d anim4=%d\r\n",mission+1,sim.cameraState,level.aimAnimations()[4].alive()?1:0);passed=false;}
        for(int t=0;t<85;++t)level.update({});
        sim.charge=1;aim={};aim.shoot=aim.shootHeld=true;level.update(aim);
        for(int t=0;t<12;++t)level.update(aim);
        level.update({});capture("shutter");
        for(int t=0;t<20;++t)level.update({});
        capture("miss-result");
        for(int t=0;t<80;++t)level.update({});
        for(int wait=0;wait<200;++wait){
            bool visible=false;
            for(const auto&e:sim.enemies())if(e.alive&&e.boss&&!e.hidden&&!e.spawnCircle){visible=true;break;}
            if(visible)break;
            level.update({});
        }
        const int photosBefore=sim.photos;
        for(int shot=0;shot<2;++shot){
            for(int wait=0;wait<200;++wait){
                bool visible=false;
                for(const auto&e:sim.enemies())if(e.alive&&e.boss&&!e.hidden&&!e.spawnCircle){visible=true;break;}
                if(visible)break;
                level.update({});
            }
            sim.cameraState=1;sim.charge=1;sim.photoWidth=256;sim.photoHeight=192;
            const th95::scene::LevelSimulation::Enemy*photoTarget=nullptr;
            for(const auto&e:sim.enemies())if(e.alive&&e.boss&&!e.hidden&&!e.spawnCircle){photoTarget=&e;break;}
            if(!photoTarget)for(const auto&e:sim.enemies())if(e.alive&&e.boss){photoTarget=&e;break;}
            if(!photoTarget)photoTarget=&sim.enemies().front();
            sim.cameraX=photoTarget->x;sim.cameraY=photoTarget->y;
            aim={};aim.shootHeld=true;level.update(aim);
            capture(shot==0?"boss-aim":"boss-aim-second");
            for(int t=0;t<8;++t)level.update(aim);
            capture(shot==0?"boss-aim-locked":"boss-aim-locked-second");
            level.update({});capture(shot==0?"boss-shutter":"boss-shutter-second");
            for(int t=0;t<12;++t)level.update({});
            capture(shot==0?"boss-photo-flight":"boss-photo-flight-second");
            for(int t=0;t<43;++t)level.update({});
            capture(shot==0?"boss-result":"boss-result-second");
            if(!sim.photoSuccess||sim.photos!=photosBefore+shot+1){th95::log::print("error : LV1-%d boss-photo check success=%d photos=%d shot=%d\r\n",mission+1,sim.photoSuccess?1:0,sim.photos,shot);passed=false;}
            for(int t=0;t<30;++t)level.update({});
        }
        th95::platform::InputState pause;pause.pause=true;level.update(pause);for(int t=0;t<15;++t)level.update({});capture("pause");
        level.update(pause);for(int t=0;t<2;++t)level.update({});
        // Finish through actual successful photos and the native interrupt
        // sequence; forcing cleared used to conceal the missing boss burst.
        while(sim.photos<sim.requiredPhotos&&!sim.fault){
            if(sim.enemies().empty()){passed=false;break;}
            const th95::scene::LevelSimulation::Enemy*photoTarget=nullptr;
            for(int wait=0;wait<200;++wait){
                for(const auto&e:sim.enemies())if(e.alive&&e.boss&&!e.hidden&&!e.spawnCircle){photoTarget=&e;break;}
                if(photoTarget)break;
                level.update({});
            }
            if(!photoTarget)for(const auto&e:sim.enemies())if(e.alive&&e.boss){photoTarget=&e;break;}
            if(!photoTarget)photoTarget=&sim.enemies().front();
            sim.cameraState=1;sim.charge=1;sim.photoWidth=256;sim.photoHeight=192;
            sim.cameraX=photoTarget->x;sim.cameraY=photoTarget->y;
            aim={};aim.shootHeld=true;level.update(aim);capture("final-aim");
            level.update({});capture("final-shutter");
            for(int t=0;t<160;++t){
                level.update({});
                if(renderer.beginFrame()){level.render(renderer.device());renderer.endFrame();renderer.present();}else passed=false;
                if(t==23)capture("final-exit-start");
                if(t==39)capture("final-exit-burst");
                if(t==79)capture("final-exit-late");
            }
            if(!sim.photoSuccess){th95::log::print("error : LV1-%d final-photo detail boss=%d hidden=%d circle=%d photos=%d/%d\r\n",mission+1,sim.enemies().empty()?0:(sim.enemies().front().boss?1:0),sim.enemies().empty()?0:(sim.enemies().front().hidden?1:0),sim.enemies().empty()?0:(sim.enemies().front().spawnCircle?1:0),sim.photos,sim.requiredPhotos);passed=false;break;}
        }
        for(int t=0;t<40;++t)level.update({});
        capture("success-result");
        passed=sim.outcomeReady()&&sim.enemies().empty()&&sim.bullets().empty()&&passed;
        if(!sim.outcomeReady()||!sim.enemies().empty()||!sim.bullets().empty())th95::log::print("error : LV1-%d success check outcome=%d enemies=%zu bullets=%zu photos=%d/%d\r\n",mission+1,sim.outcomeReady()?1:0,sim.enemies().size(),sim.bullets().size(),sim.photos,sim.requiredPhotos);
        th95::scene::LevelScene failedLevel;
        if(!failedLevel.load(archive,renderer.device(),mission))return 1;
        const auto failedCapture=[&](const char*label){
            if(!renderer.beginFrame()){passed=false;return;}failedLevel.render(renderer.device());renderer.endFrame();
            const auto name=std::string("lv1-")+std::to_string(mission+1)+"-"+label+".png";
            passed=renderer.capture(directory/name)&&passed;renderer.present();
        };
        auto&failedSim=failedLevel.simulation();failedSim.invulnerable=true;for(int t=0;t<400;++t)failedLevel.update({});
        failedSim.invulnerable=false;
        const auto collisionBullet=std::find_if(failedSim.bullets().begin(),failedSim.bullets().end(),[](const auto&b){return b.alive&&b.radius>0&&b.appearance<=0&&b.grace<=0&&b.y>180&&b.y<420&&std::abs(b.x)<175;});
        if(collisionBullet!=failedSim.bullets().end()){
            failedSim.playerX=collisionBullet->x+collisionBullet->vx*failedSim.timeScale;
            failedSim.playerY=collisionBullet->y+collisionBullet->vy*failedSim.timeScale;
        }else{failedSim.playerX=failedSim.enemies().front().x;failedSim.playerY=failedSim.enemies().front().y;}
        failedLevel.update({});failedCapture("death-start");
        passed=failedSim.dead&&failedLevel.deathParticles().size()==32&&passed;
        if(!failedSim.dead||failedLevel.deathParticles().size()!=32)th95::log::print("error : LV1-%d death check dead=%d particles=%zu\r\n",mission+1,failedSim.dead?1:0,failedLevel.deathParticles().size());
        for(int t=0;t<100;++t){
            failedLevel.update({});
            if(renderer.beginFrame()){failedLevel.render(renderer.device());renderer.endFrame();renderer.present();}else passed=false;
            if(t==11)failedCapture("death-motion");
            if(t==31)failedCapture("death-late");
        }
        failedCapture("failed-result");passed=failedSim.paused&&passed;
        if(!failedSim.paused)th95::log::print("error : LV1-%d failed-result check paused=0\r\n",mission+1);
        th95::log::print("%s : LV1-%d emitted=%u executed=%u bullets=%zu\r\n",sim.fault?"error":"info",mission+1,sim.emitted,sim.executed,sim.bullets().size());
    }
    th95::log::close();return passed?0:1;
}

int frameTest(const std::filesystem::path&datPath,const std::filesystem::path&directory){
    std::filesystem::create_directories(directory);th95::log::open((directory/"frame-test.log").string().c_str());
    th95::platform::timeInit();th95::platform::FramePacer pacer;
    th95::Archive archive;if(!archive.open(datPath))return 1;
    th95::platform::Window window;if(!window.create("th95 frame pacing verification",640,480))return 1;
    th95::platform::Renderer renderer;if(!renderer.init(window.handle(),640,480)||!th95::platform::gfxInit())return 1;
    th95::platform::Audio audio;if(!audio.load(archive,datPath.parent_path()/L"thbgm.dat","th095_01.wav")||!audio.start())return 1;
    th95::scene::LevelScene level;if(!level.load(archive,renderer.device(),0,&audio))return 1;
    level.simulation().invulnerable=true;
    double deadline=th95::platform::timeNow(),previous=0;
    std::vector<double> intervals,costs;bool passed=true;
    for(int frame=0;frame<600&&window.poll();++frame){
        pacer.waitUntil(deadline);const double start=th95::platform::timeNow();
        level.update({});audio.update();
        if(!renderer.beginFrame()){passed=false;break;}
        level.render(renderer.device());renderer.endFrame();renderer.present();
        const double finish=th95::platform::timeNow();
        if(frame>=60){intervals.push_back((finish-previous)*1000);costs.push_back((finish-start)*1000);}
        previous=finish;deadline+=kFrameStep;
        if(finish-deadline>3*kFrameStep)deadline=finish+kFrameStep;
    }
    if(intervals.empty())return 1;
    const auto report=[&](const char*name,std::vector<double> samples){
        std::sort(samples.begin(),samples.end());
        th95::log::print("info : %s samples=%zu mean=%.3f p95=%.3f p99=%.3f max=%.3f ms\r\n",name,samples.size(),
            std::accumulate(samples.begin(),samples.end(),0.0)/samples.size(),samples[samples.size()*95/100],samples[samples.size()*99/100],samples.back());
    };
    report("presentation interval",intervals);report("update/render/audio cost",costs);
    passed=passed&&intervals.size()==540&&!level.simulation().fault&&audio.hasPlaybackProgress();
    th95::log::print("%s : 600 real-time LV1 frames with BGM and volley effects\r\n",passed?"info":"error");
    th95::log::close();return passed?0:1;
}

int audioTest(const std::filesystem::path&datPath){
    th95::Archive archive;if(!archive.open(datPath))return 1;
    th95::platform::Audio audio;if(!audio.loadEffects(archive))return 1;
    bool passed=true;
    for(int id=0;id<47;++id){
        audio.playEffect(id,id%2?-128.0f:128.0f);
        Sleep(80);
        const bool progress=audio.effectHasPlaybackProgress(id);
        std::printf("%s sound %02d %s device progress\n",progress?"PASS":"FAIL",id,th95::platform::soundEffects[id].file);
        passed=progress&&passed;audio.stopEffect(id);
        passed=!audio.effectPlaying(id)&&passed;
    }
    audio.playEffect(42);audio.playEffect(44);audio.playEffect(7);
    Sleep(80);
    passed=audio.effectHasPlaybackProgress(42)&&audio.effectHasPlaybackProgress(44)&&passed;
    audio.stopEffect(44);audio.playEffect(41);
    passed=!audio.effectPlaying(44)&&audio.effectPlaying(42)&&audio.effectPlaying(41)&&passed;
    audio.stopEffects();
    for(int id=0;id<47;++id)passed=!audio.effectPlaying(id)&&passed;
    std::printf("%s concurrent charge/focus/shutter and stop cleanup\n",passed?"PASS":"FAIL");
    // Exercise the real scene-to-device path as well as isolated WAV buffers.
    th95::scene::LevelScene level;if(!level.load(archive,nullptr,0,&audio))return 1;
    auto&sim=level.simulation();sim.invulnerable=true;
    const auto expect=[&](bool ok,const char*label){if(!ok)th95::log::print("error : audio level check failed: %s\r\n",label);passed=ok&&passed;};
    for(int t=0;t<140;++t)level.update({});
    th95::platform::InputState charge;charge.shiftHeld=charge.shootHeld=true;
    for(int t=0;t<5;++t)level.update(charge);
    expect(audio.effectPlaying(42),"charge");
    th95::platform::InputState pause;pause.pause=true;level.update(pause);
    expect(!audio.effectPlaying(42)&&audio.effectPlaying(32),"pause");
    for(int t=0;t<15;++t)level.update({});
    level.update(pause);level.update(charge);expect(audio.effectPlaying(42),"resume");
    level.update({});expect(!audio.effectPlaying(42),"resume-stop");
    sim.charge=.9999f;level.update({});expect(audio.effectPlaying(43),"ready");
    sim.cameraX=0;sim.cameraY=128;
    th95::platform::InputState shoot;shoot.shoot=shoot.shootHeld=true;level.update(shoot);
    expect(audio.effectPlaying(44)&&audio.effectPlaying(46),"aim");
    level.update({});expect(!audio.effectPlaying(44)&&audio.effectPlaying(41)&&audio.effectPlaying(15),"shutter");
    for(int t=0;t<20;++t)level.update({});
    expect(sim.photoSuccess&&audio.effectPlaying(33),"success-result");
    for(int t=0;t<80;++t)level.update({});
    sim.charge=1;sim.cameraX=0;sim.cameraY=400;level.update(shoot);level.update({});
    for(int t=0;t<20;++t)level.update({});
    expect(!sim.photoSuccess&&audio.effectPlaying(37),"miss-result");
    sim.restart();sim.invulnerable=false;
    for(int t=0;t<140;++t)level.update({});
    const th95::scene::LevelSimulation::Bullet*collision=nullptr;
    for(const auto&b:sim.bullets())if(b.alive&&b.radius>0&&b.appearance<=0&&b.grace<=0&&b.y>180&&b.y<420&&std::abs(b.x)<175){collision=&b;break;}
    if(collision){sim.playerX=collision->x+collision->vx*sim.timeScale;sim.playerY=collision->y+collision->vy*sim.timeScale;}
    else{for(const auto&e:sim.enemies())if(e.alive&&e.collision){sim.playerX=e.x;sim.playerY=e.y;break;}}
    level.update({});
    if(!sim.dead){th95::log::print("error : audio collision detail bullets=%zu selected=%d enemies=%zu frames=%d invuln=%d state=%d\r\n",sim.bullets().size(),collision?1:0,sim.enemies().size(),sim.frames,sim.invulnerable?1:0,sim.cameraState);}
    expect(sim.dead&&audio.effectPlaying(4)&&!audio.effectPlaying(42)&&!audio.effectPlaying(44),"collision");
    std::printf("%s level charge/pause/resume/ready/aim/shutter/result/collision routing\n",passed?"PASS":"FAIL");
    // The previous fixture advanced aim in a few microseconds. Keep the
    // actual 60 Hz hold alive long enough to detect accidental stop/restart.
    if(!audio.load(archive,datPath.parent_path()/L"thbgm.dat","th095_01.wav")||!audio.start())return 1;
    th95::scene::LevelScene heldLevel;if(!heldLevel.load(archive,nullptr,0,&audio))return 1;
    auto&heldSim=heldLevel.simulation();heldSim.invulnerable=true;
    for(int t=0;t<140;++t)heldLevel.update({});
    heldSim.charge=1;shoot={};shoot.shoot=shoot.shootHeld=true;heldLevel.update(shoot);shoot.shoot=false;
    int starts=0,stops=0;
    const auto countFocus=[&](){for(const auto&e:heldSim.sounds())if(e.id==44){if(e.stop)++stops;else ++starts;}};
    countFocus();bool heldPlayback=true;
    for(int t=0;t<24;++t){
        Sleep(16);audio.update();heldLevel.update(shoot);countFocus();
        heldPlayback=heldPlayback&&audio.effectPlaying(44);
    }
    heldPlayback=heldPlayback&&audio.hasPlaybackProgress()&&audio.effectHasPlaybackProgress(44)&&starts==1&&stops==0;
    heldLevel.update({});countFocus();
    heldPlayback=heldPlayback&&!audio.effectPlaying(44)&&audio.effectPlaying(41)&&audio.effectPlaying(15)&&stops==1;
    std::printf("%s 60 Hz held Z focus sample continues with stage BGM until shutter (starts=%d stops=%d)\n",heldPlayback?"PASS":"FAIL",starts,stops);
    passed=passed&&heldPlayback;
    for(int t=0;t<85;++t)heldLevel.update({});
    heldSim.charge=1;heldLevel.update(shoot);
    bool exhaustedPlayback=audio.effectPlaying(44);
    for(int t=0;t<60&&heldSim.cameraState==1;++t){Sleep(16);audio.update();heldLevel.update(shoot);}
    exhaustedPlayback=exhaustedPlayback&&heldSim.cameraState==2&&!audio.effectPlaying(44)&&!audio.effectPlaying(41)&&!audio.effectPlaying(15);
    std::printf("%s 60 Hz held Z exhaustion stops focus without shutter\n",exhaustedPlayback?"PASS":"FAIL");
    passed=passed&&exhaustedPlayback;
    return passed?0:1;
}

enum class CaptureMode { None, Title, Startup, Help, Mission, Music, Option };
int runGame(const std::filesystem::path &datPath, const std::filesystem::path &capturePath = {},
            CaptureMode captureMode = CaptureMode::None,bool slowLoadFixture=false,
            const std::filesystem::path &scorePath={},
            const std::filesystem::path &configPath={})
{
    th95::log::open("log.txt");
    th95::log::print("th95 port -- startup\r\n");
    th95::platform::timeInit();

    th95::Archive archive;
    if (archive.open(datPath)) {
        th95::log::print("info : archive ready, %zu entries\r\n", archive.entries().size());
    } else {
        th95::log::print("warning : archive unavailable, running without resources\r\n");
    }

    th95::platform::Window window;
    if (!window.create("th95 port", kScreenWidth, kScreenHeight)) {
        th95::log::print("error : window creation failed\r\n");
        th95::log::close();
        return 1;
    }

    th95::platform::Renderer renderer;
    if (!renderer.init(window.handle(), kScreenWidth, kScreenHeight)) {
        th95::log::close();
        return 1;
    }

    if (!th95::platform::gfxInit()) {
        th95::log::print("error : GDI+ init failed\r\n");
        th95::log::close();
        return 1;
    }

    const bool optionTest=captureMode==CaptureMode::Option;
    std::filesystem::path optionCfg=configPath;
    if(optionCfg.empty())
        optionCfg=(optionTest?capturePath:exeDirectory())/L"th095.cfg";
    if(optionTest) {
        std::error_code ec;
        std::filesystem::create_directories(capturePath,ec);
        if(ec){th95::log::print("error : cannot create option capture directory\r\n");return 1;}
        // The retail sample lives in the read-only original game folder;
        // mode 7 reads and writes a copy inside the capture directory.
        // Always reseed it: a previous run leaves edited bytes behind, which
        // would make the fixed expected values below fail on a second run.
        const std::filesystem::path sample=std::filesystem::path("th95")/"th095.cfg";
        if(std::filesystem::exists(sample,ec))
            std::filesystem::copy_file(sample,optionCfg,
                std::filesystem::copy_options::overwrite_existing,ec);
    }
    if(!optionTest) {
        // retail routine resolves "th095.cfg" against the working directory,
        // i.e. the original game folder. Seed the port's writable copy from
        // the read-only retail sample when it does not exist yet.
        std::error_code ec;
        if(!std::filesystem::exists(optionCfg,ec)) {
            const std::filesystem::path sample=datPath.parent_path()/L"th095.cfg";
            if(sample!=optionCfg&&std::filesystem::exists(sample,ec))
                std::filesystem::copy_file(sample,optionCfg,ec);
        }
    }
    const bool startupTest=captureMode==CaptureMode::Startup;
    if(startupTest) {
        std::error_code ec;
        std::filesystem::create_directories(capturePath,ec);
        if(ec){th95::log::print("error : cannot create startup capture directory\r\n");return 1;}
    }
    th95::scene::StartupScene scene;
    if (!scene.load(archive, renderer.device(), datPath.parent_path() / L"thbgm.dat",
                    captureMode==CaptureMode::Title,startupTest&&slowLoadFixture?90:0,scorePath)) {
        th95::log::print("error : startup scene init failed\r\n");
        th95::log::close();
        return 1;
    }

    // retail routine loads the 200-byte config at boot and copies the two
    // volume bytes into the runtime audio globals (retail routine).
    scene.title().configure(optionCfg,datPath.parent_path()/L"thbgm.dat");
    // retail routine normalises the block on disk right after loading it. Writing
    // here also creates th095.cfg on the first run instead of waiting for the
    // shutdown write.
    scene.title().saveConfig();
    // retail routine picks the Win32 style/rect from cfg +0xAF before the first
    // frame; the port applies the same two styles to its existing window.
    // Retail mode 0 also asks retail routine for an exclusive 640x480@60 display
    // mode; on modern panels that hands aspect handling to the GPU scaler
    // (stretch depends on driver settings) and reverts the desktop
    // resolution on every Option toggle. The port therefore keeps mode 0 on
    // the borderless largest-4:3 window, which scales the fixed backbuffer
    // uniformly, and never switches the desktop display mode.
    const bool initialWindowed=scene.title().config().displayMode()!=0;
    window.setDisplayMode(initialWindowed);
    // retail routine asks for an exclusive 640x480@60 display mode for mode 0,
    // exactly like retail; the GPU scales it to the panel. Capture and
    // fixture runs must not switch the desktop display mode.
    const bool allowFullscreen=captureMode==CaptureMode::None;
    if(!initialWindowed&&allowFullscreen&&!renderer.setFullscreen(true))
        th95::log::print("warning : exclusive fullscreen refused; windowed backbuffer kept\r\n");
    th95::log::print("info : display mode cfg +0xAF=%u windowed=%u fullscreen=%u\r\n",
        unsigned(scene.title().config().displayMode()),unsigned(initialWindowed),
        unsigned(renderer.fullscreen()));

    th95::log::print("info : entering main loop\r\n");

    // Mirrors retail routine: fixed 60 Hz step accumulated against the wall clock.
    // Resource/audio initialization must not fast-forward the title animation.
    double accumulated = th95::platform::timeNow();
    unsigned frames = 0;
    bool captured = false, capturedLate = false;
    bool captureFailed=false,bootAudioSilent=true;
    bool restartApplied=false;
    th95::platform::Input input;
    using Phase=th95::scene::StartupScene::Phase;
    struct Checkpoint {const char *name;Phase phase;unsigned tick;bool saved=false;};
    std::array<Checkpoint,6> checkpoints{{
        {"splash.png",Phase::Presentation,0},
        {"loading-30.png",Phase::Loading,30},
        {"loading-60.png",Phase::Loading,60},
        {"title-enter.png",Phase::Title,0},
        {"title-fade.png",Phase::Title,15},
        {"title-ready.png",Phase::Title,120}
    }};
    if(startupTest&&!slowLoadFixture) {
        checkpoints[1]={"loading-first.png",Phase::Loading,0};
        checkpoints[2]={"loading-next.png",Phase::Loading,1};
    }
    const bool helpTest=captureMode==CaptureMode::Help;
    if(helpTest) {
        std::error_code ec;
        std::filesystem::create_directories(capturePath,ec);
        if(ec){th95::log::print("error : cannot create help capture directory\r\n");return 1;}
    }
    using HelpStage=th95::scene::HelpScene::Stage;
    using HelpInput=th95::scene::HelpInput;
    struct HelpCheckpoint {
        std::string name;
        int page;
        HelpStage stage;
        HelpInput input;
        unsigned wait;
    };
    const auto key=[](char key) {
        HelpInput input;
        input.up=key=='u';input.down=key=='d';input.left=key=='l';input.right=key=='r';
        input.confirm=key=='z';input.cancel=key=='x';return input;
    };
    std::vector<HelpCheckpoint> helpCheckpoints{
        {"help-list.png",0,HelpStage::List,key('d'),10},
        {"",1,HelpStage::List,key('d'),10},
        {"help-list-selected.png",2,HelpStage::List,key('z'),40},
        {"help-page-02.png",2,HelpStage::Page,key('r'),40},
        {"help-page-03.png",3,HelpStage::Page,key('x'),30},
        {"help-list-back.png",3,HelpStage::List,key('z'),40},
        {"help-page-reopened.png",3,HelpStage::Page,key('l'),40},
        {"",2,HelpStage::Page,key('l'),40},
        {"help-page-01.png",1,HelpStage::Page,key('l'),40},
        {"help-page-00.png",0,HelpStage::Page,key('l'),30},
        {"help-page-first-boundary.png",0,HelpStage::Page,key('r'),40}
    };
    for(int page=1;page<9;++page) {
        char name[32];std::snprintf(name,sizeof(name),"help-all-page-%02d.png",page);
        helpCheckpoints.push_back({name,page,HelpStage::Page,key('r'),page==8?30u:40u});
    }
    helpCheckpoints.push_back({"help-page-last-boundary.png",8,HelpStage::Page,key('z'),30});
    helpCheckpoints.push_back({"help-list-last.png",8,HelpStage::List,key('x'),70});
    helpCheckpoints.push_back({"help-closed.png",8,HelpStage::Closed,{},0});
    std::size_t helpStep=0;
    unsigned helpWait=0;
    bool helpOpened=false,helpDone=false,helpCaptureFailed=false;
    const auto driveHelp=[&](unsigned tick) {
        if(helpDone||tick<helpWait)return;
        th95::scene::TitleScene &title=scene.title();
        if(!helpOpened) {
            if(tick<120)return;
            if(!title.openHelp()) {
                th95::log::print("error : help task did not start\r\n");
                helpCaptureFailed=true;helpDone=true;return;
            }
            helpOpened=true;helpWait=tick+70;return;
        }
        const auto &step=helpCheckpoints[helpStep];
        if(title.helpFailed()||title.helpPage()!=step.page||title.helpStage()!=step.stage) {
            // A page is asynchronous: allow bounded IO latency, but never
            // count a PNG as success when the scene is in the wrong state.
            if(!title.helpFailed()&&title.helpStage()==HelpStage::Loading&&tick<helpWait+120)return;
            th95::log::print("error : help checkpoint %zu expected page=%d state=%d; got page=%d state=%d\r\n",
                helpStep,step.page,int(step.stage),title.helpPage(),int(title.helpStage()));
            helpCaptureFailed=true;helpDone=true;return;
        }
        if(!step.name.empty()) {
            if(!renderer.capture(capturePath/step.name))helpCaptureFailed=true;
            th95::log::print("info : help checkpoint %s page=%d state=%d step=%zu\r\n",
                step.name.c_str(),title.helpPage(),int(title.helpStage()),helpStep);
        }
        title.updateHelp(step.input);
        helpWait=tick+step.wait;
        helpDone=++helpStep==helpCheckpoints.size();
    };
    const bool missionTest=captureMode==CaptureMode::Mission;
    if(missionTest) {
        std::error_code ec;
        std::filesystem::create_directories(capturePath,ec);
        if(ec){th95::log::print("error : cannot create mission capture directory\r\n");return 1;}
    }
    struct MissionCheckpoint {
        std::string name;
        Phase phase;
        int group,index;
        th95::platform::InputState input;
        unsigned wait;
    };
    const auto missionKey=[](char key) {
        th95::platform::InputState input;
        input.down=key=='d';input.right=key=='r';input.confirm=key=='z';
        input.cancel=key=='x';input.shiftHeld=key=='s';return input;
    };
    std::vector<MissionCheckpoint> missionCheckpoints{
        {"mission-enter.png",Phase::MissionSelect,0,0,{},14},
        {"mission-fade.png",Phase::MissionSelect,0,0,{},45},
        {"mission-ready.png",Phase::MissionSelect,0,0,missionKey('d'),24},
        {"mission-scene02.png",Phase::MissionSelect,0,1,missionKey('s'),12},
        {"mission-shift-hidden.png",Phase::MissionSelect,0,1,{},12},
        {"mission-shift-restored.png",Phase::MissionSelect,0,1,missionKey('r'),28},
        {"mission-locked02.png",Phase::MissionSelect,1,0,missionKey('r'),28},
        {"mission-locked03.png",Phase::MissionSelect,2,0,missionKey('r'),28},
        {"mission-wrapped.png",Phase::MissionSelect,0,1,missionKey('x'),70},
        {"mission-title-return.png",Phase::Title,0,1,missionKey('z'),60},
        {"mission-reopened.png",Phase::MissionSelect,0,1,missionKey('x'),70},
        {"mission-title-final.png",Phase::Title,0,1,{},0}
    };
    unsigned logicalTicks=0,missionWait=0;
    std::size_t missionStep=0;
    bool missionOpened=false,missionDone=false,missionCaptureFailed=false,missionShift=false;
    const auto driveMission=[&]() {
        if(missionDone||logicalTicks<missionWait)return;
        if(!missionOpened) {
            if(scene.phase()!=Phase::Title||scene.titleFrames()<120)return;
            scene.update(kFrameStep,missionKey('z'));++logicalTicks;
            if(!scorePath.empty()&&scene.phase()==Phase::MissionSelect) {
                missionCheckpoints.clear();
                const auto &mission=scene.missionSelect();
                const int initialGroup=mission.group(),initialIndex=mission.missionIndex();
                missionCheckpoints.push_back({"profile-enter.png",Phase::MissionSelect,initialGroup,initialIndex,{},160});
                if(initialIndex>0) {
                    th95::platform::InputState upKey;upKey.up=true;
                    missionCheckpoints.push_back({"profile-scene-saved.png",Phase::MissionSelect,initialGroup,initialIndex,upKey,90});
                    missionCheckpoints.push_back({"profile-scene-previous.png",Phase::MissionSelect,initialGroup,initialIndex-1,missionKey('d'),90});
                }
                for(int i=0;i<mission.visibleGroups();++i) {
                    const int group=(initialGroup+i)%mission.visibleGroups();
                    char name[40];std::snprintf(name,sizeof(name),"profile-group-%02d.png",group);
                    missionCheckpoints.push_back({name,Phase::MissionSelect,group,i==0?initialIndex:0,missionKey('r'),90});
                }
                th95::platform::InputState rateKey;rateKey.showRate=true;
                missionCheckpoints.push_back({"profile-rate-off.png",Phase::MissionSelect,initialGroup,initialIndex,rateKey,24});
                missionCheckpoints.push_back({"profile-rate-on.png",Phase::MissionSelect,initialGroup,initialIndex,{},24});
                th95::platform::InputState lockKey;lockKey.lockBest=true;
                missionCheckpoints.push_back({"profile-lock-off.png",Phase::MissionSelect,initialGroup,initialIndex,lockKey,24});
                missionCheckpoints.push_back({"profile-lock-on.png",Phase::MissionSelect,initialGroup,initialIndex,{},24});
                missionCheckpoints.push_back({"profile-wrap.png",Phase::MissionSelect,initialGroup,initialIndex,missionKey('x'),70});
                missionCheckpoints.push_back({"profile-title-return.png",Phase::Title,initialGroup,initialIndex,{},0});
            }
            missionOpened=true;missionWait=logicalTicks+1;return;
        }
        const auto &step=missionCheckpoints[missionStep];
        const auto &mission=scene.missionSelect();
        if(scene.phase()!=step.phase||mission.group()!=step.group||mission.missionIndex()!=step.index) {
            th95::log::print("error : mission checkpoint %s expected phase=%d group=%d scene=%d; got %d/%d/%d\r\n",
                step.name.c_str(),int(step.phase),step.group,step.index,int(scene.phase()),mission.group(),mission.missionIndex());
            missionDone=true;missionCaptureFailed=true;return;
        }
        if(!renderer.capture(capturePath/step.name))missionCaptureFailed=true;
        th95::log::print("info : mission checkpoint %s phase=%d group=%d scene=%d tick=%u\r\n",
            step.name.c_str(),int(step.phase),mission.group(),mission.missionIndex(),logicalTicks);
        scene.update(kFrameStep,step.input);++logicalTicks;
        missionShift=step.input.shiftHeld;
        missionWait=logicalTicks+step.wait;
        missionDone=++missionStep==missionCheckpoints.size();
    };
    const bool musicTest=captureMode==CaptureMode::Music;
    if(musicTest) {
        std::error_code ec;
        std::filesystem::create_directories(capturePath,ec);
        if(ec){th95::log::print("error : cannot create music capture directory\r\n");return 1;}
    }
    using MusicStage=th95::scene::MusicRoomScene::Stage;
    using MusicInput=th95::scene::MusicRoomInput;
    struct MusicCheckpoint {
        std::string name;
        MusicStage stage;
        int selected;
        const char *track;
        MusicInput input;
        unsigned wait;
    };
    const auto musicKey=[](char key) {
        MusicInput input;
        input.up=key=='u';input.down=key=='d';
        input.confirm=key=='z';input.cancel=key=='x';
        return input;
    };
    // Retail timing is one state tick per presented title frame: 30 ticks of
    // list intro, then eight comment rows at ticks 10/12/../24.
    std::vector<MusicCheckpoint> musicCheckpoints{
        {"music-intro.png",MusicStage::Intro,0,nullptr,{},32},
        {"music-list.png",MusicStage::List,0,nullptr,{},24},
        {"music-comments.png",MusicStage::List,0,nullptr,musicKey('d'),3},
        {"music-selected-1.png",MusicStage::List,1,nullptr,musicKey('d'),3},
        {"music-selected-2.png",MusicStage::List,2,nullptr,musicKey('z'),3},
        {"music-playing.png",MusicStage::List,2,"bgm/th095_02.wav",musicKey('u'),20},
        {"music-up-1.png",MusicStage::List,1,nullptr,musicKey('u'),3},
        {"music-up-0.png",MusicStage::List,0,nullptr,musicKey('u'),3},
        {"music-wrap.png",MusicStage::List,5,nullptr,musicKey('d'),3},
        {"music-wrap-back.png",MusicStage::List,0,nullptr,musicKey('x'),3},
        {"music-closing.png",MusicStage::Closing,0,nullptr,{},65},
        {"",MusicStage::Closed,0,nullptr,{},0}
    };
    std::size_t musicStep=0;
    unsigned musicWait=0;
    bool musicOpened=false,musicDone=false,musicCaptureFailed=false;
    const auto driveMusic=[&](unsigned tick) {
        if(musicDone||tick<musicWait)return;
        th95::scene::TitleScene &title=scene.title();
        if(!musicOpened) {
            if(tick<120)return;
            if(!title.openMusicRoom()) {
                th95::log::print("error : music room did not start\r\n");
                musicCaptureFailed=true;musicDone=true;return;
            }
            // Let the mode-8 intro play for a few ticks before capturing it.
            musicOpened=true;musicWait=tick+8;return;
        }
        const auto &step=musicCheckpoints[musicStep];
        if(title.musicRoomStage()!=step.stage||title.musicRoomSelected()!=step.selected||
           (step.track&&title.musicRoomTrack()!=step.track)) {
            th95::log::print("error : music checkpoint %s expected stage=%d selected=%d; got stage=%d selected=%d track=%s\r\n",
                step.name.c_str(),int(step.stage),step.selected,int(title.musicRoomStage()),
                title.musicRoomSelected(),title.musicRoomTrack().c_str());
            musicCaptureFailed=true;musicDone=true;return;
        }
        if(!step.name.empty()) {
            if(!renderer.capture(capturePath/step.name))musicCaptureFailed=true;
            th95::log::print("info : music checkpoint %s stage=%d selected=%d track=%s tick=%u\r\n",
                step.name.c_str(),int(step.stage),step.selected,title.musicRoomTrack().c_str(),tick);
        }
        title.updateMusicRoom(step.input);
        musicWait=tick+step.wait;
        musicDone=++musicStep==musicCheckpoints.size();
    };
    using OptionStage=th95::scene::OptionScene::Stage;
    using OptionInputT=th95::scene::OptionInput;
    const auto optionKey=[](char key,int keyIndex=OptionInputT::kNoKey) {
        OptionInputT input;
        input.up=key=='u';input.down=key=='d';
        input.left=key=='l';input.right=key=='r';
        input.confirm=key=='z';input.cancel=key=='x';
        input.keyIndex=keyIndex;
        return input;
    };
    struct OptionCheckpoint {
        const char *name;
        OptionStage stage;
        int selected;
        OptionInputT input;
        unsigned wait;
    };
    std::vector<OptionCheckpoint> optionCheckpoints{
        {"option-intro.png",OptionStage::Intro,0,{},40},
        {"option-list.png",OptionStage::List,0,optionKey('d'),4},
        {"option-row-1.png",OptionStage::List,1,optionKey('d'),4},
        {"option-key.png",OptionStage::List,2,optionKey(0,17),4},
        {"option-row-3.png",OptionStage::List,2,optionKey('d'),4},
        {"option-display.png",OptionStage::List,3,optionKey('r'),4},
        {"option-row-4.png",OptionStage::List,3,optionKey('d'),4},
        {"option-bgm.png",OptionStage::List,4,optionKey('l'),4},
        {"option-row-5.png",OptionStage::List,4,optionKey('d'),4},
        {"option-se.png",OptionStage::List,5,optionKey('r'),4},
        {"option-row-6.png",OptionStage::List,5,optionKey('d'),4},
        {"option-confirm.png",OptionStage::List,6,optionKey('z'),4},
        {"option-closing.png",OptionStage::Closing,6,{},70},
        {"",OptionStage::Closed,0,{},0}
    };
    std::size_t optionStep=0;
    unsigned optionWait=0;
    bool optionOpened=false,optionDone=false,optionCaptureFailed=false;
    const auto driveOption=[&](unsigned tick) {
        if(optionDone||tick<optionWait)return;
        th95::scene::TitleScene &title=scene.title();
        if(!optionOpened) {
            if(tick<120)return;
            if(!title.openOption()) {
                th95::log::print("error : option mode 7 did not start\r\n");
                optionCaptureFailed=true;optionDone=true;return;
            }
            optionOpened=true;optionWait=tick+8;return;
        }
        const auto &step=optionCheckpoints[optionStep];
        if(title.optionStage()!=step.stage||title.optionSelected()!=step.selected) {
            th95::log::print("error : option checkpoint %s expected stage=%d selected=%d; got stage=%d selected=%d tick=%u\r\n",
                step.name,int(step.stage),step.selected,int(title.optionStage()),
                title.optionSelected(),tick);
            optionCaptureFailed=true;optionDone=true;return;
        }
        if(*step.name) {
            if(!renderer.capture(capturePath/step.name))optionCaptureFailed=true;
            th95::log::print("info : option checkpoint %s stage=%d selected=%d display=%u bgm=%u se=%u keys=%u/%u/%u tick=%u\r\n",
                step.name,int(step.stage),title.optionSelected(),
                unsigned(title.optionDisplayMode()),unsigned(title.optionBgmVolume()),
                unsigned(title.optionSeVolume()),unsigned(title.optionKeyBinding(0)),
                unsigned(title.optionKeyBinding(1)),unsigned(title.optionKeyBinding(2)),tick);
        }
        title.updateOption(step.input);
        optionWait=tick+step.wait;
        optionDone=++optionStep==optionCheckpoints.size();
    };
    double firstTitleSeconds=-1;
    unsigned skippedDraws=0;
    th95::platform::FramePacer pacer;
    unsigned discardedTicks=0;
    std::vector<double> frameIntervals;
    double previousPresentation=0;
    while (window.poll()) {
        pacer.waitUntil(accumulated);
        const double now = th95::platform::timeNow();
        if(now-accumulated>3*kFrameStep){
            discardedTicks+=unsigned((now-accumulated)/kFrameStep)-2;
            accumulated=now-2*kFrameStep;
        }
        input.poll();
        th95::platform::InputState frameInput = input.state();
        if(missionTest){frameInput={};frameInput.shiftHeld=missionShift;}
        if(musicTest||optionTest)frameInput={};
        while (accumulated < now) {
            scene.update(kFrameStep, frameInput);
            ++logicalTicks;
            // A wall-clock catch-up can run several logical ticks. Pressed
            // and repeat edges belong to the first tick only; held state is
            // retained for menu repeat timing.
            frameInput.up = frameInput.down = frameInput.left = frameInput.right = false;
            frameInput.confirm = frameInput.cancel = false;
            frameInput.shoot = frameInput.pause = false;
            frameInput.upRepeat = frameInput.downRepeat = false;
            accumulated += kFrameStep;
            if(scene.phase()==Phase::Failed)break;
        }
        if(scene.phase()==Phase::Failed)break;
        // retail routine returns 5 only when cfg +0xAF changed; retail routine
        // rebuilds the window and the device. Mode 0 uses an exclusive
        // 640x480 swap chain (retail routine Windowed=FALSE), so the D3D9
        // device is reset as well as the window.
        if(scene.title().restartRequested()) {
            const bool windowed=scene.title().config().displayMode()!=0;
            // Consume the request exactly once, like retail's return-5
            // handling. Leaving it latched made every later display-row
            // toggle inside a new Option session re-trigger the rebuild.
            scene.title().consumeRestartRequest();
            if(window.windowed()!=windowed) {
                if(windowed) {
                    // Restore the desktop before sizing the window.
                    renderer.setFullscreen(false);
                    window.setDisplayMode(true);
                } else {
                    window.setDisplayMode(false);
                    if(allowFullscreen&&!renderer.setFullscreen(true))
                        th95::log::print("warning : exclusive fullscreen refused; windowed backbuffer kept\r\n");
                }
                restartApplied=true;
                th95::log::print("info : option return 5 -> rebuilt display windowed=%u fullscreen=%u\r\n",
                    unsigned(windowed),unsigned(renderer.fullscreen()));
            }
        }
        if(scene.phase()==Phase::Title&&firstTitleSeconds<0) {
            firstTitleSeconds=th95::platform::timeNow();
            th95::log::print("info : first title ready after %.3f seconds, %u startup presentations\r\n",
                            firstTitleSeconds,frames);
        }
        if(scene.phase()!=Phase::Title&&scene.audioHasPlaybackProgress())bootAudioSilent=false;

        if (renderer.beginFrame()) {
            scene.render(renderer.device());
            renderer.endFrame();
            if(captureMode==CaptureMode::Title && frames==119)captured=renderer.capture(capturePath);
            if(captureMode==CaptureMode::Title && frames==599){
                const auto late=capturePath.parent_path()/(capturePath.stem().wstring()+L"-late.png");
                capturedLate=renderer.capture(late);
            }
            if(startupTest)for(auto &checkpoint:checkpoints) {
                const auto tick=scene.phase()==Phase::Loading?scene.loadingFrames():scene.titleFrames();
                if(!checkpoint.saved&&scene.phase()==checkpoint.phase&&tick>=checkpoint.tick) {
                    checkpoint.saved=renderer.capture(capturePath/checkpoint.name);
                    if(!checkpoint.saved)captureFailed=true;
                    th95::log::print("info : startup checkpoint %s loadingTick=%u titleTick=%u\r\n",
                                    checkpoint.name,scene.loadingFrames(),scene.titleFrames());
                }
            }
        } else {++skippedDraws;}
        renderer.present();
        const double presentation=th95::platform::timeNow();
        if(previousPresentation>0){frameIntervals.push_back((presentation-previousPresentation)*1000);scene.recordFrame(presentation-previousPresentation);}
        previousPresentation=presentation;
        scene.presented();
        if(helpTest&&scene.phase()==Phase::Title)driveHelp(scene.titleFrames());
        if(missionTest)driveMission();
        if(musicTest&&scene.phase()==Phase::Title)driveMusic(scene.titleFrames());
        if(optionTest&&scene.phase()==Phase::Title)driveOption(scene.titleFrames());

        if (scene.quitRequested()) {
            break;
        }
        if (++frames == 60) {
            th95::log::print("info : 60 frames presented\r\n");
        }
        if(captureMode==CaptureMode::Title && frames>=720)break;
        if(helpTest&&(helpDone||frames>=2400))break;
        if(missionTest&&(missionDone||frames>=2200))break;
        if(musicTest&&(musicDone||frames>=2400))break;
        if(optionTest&&(optionDone||frames>=2400))break;
        // Startup acceptance ends after the transition and two seconds of
        // settled title output. Long title/background checks remain in
        // --smoke-test and the 3600-tick fixtures.
        if(startupTest&&(scene.titleFrames()>=180||frames>=3000))break;
    }

    const bool audioProgress=scene.audioHasPlaybackProgress();
    if(!frameIntervals.empty()){
        std::sort(frameIntervals.begin(),frameIntervals.end());
        th95::log::print("info : frame intervals ms mean=%.3f p95=%.3f p99=%.3f max=%.3f discardedTicks=%u\r\n",
            std::accumulate(frameIntervals.begin(),frameIntervals.end(),0.0)/frameIntervals.size(),
            frameIntervals[frameIntervals.size()*95/100],frameIntervals[frameIntervals.size()*99/100],frameIntervals.back(),discardedTicks);
    }
    if(!capturePath.empty())th95::log::print("%s : smoke-test BGM device progress\r\n",audioProgress?"info":"error");
    th95::log::print("info : shutting down after %u frames titleTick=%u skippedDraws=%u quit=%u\r\n",
                    frames,scene.titleFrames(),skippedDraws,unsigned(scene.quitRequested()));
    bool passed=scene.phase()!=Phase::Failed;
    if(captureMode==CaptureMode::Title)passed=passed&&captured&&capturedLate&&audioProgress;
    if(startupTest) {
        for(const auto &checkpoint:checkpoints){
            passed=passed&&checkpoint.saved;
            if(!checkpoint.saved)th95::log::print("error : missing startup checkpoint %s\r\n",checkpoint.name);
        }
        passed=passed&&!captureFailed&&bootAudioSilent&&audioProgress;
        th95::log::print("%s : startup-test checkpoints, silent boot, title audio progress\r\n",passed?"info":"error");
    }
    if(helpTest) {
        passed=passed&&helpDone&&!helpCaptureFailed;
        th95::log::print("%s : help-test nine pages, boundaries, shared layers and returns\r\n",passed?"info":"error");
    }
    if(missionTest) {
        passed=passed&&missionDone&&!missionCaptureFailed;
        th95::log::print("%s : mission-test navigation, locks, Shift, wrap and title returns\r\n",passed?"info":"error");
    }
    if(musicTest) {
        passed=passed&&musicDone&&!musicCaptureFailed&&
            scene.title().musicRoomStage()==th95::scene::MusicRoomScene::Stage::Closed;
        th95::log::print("%s : music-test track list, comments, BGM, wrap and title return\r\n",passed?"info":"error");
    }
    // retail routine writes the 200-byte block when the retail main loop exits.
    const bool configSaved=scene.title().saveConfig();
    if(optionTest) {
        bool persisted=false;
        std::ifstream cfg(optionCfg,std::ios::binary|std::ios::ate);
        if(cfg&&cfg.tellg()==std::streamoff(200)) {
            cfg.seekg(0);
            uint8_t data[200]{};
            if(cfg.read(reinterpret_cast<char*>(data),200)) {
                const uint16_t key2=uint16_t(data[6])|uint16_t(uint16_t(data[7])<<8);
                // Sample starts at display=1, bgm=100, se=80, slot 2=2.
                // The checkpoint script toggles display, -5 BGM, +5 SE and
                // captures key 17 into slot 2.
                persisted=data[0xAF]==0&&data[0xB5]==95&&data[0xB6]==85&&key2==17;
            }
        }
        const bool displayApplied=restartApplied&&
            window.windowed()==(scene.title().config().displayMode()!=0);
        const bool optionPassed=optionDone&&!optionCaptureFailed&&configSaved&&persisted&&
            scene.title().optionStage()==OptionStage::Closed&&displayApplied;
        passed=passed&&optionPassed;
        th95::log::print("info : option cfg write saved=%u persisted=%u restartApplied=%u\r\n",
            unsigned(configSaved),unsigned(persisted),unsigned(restartApplied));
        th95::log::print("%s : option-test seven rows, key capture, display, volumes, restart and cfg write\r\n",
            optionPassed?"info":"error");
    }
    // retail routine saves the score container before the game object is released.
    // Fixture captures pass their own score path, so they never touch it.
    if(captureMode==CaptureMode::None)scene.saveOnExit();
    th95::platform::gfxShutdown();
    th95::log::close();
    return passed?0:1;
}

} // namespace

int main()
{
    const std::vector<std::wstring> args = commandLineArgs();
    std::filesystem::path datPath = kDefaultDat;

    if (args.size() >= 2 && (args[1] == L"-h" || args[1] == L"--help")) {
        usage();
        return 0;
    }
    if (args.size() >= 2 && args[1] == L"--list") {
        if (args.size() >= 3) {
            datPath = args[2];
        }
        return cmdList(resolveDatPath(datPath.wstring()));
    }
    if (args.size() >= 2 && args[1] == L"--verify") {
        if (args.size() >= 3) {
            datPath = args[2];
        }
        return cmdVerify(resolveDatPath(datPath.wstring()));
    }
    if (args.size() >= 3 && args[1] == L"--extract") {
        if (args.size() >= 4) {
            datPath = args[3];
        }
        return cmdExtract(resolveDatPath(datPath.wstring()), args[2]);
    }
    if (args.size() >= 3 && args[1] == L"--dat") {
        datPath = args[2];
    }
    if (args.size() >= 3 && args[1] == L"--smoke-test") {
        if(args.size()>=4)datPath=args[3];
        return runGame(resolveDatPath(datPath.wstring()),args[2],CaptureMode::Title);
    }
    if(args.size()>=3&&args[1]==L"--startup-test") {
        bool slowLoading=false;
        for(std::size_t i=3;i<args.size();++i) {
            if(args[i]==L"--slow-loading")slowLoading=true;
            else if(args[i]==L"--natural")slowLoading=false; // legacy no-delay spelling
            else datPath=args[i];
        }
        return runGame(resolveDatPath(datPath.wstring()),args[2],CaptureMode::Startup,slowLoading);
    }
    if(args.size()>=3&&args[1]==L"--help-test") {
        if(args.size()>=4)datPath=args[3];
        return runGame(resolveDatPath(datPath.wstring()),args[2],CaptureMode::Help);
    }
    if(args.size()>=3&&args[1]==L"--level-test") {
        if(args.size()>=4)datPath=args[3];
        return levelTest(resolveDatPath(datPath.wstring()),args[2]);
    }
    if(args.size()>=3&&args[1]==L"--frame-test") {
        if(args.size()>=4)datPath=args[3];
        return frameTest(resolveDatPath(datPath.wstring()),args[2]);
    }
    if(args.size()>=2&&args[1]==L"--audio-test") {
        if(args.size()>=3)datPath=args[2];
        return audioTest(resolveDatPath(datPath.wstring()));
    }
    if(args.size()>=3&&args[1]==L"--music-test") {
        if(args.size()>=4)datPath=args[3];
        return runGame(resolveDatPath(datPath.wstring()),args[2],CaptureMode::Music);
    }
    if(args.size()>=3&&args[1]==L"--option-test") {
        std::filesystem::path configPath;
        for(std::size_t i=3;i<args.size();++i) {
            if(args[i]==L"--config"&&i+1<args.size())configPath=args[++i];
            else datPath=args[i];
        }
        return runGame(resolveDatPath(datPath.wstring()),args[2],CaptureMode::Option,false,{},configPath);
    }
    if(args.size()>=3&&args[1]==L"--mission-test") {
        std::filesystem::path scorePath;
        for(std::size_t i=3;i<args.size();++i) {
            if(args[i]==L"--score"&&i+1<args.size())scorePath=args[++i];
            else datPath=args[i];
        }
        return runGame(resolveDatPath(datPath.wstring()),args[2],CaptureMode::Mission,false,scorePath);
    }
    return runGame(resolveDatPath(datPath.wstring()));
}
