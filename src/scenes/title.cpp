#include "title.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include "../log.hpp"

namespace th95::scene {
bool TitleScene::load(Archive &archive,IDirect3DDevice9 *device,const std::filesystem::path &bgmPath)
{
    m_bgmPath=bgmPath;
    Preparation result;
    do {result=prepareNext(archive,device,bgmPath);}while(result==Preparation::Loading);
    return result==Preparation::Ready&&start();
}
TitleScene::Preparation TitleScene::prepareNext(Archive &archive,IDirect3DDevice9 *device,const std::filesystem::path &bgmPath)
{
    if(m_prepareStage<0)return Preparation::Failed;
    // The normal boot path calls prepareNext(), not load(); sub-modes still
    // need the BGM path for Music Room track changes and the title restore.
    m_bgmPath=bgmPath;
    m_dataDirectory=bgmPath.parent_path();
    m_device=device;m_archive=&archive;
    bool ok=true;
    switch(m_prepareStage) {
    case 0:ok=m_title.load(archive,"title.anm",device);break;
    case 1:ok=m_version.load(archive,"title_v.anm",device);break;
    case 2:
        if(!m_audio.load(archive,bgmPath))log::print("warning : title BGM unavailable; see audio error above\r\n");
        break;
    default:return Preparation::Ready;
    }
    if(!ok){m_prepareStage=-1;return Preparation::Failed;}
    return ++m_prepareStage==3?Preparation::Ready:Preparation::Loading;
}
bool TitleScene::start()
{
    if(m_started)return true;
    if(m_prepareStage!=3)return false;
    // retail routine creates the two background halves, title art and scrolling
    // layers. retail routine creates three animation layers per menu item.
    const int scripts[5]={102,103,100,101,25};
    for(int i=0;i<5;++i)if(!m_background[i].start(m_title,scripts[i]))return false;
    format::AnmAnimation secondLogo;
    if(!secondLogo.start(m_title,26))return false;
    m_particles.push_back(std::move(secondLogo));
    format::AnmAnimation copyright;
    if(!copyright.start(m_title,27))return false;
    m_particles.push_back(std::move(copyright));
    if(!m_versionAnimation.start(m_version,0))return false;
    for(int i=0;i<18;++i)if(!m_menuAnimations[i].start(m_title,i,64.0f,130.0f+38.0f*(i%6)))return false;
    // Preparation is silent: music and title animations begin together only
    // after the splash is retired. A key held during boot is not a fresh press.
    if(!m_audio.start())log::print("warning : title BGM could not start\r\n");
    m_started=true;
    log::print("info : title ready: six vertical items x=64 y=130 step=38, retail scripts\r\n");
    return true;
}
void TitleScene::configure(const std::filesystem::path &cfgPath,
                           const std::filesystem::path &bgmPath)
{
    m_configPath=cfgPath;
    m_config.load(cfgPath,bgmPath);
    // retail routine copies +0xB5/+0xB6 into the runtime audio globals.
    m_audio.setVolumes(m_config.bgmVolume(),m_config.seVolume());
}
bool TitleScene::saveConfig()
{
    if(m_configPath.empty())return false;
    return m_config.save(m_configPath);
}
bool TitleScene::openHelp()
{
    if(!m_help.open(*m_archive,m_device,m_title,m_audio))return false;
    m_selected=4; // The fixture enters through the same Help menu item.
    // retail routine and Help state 0 operate on shared title animations.
    for(auto &a:m_menuAnimations)a.interrupt(1);
    m_background[0].interrupt(1);m_background[1].interrupt(1);
    m_background[4].interrupt(3); // logo 25
    for(auto &a:m_particles)if(a.spriteId()==19||a.spriteId()==20)a.interrupt(3);
    m_versionAnimation.interrupt(3);
    return true;
}
bool TitleScene::openMusicRoom()
{
    if(!m_musicRoom.open(*m_archive,m_device,m_bgmPath,m_title,m_audio))return false;
    m_selected=2;
    // retail routine state 0 retires the shared title layers. The menu widget
    // keeps its wrap flag and its selection history (retail address).
    for(auto &a:m_menuAnimations)a.interrupt(1);
    m_background[0].interrupt(1);m_background[1].interrupt(1);
    m_background[4].interrupt(3);
    for(auto &a:m_particles)if(a.spriteId()==19||a.spriteId()==20)a.interrupt(3);
    m_versionAnimation.interrupt(3);
    return true;
}
bool TitleScene::updateMusicRoom(const MusicRoomInput &input)
{
    const bool returned=m_musicRoom.update(input);
    if(returned)returnFromHelp();
    return returned;
}
bool TitleScene::openOption()
{
    if(!m_option.open(m_device,m_title,m_audio,m_config,m_configPath))return false;
    m_selected=3;
    // retail routine state 0 retires the same shared title layers as Help and
    // Music Room: interrupt 1 on the six menu rows/backgrounds, interrupt 3
    // on the logo, copyright and version animation.
    for(auto &a:m_menuAnimations)a.interrupt(1);
    m_background[0].interrupt(1);m_background[1].interrupt(1);
    m_background[4].interrupt(3);
    for(auto &a:m_particles)if(a.spriteId()==19||a.spriteId()==20)a.interrupt(3);
    m_versionAnimation.interrupt(3);
    return true;
}
bool TitleScene::updateOption(const OptionInput &input)
{
    const bool returned=m_option.update(input);
    if(returned) {
        m_restartRequested=m_option.restartRequested();
        returnFromHelp();
    }
    return returned;
}
bool TitleScene::openReplayBrowser()
{
    if(!m_asciiReady)m_asciiReady=m_ascii.load(*m_archive,"ascii.anm",m_device);
    m_replaySlots=format::scanReplays(m_dataDirectory);
    // Retail remembers the last confirmed entry across visits, then zeroes it.
    m_replayPage=std::clamp(m_replayRemember/20,0,3);
    m_replayCursor=std::clamp(m_replayRemember%20,0,19);
    m_replayRemember=0;
    m_replayTicks=0;
    m_replayPath.clear();
    const int scripts[4]={104,105,31,71};
    for(int i=0;i<4;++i)
        if(!m_replayBackground[std::size_t(i)].start(m_title,scripts[i]))return false;
    m_replayActive=true;
    m_selected=1;
    // Retail state 3 retires the shared title layers like the other sub-modes.
    for(auto &a:m_menuAnimations)a.interrupt(1);
    m_background[0].interrupt(1);m_background[1].interrupt(1);
    m_background[4].interrupt(3);
    for(auto &a:m_particles)if(a.spriteId()==19||a.spriteId()==20)a.interrupt(3);
    m_versionAnimation.interrupt(3);
    return true;
}
void TitleScene::updateReplayBrowser(const platform::InputState &input)
{
    // Retail ignores input for the first thirty frames after the page opens.
    if(m_replayTicks<30){++m_replayTicks;return;}
    ++m_replayTicks;
    const int row=(input.up||input.upRepeat)?-1:(input.down||input.downRepeat)?1:0;
    const int page=(input.left||input.leftRepeat)?-1:(input.right||input.rightRepeat)?1:0;
    if(row){m_replayCursor=(m_replayCursor+row+20)%20;m_audio.playEffect(12);}
    if(page){m_replayPage=(m_replayPage+page+4)%4;m_audio.playEffect(12);}
    const int index=m_replayPage*20+m_replayCursor;
    if(input.confirm){
        const auto &slot=m_replaySlots[std::size_t(index)];
        if(slot.exists){
            m_audio.playEffect(10);
            m_replayPath=slot.path;
            m_replayRemember=index;
            m_replayActive=false;
            // Retail state 3 -> 6 only fades the browser layers; it does not
            // rebuild the title mode-1 menu because the level takes over.
            for(auto &a:m_replayBackground)a.interrupt(1);
        }else{
            m_audio.playEffect(0x25);
        }
        return;
    }
    if(input.cancel||input.pause){
        m_audio.playEffect(11);
        m_replayActive=false;
        for(auto &a:m_replayBackground)a.interrupt(1);
        returnFromHelp();
    }
}
void TitleScene::drawReplayBrowser(IDirect3DDevice9 *device)
{
    if(!m_asciiReady)return;
    auto print=[&](const char *s,float x,float y,DWORD tint){
        for(std::size_t i=0;s[i];++i)
            m_ascii.draw(device,int(static_cast<unsigned char>(s[i]))-32,
                         x+9.0f*float(i),y,tint);
    };
    char text[96],level[8],scene[8];
    for(int n=0;n<20;++n){
        const int index=m_replayPage*20+n;
        const auto &slot=m_replaySlots[std::size_t(index)];
        const DWORD color=n==m_replayCursor?0xFFFFFFFFu:0xFF404040u;
        if(slot.exists){
            const auto &header=slot.header;
            if(header.level==10)std::strcpy(level,"EX");
            else std::snprintf(level,sizeof(level),"%2d",header.level+1);
            std::snprintf(scene,sizeof(scene),"%d",header.scene+1);
            const std::time_t stamp=header.timestamp;
            const std::tm *tm=std::localtime(&stamp);
            const int year=tm?(tm->tm_year+1900)%100:0,mon=tm?tm->tm_mon+1:0;
            const int day=tm?tm->tm_mday:0,hour=tm?tm->tm_hour:0,minute=tm?tm->tm_min:0;
            if(m_replayPage==0){
                std::snprintf(text,sizeof(text),
                    "No.%.2d %s %s-%s %.2d/%.2d/%.2d %.2d:%.2d %6d %2.0f%%",
                    index+1,header.name.c_str(),level,scene,year,mon,day,hour,minute,
                    int(header.score),double(header.rate));
            }else{
                // Retail prints the four characters after "th95_ud" in the file name.
                const std::string file=slot.path.filename().string();
                char user[5]={0};
                for(int c=0;c<4&&std::size_t(7+c)<file.size();++c)user[c]=file[std::size_t(7+c)];
                std::snprintf(text,sizeof(text),
                    "User%s %s %s-%s %.2d/%.2d/%.2d %.2d:%.2d %6d %2.0f%%",
                    user,header.name.c_str(),level,scene,year,mon,day,hour,minute,
                    int(header.score),double(header.rate));
            }
        }else if(m_replayPage==0){
            std::snprintf(text,sizeof(text),
                "No.%.2d --------  *-* --/--/-- --:-- ------ --%%",index+1);
        }else{
            std::snprintf(text,sizeof(text),
                "User---- --------  *-* --/--/-- --:-- ------ --.-%%");
        }
        print(text,104.0f,88.0f+18.0f*float(n),color);
    }
}
void TitleScene::returnFromHelp()
{
    // retail routine restores title mode 1 and the saved main-menu selection.
    m_background[0].start(m_title,102);m_background[1].start(m_title,103);
    m_background[4].interrupt(2);
    for(auto &a:m_particles)if(a.spriteId()==19||a.spriteId()==20)a.interrupt(2);
    m_versionAnimation.interrupt(2);
    for(int i=0;i<18;++i)m_menuAnimations[i].start(m_title,i,64.0f,130.0f+38.0f*(i%6));
    m_menuDelay=30;m_repeat=0;
}
void TitleScene::enterMissionSelect()
{
    m_inMissionSelect=true;
    for(auto &a:m_menuAnimations)a.interrupt(1);
    m_background[0].interrupt(1);m_background[1].interrupt(1);
    m_background[4].interrupt(3);
    for(auto &a:m_particles)if(a.spriteId()==19||a.spriteId()==20)a.interrupt(3);
    m_versionAnimation.interrupt(3);
}
void TitleScene::leaveMissionSelect()
{
    m_inMissionSelect=false;
    returnFromHelp();
}
bool TitleScene::updateHelp(const HelpInput &input)
{
    const bool returned=m_help.update(input);
    if(returned)returnFromHelp();
    return returned;
}
void TitleScene::select(int index)
{
    m_selected=(index+kItemCount)%kItemCount;
    for(int i=0;i<18;++i)m_menuAnimations[i].interrupt(i%6==m_selected?2:3);
    log::print("info : title selection=%d\r\n",m_selected);
}
void TitleScene::status(const wchar_t *message)
{
    platform::Texture fresh;
    if(fresh.fromText(m_device,message,16,0xFFFFFFFF)){m_status=std::move(fresh);m_statusSeconds=3;}
}
void TitleScene::activate()
{
    m_audio.playConfirm();
    if(m_selected==5){m_quit=true;return;}
    if(m_selected==0){
        m_missionSelect=true;
        return;
    }
    if(m_selected==2){
        // Original title mode 8, retail routine: musiccmt.txt track list and
        // eight retail comment rows, no placeholder overlay.
        if(!openMusicRoom())
            status(L"Music Room resources could not be loaded (see log.txt)");
        return;
    }
    if(m_selected==4){
        // Original help task retail routine: nine page entries from title.anm and
        // one help_%.2d.anm page texture each.
        if(!openHelp())
            status(L"Help resources could not be loaded (see log.txt)");
        return;
    }
    if(m_selected==3){
        // Original title mode 7, retail routine: seven retail rows, key slots,
        // display mode and BGM/SE volumes from th095.cfg.
        if(!openOption())
            status(L"Option resources could not be loaded (see log.txt)");
        return;
    }
    if(m_selected==1){
        // Original title mode 2, retail routine: four pages of twenty
        // replay/th95_*.rpy headers, printed through ascii.anm.
        if(!openReplayBrowser())
            status(L"Replay list resources could not be loaded (see log.txt)");
    }
}
void TitleScene::update(double deltaSeconds, const platform::InputState &platformInput)
{
    m_audio.update();
    m_statusSeconds=std::max(0.0,m_statusSeconds-deltaSeconds);
    const bool confirm=platformInput.confirm;
    const bool cancel=platformInput.cancel;
    if(m_menuDelay>0&&--m_menuDelay==0)select(m_selected);
    if(m_replayActive) {
        updateReplayBrowser(platformInput);
    } else if(m_help.active()) {
        HelpInput helpInput;
        helpInput.up=platformInput.up;
        helpInput.down=platformInput.down;
        helpInput.left=platformInput.left;
        helpInput.right=platformInput.right;
        helpInput.confirm=confirm;
        helpInput.cancel=cancel;
        helpInput.upRepeat=platformInput.upRepeat;
        helpInput.downRepeat=platformInput.downRepeat;
        updateHelp(helpInput);
    } else if(m_musicRoom.active()) {
        MusicRoomInput musicInput;
        musicInput.up=platformInput.up;
        musicInput.down=platformInput.down;
        musicInput.upRepeat=platformInput.upRepeat;
        musicInput.downRepeat=platformInput.downRepeat;
        musicInput.confirm=confirm;
        musicInput.cancel=cancel;
        updateMusicRoom(musicInput);
    } else if(m_option.active()) {
        OptionInput optionInput;
        // retail routine uses only pressed edges for up/down, but pressed or
        // repeat masks for left/right (volume/mode adjustment).
        optionInput.up=platformInput.up;
        optionInput.down=platformInput.down;
        optionInput.left=platformInput.left;
        optionInput.right=platformInput.right;
        optionInput.leftRepeat=platformInput.leftRepeat;
        optionInput.rightRepeat=platformInput.rightRepeat;
        optionInput.confirm=confirm;
        optionInput.cancel=cancel;
        // FUN_00419910(0) scans the raw 32-button state for key rebinding.
        // The port maps held Win32 keys back to their DIK index 0..31.
        optionInput.keyIndex=platformInput.buttonIndex;
        updateOption(optionInput);
    } else {
        updateHelp({}); // Retire Help panels while the title fades back in.
        if(m_active&&!m_inMissionSelect&&m_menuDelay==0) {
            const bool up=platformInput.upHeld;
            const bool down=platformInput.downHeld;
            const int direction=up&&!down?-1:down&&!up?1:0;
            const bool move=platformInput.up||platformInput.down||(direction&&m_repeat>=25&&(m_repeat-25)%5==0);
            if(direction){if(move){select(m_selected+direction);m_audio.playSelect();}++m_repeat;}else m_repeat=0;
            if(cancel){m_audio.playCancel();if(m_selected==5)m_quit=true;else select(5);}
            else if(confirm)activate();
        }
    }
    if(++m_frames==30){m_active=true;select(0);}
    for(auto &a:m_background)a.update();
    for(auto &a:m_replayBackground)a.update();
    for(auto &a:m_menuAnimations)a.update();
    m_versionAnimation.update();
    for(auto &a:m_particles)a.update();
    m_particles.erase(std::remove_if(m_particles.begin(),m_particles.end(),[](const auto &a){return !a.alive();}),m_particles.end());
    if(m_frames%5==0){format::AnmAnimation a;if(a.start(m_title,28,0,0,uint32_t(m_frames)*2654435761u))m_particles.push_back(std::move(a));}
}
void TitleScene::render(IDirect3DDevice9 *device)
{
    // Same ascending draw-layer ordering as the original animation manager.
    for(int layer=0;layer<=15;++layer)renderLayer(device,layer);
}
void TitleScene::renderLayer(IDirect3DDevice9 *device,int layer)
{
    for(const auto &a:m_background)if(a.layer()==layer)a.draw(device);
    if(m_replayActive){
        for(const auto &a:m_replayBackground)if(a.layer()==layer)a.draw(device);
        if(layer==15)drawReplayBrowser(device);
    }
    for(const auto &a:m_menuAnimations)if(a.layer()==layer)a.draw(device);
    for(const auto &a:m_particles)if(a.layer()==layer)a.draw(device);
    if(m_versionAnimation.layer()==layer)m_versionAnimation.draw(device);
    m_help.renderLayer(device,layer);
    m_musicRoom.renderLayer(device,layer);
    m_option.renderLayer(device,layer);
    if(layer==15&&m_statusSeconds>0&&m_status.get()) {
        platform::drawQuad(device,nullptr,8,436,624,36,0xD0000000);
        platform::drawQuad(device,m_status.get(),16,440,float(m_status.width()),float(m_status.height()));
    }
}
} // namespace th95::scene
