#include "mission_select.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "../log.hpp"

namespace th95::scene {
namespace {
uint32_t u32(const uint8_t *p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void put32(std::vector<uint8_t> &data,int pos,uint32_t value) {for(int i=0;i<4;++i)data[pos+i]=uint8_t(value>>(i*8));}
std::vector<uint8_t> extract(Archive &archive,const char *name) {
    const auto *entry=archive.find(name);if(!entry)return {};
    auto data=archive.extract(*entry);return data.size()==entry->size?data:std::vector<uint8_t>{};
}
// retail routines put the character caption in the top half
// and the required photo count in the bottom half of the same runtime slot.
bool caption(Archive &archive,format::AnmTexture &target,int id,int shots,IDirect3DDevice9 *device) {
    char name[40];std::snprintf(name,sizeof(name),id<0?"mission_%02d.anm":"mission%03d.anm",id<0?-id:id);
    auto base=extract(archive,name);
    if(id<0)return target.loadData(base,name,device);
    std::snprintf(name,sizeof(name),"mission%03d.anm",100+shots);
    const auto bottom=extract(archive,name);
    const auto pixels=[](const std::vector<uint8_t> &data)->const uint8_t * {
        if(data.size()<80||u32(data.data()+12)!=256||u32(data.data()+16)!=32)return nullptr;
        const auto offset=u32(data.data()+48);
        if(offset>data.size()||data.size()-offset<16+256*32*2||std::memcmp(data.data()+offset,"THTX",4))return nullptr;
        if(data[offset+6]!=5||data[offset+7]!=0)return nullptr;
        return data.data()+offset+16;
    };
    const auto *top=pixels(base),*low=pixels(bottom);if(!top||!low)return false;
    std::vector<uint8_t> combined(80+256*64*2,0);
    put32(combined,12,256);put32(combined,16,64);put32(combined,20,5);
    put32(combined,40,4);put32(combined,48,64);combined[52]=1;
    std::memcpy(combined.data()+64,"THTX",4);combined[70]=5;
    combined[72]=0;combined[73]=1;combined[74]=64;put32(combined,76,256*64*2);
    std::memcpy(combined.data()+80,top,256*32*2);
    std::memcpy(combined.data()+80+256*32*2,low,256*32*2);
    return target.loadData(combined,"mission caption slot",device);
}
}
const MissionDefinition &MissionSelectScene::definition() const {
    const int id=selected();
    for(const auto &mission:kMissionDefinitions)if(mission.id==id)return mission;
    return kMissionDefinitions[0];
}
int MissionSelectScene::cleared(int group) const {
    int count=0;
    for(const auto &mission:kMissionDefinitions)
        if((group<0||mission.id/10==group)&&(m_profile.field(mission.id,80)&1))++count;
    return count;
}
bool MissionSelectScene::groupUnlocked(int group) const {
    if(group<0||group>=12)return false;
    if(group==0)return true;
    // retail routine: signed comparison against the twelve -1 values at retail routine.
    // Keep the original field offsets until their semantics are established.
    constexpr int clearThreshold[]={0,4,8,14,20,25,30,35,40,45,66,99};
    constexpr int previousThreshold[]={0,3,3,3,3,3,3,3,3,3,66,99};
    uint32_t sum=0,previous=0;
    for(int i=0;i<120;++i)sum+=m_profile.field(i,16);
    for(const auto &mission:kMissionDefinitions)if(mission.id/10==group-1)previous+=m_profile.field(mission.id,68);
    return (int32_t(sum)>=-1&&int32_t(previous)>=10000)||
           cleared(-1)>=clearThreshold[group]||cleared(group-1)>=previousThreshold[group];
}
int MissionSelectScene::visibleGroups() const {
    int last=0;
    while(last<11&&groupUnlocked(last+1))++last;
    return last<2?3:last<5?6:std::min(last+2,12);
}
bool MissionSelectScene::start(int script) {return m_ui[script].start(m_title,script,script==105?512.0f:0.0f);}
bool MissionSelectScene::load(Archive &archive,IDirect3DDevice9 *device,platform::Audio *audio,
                              format::ScoreProfile *profile)
{
    m_archive=&archive;m_device=device;m_audio=audio;m_result=Result::Failed;
    m_loaded=false;m_closing=false;m_ticks=0;m_commentTicks=-1;m_faceId=-1;m_showRate=false;
    m_cheatProgress=-1;
    m_displayLines={};m_pendingLines={};
    m_profileSource=profile;
    if(profile&&!m_profileMounted) {
        m_profile=*profile;m_profileMounted=true;
        m_group=std::clamp(m_profile.savedGroup(),0,visibleGroups()-1);
        m_indices[m_group]=std::clamp(m_profile.savedScene(),0,kMissionCounts[m_group]-1);
    }
    m_ui={};m_comments={};m_outgoing.clear();
    if(!m_title.load(archive,"title.anm",device)||!m_text.load(archive,"text.anm",device)||
       !m_ascii.load(archive,"ascii.anm",device)||
       !m_messages.parse(extract(archive,"mission.msg")))return false;
    // Blank best-photo slots are opaque black in retail routine, not atlas pages.
    std::vector<uint8_t> blank(80+256*256*2,0);
    put32(blank,12,256);put32(blank,16,256);put32(blank,20,5);put32(blank,40,4);
    put32(blank,48,64);blank[52]=1;std::memcpy(blank.data()+64,"THTX",4);
    blank[70]=5;blank[73]=1;blank[75]=1;put32(blank,76,256*256*2);
    for(std::size_t i=81;i<blank.size();i+=2)blank[i]=0xF0;
    if(!m_blankPhoto.loadData(blank,"empty best photo",device)||
       !m_title.bindExternalPage(1,&m_blankPhoto)||!m_title.bindExternalPage(2,&m_caption)||
       !m_title.bindExternalPage(3,&m_face)||!m_title.bindExternalPage(4,&m_faceText)||
       !m_text.bindExternalTexture(0,&m_commentAtlas))return false;
    // Page 1 also accepts a raw texture for the loaded best shot; the blank
    // black page above stays as the fallback when no file can be read.
    const std::vector<uint8_t> black(256*256*4,0);
    std::vector<uint8_t> blankPixels(black);
    for(std::size_t i=3;i<blankPixels.size();i+=4)blankPixels[i]=0xF0;
    platform::Texture page;
    if(page.fromPixels(device,blankPixels.data(),256,256))m_photoPage=std::move(page);
    if(!m_title.bindExternalTexture(1,&m_photoPage))return false;
    m_photoMission=-1;
    for(int script:{104,105,30,34,35,36,68,72,21,22,69,70,19,18,20,73,74,75})
        if(!start(script))return false;
    if(!start(m_group>10?33:32))return false;
    // No valid bestshot pixels have been loaded. Retail hides this sprite
    // instead of displaying the uninitialised dynamic texture.
    m_ui[18].setEnabled(false);
    for(int script=76;script<=99;++script)if(!start(script))return false;
    for(int row=0;row<kMissionCounts[m_group];++row)for(int part=0;part<3;++part) {
        if(!start(37+row*3+part))return false;
        m_ui[37+row*3+part].setEnabled(groupUnlocked()&&(part<2||m_profile.field(m_group*10+row,16)!=0));
    }
    refreshNumbers();
    colorBackground(true);
    m_loaded=true;m_result=Result::Running;
    log::print("info : Mission Select: original title/text ANMs, %d visible groups; read-only score profile\r\n",visibleGroups());
    return true;
}
void MissionSelectScene::refreshProfile() {
    if(m_profileSource)m_profile=*m_profileSource;
    for(int row=0;row<kMissionCounts[m_group];++row)
        for(int part=0;part<3;++part)
            m_ui[37+row*3+part].setEnabled(groupUnlocked()&&(part<2||m_profile.field(m_group*10+row,16)!=0));
    m_photoMission=-1;
    refreshPhoto();
    refreshNumbers();
}
void MissionSelectScene::highlight() {
    for(int row=0;row<kMissionCounts[m_group];++row)
        for(int part=0;part<3;++part)m_ui[37+row*3+part].interrupt(row==missionIndex()?2:3);
}
void MissionSelectScene::colorBackground(bool initial) {
    // retail routine -> retail routine, table retail routine; the second half follows the first.
    constexpr DWORD colors[]={0xFFFFC0C0,0xFFC0D0FF,0xFFBFFFFF,0xFFFFFFC0,
        0xFFFFC0C0,0xFFC0FFD0,0xFFFFB0B0,0xFFE0C0FF,0xFFFFC0E0,0xFFFFB0FF,
        0xFF808080,0xFF8080FF};
    const DWORD color=groupUnlocked()?colors[m_group]:initial?0xFFFFFFFF:0xFF8080FF;
    for(int id:{104,105})m_ui[id].animateColor(color,60);
}
void MissionSelectScene::refreshNumbers() {
    m_ui[34].setSprite(m_group>10?m_group+44:m_group+55);
    m_ui[35].setEnabled(m_group!=0);m_ui[36].setEnabled(m_group!=visibleGroups()-1);
    m_ui[73].setSprite((m_group<11?m_group:m_group-11)+40);
    m_ui[75].setSprite(missionIndex()+40);
    for(int i=76;i<=91;++i) {
        // Empty date/time and high score fields. Separators retain their scripts.
        if(i!=78&&i!=83)m_ui[i].setSprite(52);
    }
    const auto timestamp=m_profile.field(selected(),60);
    if(timestamp) {
        const std::time_t time=timestamp;const auto *date=std::localtime(&time);
        if(date) {
            const int pairs[]={date->tm_mon+1,date->tm_mday,date->tm_hour,date->tm_min};
            const int slots[]={76,79,81,84};
            for(int i=0;i<4;++i){m_ui[slots[i]].setSprite(pairs[i]/10+39);m_ui[slots[i]+1].setSprite(pairs[i]%10+39);}
        }
        auto score=m_profile.field(selected(),24);
        for(int id=91;id>=86;--id){m_ui[id].setSprite(score%10+39);score/=10;}
    }
    const auto total=m_profile.field(selected(),68);
    uint32_t divisor=100000;
    for(int id=94;id<=99;++id,divisor/=10) {
        m_ui[id].setEnabled(id==99||total/divisor!=0);
        m_ui[id].setSprite((total/divisor)%10+75);
    }
    refreshStats();
}
void MissionSelectScene::refreshStats() {
    // retail routine (state 2). The total is the sum of every record's +16 field,
    // the cleared count comes from bit 0 of +80, and the selected row reuses
    // the same +16 field for High Score. Slow Rate is a stored percentage.
    uint32_t total=0;
    for(int mission=0;mission<120;++mission)total+=m_profile.field(mission,16);
    const int success=cleared(-1);
    const uint32_t high=std::min<uint32_t>(m_profile.field(selected(),16),99999999u);
    const uint32_t bits=m_profile.field(selected(),72);
    float slow=0.0f;std::memcpy(&slow,&bits,4);
    if(!std::isfinite(slow))slow=0.0f;
    std::snprintf(m_totalText.data(),m_totalText.size(),"%.8d",int(total));
    std::snprintf(m_successText.data(),m_successText.size(),"%2d Scene Success",std::min(success,99));
    std::snprintf(m_highText.data(),m_highText.size(),"High Score %.6d",int(high));
    std::snprintf(m_slowText.data(),m_slowText.size(),"Slow Rate  %2.0f%%",double(slow));
    const uint32_t rateBits=m_profile.field(selected(),76);
    float rate=0.0f;std::memcpy(&rate,&rateBits,4);
    if(!std::isfinite(rate))rate=0.0f;
    std::snprintf(m_rateText.data(),m_rateText.size(),"%2.0f%%",double(rate));
}
void MissionSelectScene::ascii(IDirect3DDevice9 *device,const char *text,float x,float y,DWORD tint,float scale) const {
    // retail routine / retail routine: fixed nine pixel advance, fourteen pixel glyphs, both
    // multiplied by the queued scale (0.5 for the retail debug rows).
    for(const auto *p=reinterpret_cast<const unsigned char *>(text);*p;++p) {
        m_ascii.draw(device,*p-32,x,y,tint,scale,scale);
        x+=9.0f*scale;
    }
}
bool MissionSelectScene::refreshDetails() {
    const auto *mission=m_messages.find(m_group,missionIndex());
    const auto accumulated=m_profile.field(selected(),16);
    const bool reached=groupUnlocked()&&accumulated!=0&&accumulated>=(mission?mission->minimumScore:0);
    const auto *message=reached?mission:m_messages.find(98,!groupUnlocked()?0:accumulated?2:m_profile.field(selected(),60)?3:1);
    if(!caption(*m_archive,m_caption,captionId(),definition().shots,m_device))return false;
    m_pendingLines=message?message->lines:std::array<std::string,3>{};
    m_commentColor=reached?0xCFCFFF:0xDF8F8F;
    const int face=message?message->face:0;
    if(m_faceId!=face) {
        char name[32];std::snprintf(name,sizeof(name),"fc%02d.anm",face);
        if(!m_face.load(*m_archive,name,m_device))return false;
        std::snprintf(name,sizeof(name),"fc%02db.anm",face);
        if(!m_faceText.load(*m_archive,name,m_device))return false;
        m_faceId=face;
        m_ui[21].interrupt(4);m_ui[22].interrupt(4);
    }
    m_ui[20].interrupt(4);
    for(auto &comment:m_comments)comment.interrupt(1);
    m_commentTicks=0;
    return refreshPhoto();
}
bool MissionSelectScene::refreshPhoto() {    // retail routine: the file name carries the one-based group and scene, and retail routine
    // uploads the decoded pixels into the page 1 slot. Missing files keep the
    // opaque placeholder and leave sprite 18 hidden.
    const int mission=selected();
    if(mission==m_photoMission)return true;
    m_photoMission=mission;
    m_bestshot={};
    std::vector<uint8_t> pixels(256*256*4,0);
    bool shown=false;
    // retail routine only probes files for records that carry a timestamp, and it
    // clears the photo row's score/date when the record points at a missing
    // file. The checksum must match the SC record's +64 word.
    if(!m_assetDirectory.empty()&&m_profile.present(mission)&&m_profile.field(mission,60)!=0) {
        const auto path=m_assetDirectory/L"bestshot"/
                        std::filesystem::path(format::bestshotFileName(mission));
        std::error_code ec;
        if(!std::filesystem::exists(path,ec)&&!ec) {
            m_profile.setField(mission,24,0);m_profile.setField(mission,60,0);
            if(m_profileSource) {
                m_profileSource->setField(mission,24,0);
                m_profileSource->setField(mission,60,0);
            }
            log::print("info : Mission Select: bestshot %s missing, photo row cleared\r\n",
                       format::bestshotFileName(mission).c_str());
        } else if(m_bestshot.load(path)&&!m_bestshot.pixels().empty()) {
            const uint32_t stored=m_profile.field(mission,64);
            if(m_bestshot.checksum()==stored) {
                pixels=m_bestshot.pixels();shown=true;
            } else {
                log::print("info : Mission Select: bestshot %s checksum %08X != %08X\r\n",
                           format::bestshotFileName(mission).c_str(),m_bestshot.checksum(),stored);
            }
        }
    }
    if(!shown)for(std::size_t i=3;i<pixels.size();i+=4)pixels[i]=0xF0;
    if(m_device) {
        platform::Texture page;
        if(page.fromPixels(m_device,pixels.data(),256,256))m_photoPage=std::move(page);
        else return false;
    }
    if(shown)log::print("info : Mission Select: best shot %d/%d %dx%d\r\n",m_group+1,missionIndex()+1,m_bestshot.width(),m_bestshot.height());
    // The reveal script has already run to its delete instruction while the
    // page waited for the details request; restart it and jump to label 2,
    // which fades the loaded pixels up from black over thirty ticks.
    if(shown) {
        m_ui[18]={};
        if(!start(18))return false;
        m_ui[18].interrupt(2);
        // Script 19 is the opaque black cover over the photo slot. The loaded
        // pixels fade it out through label 1, exactly like the retail request.
        m_ui[19].interrupt(1);
    } else {
        // Scenes without a stored shot keep the opaque cover; restart it in
        // case the previous selection already faded it out.
        m_ui[19]={};
        if(!start(19))return false;
    }
    m_ui[18].setEnabled(shown);
    return true;
}
void MissionSelectScene::toggleLock() {
    // retail routine: only records that exist can be locked, and the flag is bit 1 of
    // the trailing +80 word.  retail routine uses it to keep a better shot from
    // overwriting the stored high score.
    const int mission=selected();
    if(!m_profile.present(mission))return;
    const uint32_t flags=m_profile.field(mission,80)^2u;
    m_profile.setField(mission,80,flags);
    if(m_profileSource)m_profileSource->setField(mission,80,flags);
}
bool MissionSelectScene::changeGroup(int direction) {
    for(int row=0;row<kMissionCounts[m_group];++row)for(int part=0;part<3;++part) {
        auto &a=m_ui[37+row*3+part];a.interrupt(direction>0?7:8);
        m_outgoing.push_back(std::move(a));a={};
    }
    m_group=(m_group+direction+visibleGroups())%visibleGroups();
    m_ui[32]={};m_ui[33]={};start(m_group>10?33:32);start(34);
    for(int row=0;row<kMissionCounts[m_group];++row)for(int part=0;part<3;++part) {
        const int id=37+row*3+part;if(!start(id))return false;
        m_ui[id].setEnabled(groupUnlocked()&&(part<2||m_profile.field(m_group*10+row,16)!=0));
        m_ui[id].interrupt(direction>0?10:9);
    }
    refreshNumbers();
    for(int id:{73,74,75})m_ui[id].interrupt(2);
    // The retail group change resets the input timer to 20; at tick 30
    // the row highlight and selected mission are refreshed again.
    m_ticks=20;
    m_ui[20].interrupt(5);
    colorBackground(false);
    return true;
}
void MissionSelectScene::animate() {
    for(auto &a:m_ui)a.update();
    for(auto &a:m_comments)a.update();
    for(auto &a:m_outgoing)a.update();
    m_outgoing.erase(std::remove_if(m_outgoing.begin(),m_outgoing.end(),[](const auto &a){return !a.alive();}),m_outgoing.end());
}
bool MissionSelectScene::feedCheatCode(const platform::InputState &input) {
    // Temporary testing cheat: typing the Konami sequence on this page toggles
    // the invulnerability flag the next mission starts with, and typing it
    // again clears it. Only edge flags are consumed, so holding a key cannot
    // repeat a step. The B/A steps are swallowed so they cannot leave the page
    // or start a mission; direction steps still navigate, which cancels out.
    static constexpr int kCode[]={1,1,2,2,3,4,3,4,5,6,5,6}; // up up down down left right left right B A B A
    constexpr int kSteps=int(sizeof(kCode)/sizeof(kCode[0]));
    int token=0;
    if(input.up)token=1;
    else if(input.down)token=2;
    else if(input.left)token=3;
    else if(input.right)token=4;
    else if(input.cancel)token=5;
    else if(input.shoot||input.confirm)token=6;
    if(!token)return false;
    if(m_cheatProgress+1<kSteps&&token==kCode[m_cheatProgress+1]) {
        ++m_cheatProgress;
        if(m_cheatProgress==kSteps-1) {
            m_cheatProgress=-1;
            m_cheatInvulnerable=!m_cheatInvulnerable;
            if(m_audio)m_audio->playConfirm();
            log::print("info : cheat invulnerable %s\r\n",m_cheatInvulnerable?"on":"off");
        }
        return token>=5;
    }
    m_cheatProgress=token==kCode[0]?1:-1;
    return false;
}
MissionSelectScene::Result MissionSelectScene::update(const platform::InputState &input)
{
    if(!m_loaded)return m_result;
    if(m_closing) {
        animate();
        ++m_ticks;
        if(m_result==Result::StartMission) {
            // The level is already running underneath. Retail interrupted the
            // page backgrounds at state-5 timer 40, so their 60 tick fade-out
            // keeps playing as an overlay above the mission.
            if(m_ticks>=100){m_loaded=false;m_closing=false;}
        } else if(m_result==Result::ReturnToTitle) {
            if(m_ticks>=60){m_loaded=false;m_closing=false;}
        } else if(m_ticks>=40) {
            // Retail state 5: at timer 40 every page animation (including the
            // 104/105 backgrounds) is interrupted and the level scene starts in
            // the same tick, so the fade-out overlaps the mission.
            for(auto &a:m_ui)a.interrupt(1);
            for(auto &a:m_comments)a.interrupt(1);
            m_result=Result::StartMission;
        }
        return m_result;
    }
    if(m_result!=Result::Running)return m_result;
    const bool cheat=feedCheatCode(input);
    // retail routine runs these toggles in the page body, before the 30 tick input
    // gate that guards navigation.
    if(input.showRate)m_showRate=!m_showRate;
    if(input.lockBest)toggleLock();
    if(m_ticks<30){++m_ticks;animate();return m_result;}
    if(m_ticks==30) {
        highlight();
        if(!refreshDetails())m_result=Result::Failed;
    }
    if(m_ticks>=30) {
        bool changed=false;
        if(input.left!=input.right) {
            const int direction=input.right?1:-1;
            if(!changeGroup(direction))m_result=Result::Failed;
            else if(m_audio)m_audio->playSelect();
            changed=true;
        } else if(groupUnlocked()&&((input.up||input.upRepeat)!=(input.down||input.downRepeat))) {
            const int direction=(input.down||input.downRepeat)?1:-1;
            auto &index=m_indices[m_group];index=(index+direction+kMissionCounts[m_group])%kMissionCounts[m_group];
            highlight();refreshNumbers();
            for(int id:{73,74,75})m_ui[id].interrupt(2);
            if(!refreshDetails())m_result=Result::Failed;
            else if(m_audio)m_audio->playSelect();
            changed=true;
        } else if(!cheat&&input.confirm&&groupUnlocked()&&selected()>=0) {
            // Retail plays se 10 and requests interrupt 1 on most page elements
            // immediately, but the 104/105 backgrounds are deliberately left
            // running: state 5 holds the page for another 40 ticks.
            if(m_audio)m_audio->playConfirm();
            for(int i=0;i<int(m_ui.size());++i)
                if(i!=104&&i!=105)m_ui[i].interrupt(1);
            for(auto &a:m_comments)a.interrupt(1);
            // The common tail below increments the timer once for this update;
            // -1 makes the state-5 hold exactly 40 further ticks.
            m_closing=true;m_ticks=-1;
            changed=true;
        }
        if(!cheat&&input.cancel&&!changed) {
            if(m_audio)m_audio->playCancel();
            for(auto &a:m_ui)a.interrupt(1);
            for(auto &a:m_comments)a.interrupt(1);
            m_closing=true;m_ticks=0;m_result=Result::ReturnToTitle;
        }
    }
    if(!m_closing&&m_commentTicks>=0) {
        // retail routine reveals comment lines at ticks 8, 12 and 16.
        for(int row=0;row<3;++row)if(m_commentTicks==8+row*4) {
            // Each retail line is rasterised only when its animation starts.
            // Preserve the other rows while their old text fades out.
            m_displayLines[row]=m_pendingLines[row];
            if(m_device) {
                platform::Texture atlas;
                if(!atlas.fromGameTextAtlas(m_device,m_displayLines,m_commentColor,m_messages.codePage()))m_result=Result::Failed;
                else m_commentAtlas=std::move(atlas);
            }
            if(!m_comments[row].start(m_text,11+row))m_result=Result::Failed;
        }
        ++m_commentTicks;
    }
    for(int id:{21,22,69})m_ui[id].setEnabled(!input.shiftHeld);
    for(auto &comment:m_comments)comment.setEnabled(!input.shiftHeld);
    ++m_ticks;animate();return m_result;
}
void MissionSelectScene::render(IDirect3DDevice9 *device) const {
    if(!m_loaded)return;
    for(int layer=0;layer<=15;++layer)renderLayer(device,layer);
}
void MissionSelectScene::renderOverlay(IDirect3DDevice9 *device) const {
    if(!overlayActive())return;
    // Retail keeps updating and drawing the whole page container after the
    // state-5 switch (sub_445E80 LABEL_81). Every handle was interrupted at
    // confirm, so the logo (scripts 18/19/20) and the rows fade out over the
    // first ticks of the mission while the 104/105 backgrounds are interrupted
    // again at timer 40 and last until timer 100. The page's own ascii block is
    // printed by the select state only and must not bleed into the mission.
    // Retail clips the surviving page container to the playfield viewport: the
    // 384x448 field still shows the select background fading, while the level's
    // own side panels are already visible outside it.
    const RECT field{128,16,512,464};
    device->SetScissorRect(&field);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
    for(int layer=0;layer<=15;++layer) {
        for(const auto &a:m_outgoing)if(a.layer()==layer)a.draw(device);
        for(const auto &a:m_ui)if(a.layer()==layer)a.draw(device);
        for(const auto &a:m_comments)if(a.layer()==layer)a.draw(device);
    }
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
}
void MissionSelectScene::renderLayer(IDirect3DDevice9 *device,int layer) const {
    if(!m_loaded)return;
    for(const auto &a:m_outgoing)if(a.layer()==layer)a.draw(device);
    for(const auto &a:m_ui)if(a.layer()==layer)a.draw(device);
    for(const auto &a:m_comments)if(a.layer()==layer)a.draw(device);
    if(layer<15)return;
    // Retail prints the summary block from the select state only: sub_452630
    // tests the page state word (this+6212) and draws when it is 2. Confirm
    // raises state 5 on the same tick (sub_447D00), so the block disappears
    // before the next frame; state 6 (return to title) behaves the same.
    if(m_closing)return;
    // The retail summary block is queued through retail routine and drawn after the
    // sprite layers; positions and colours come from retail routine.
    ascii(device,m_totalText.data(),516.0f,34.0f,0xFFFFFFFF);
    ascii(device,m_successText.data(),468.0f,48.0f,0xFFFFFFFF);
    ascii(device,m_highText.data(),242.0f,332.0f,0xFFFFFFFF);
    ascii(device,m_slowText.data(),242.0f,346.0f,0xFF80D0D0);
    // Conditional rows: the S toggle reveals the last shot's rate at half
    // size, and the record's lock bit draws "L" on the photo's top corner.
    if(m_showRate)ascii(device,m_rateText.data(),382.0f,285.0f,0xFFC0E0E0,0.5f);
    if(m_profile.field(selected(),80)&2)ascii(device,"L",435.0f,124.0f,0xFFC0E0E0,0.5f);
}
} // namespace th95::scene
