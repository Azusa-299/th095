#include "level.hpp"
#include "level_internal.hpp"
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
void LevelScene::beginPhoto(){
    m_photoSlot=m_sim.photoSuccess?std::clamp(m_sim.photos-1,0,9):10;
    m_captureX=320+m_sim.cameraX;m_captureY=16+m_sim.cameraY;
    m_captureWidth=m_sim.photoWidth;m_captureHeight=m_sim.photoHeight;
    m_capturePending=true;m_resultAge=1;m_photoResult.clear();m_resultDigits={};m_shotNotice={};
    auto&p=m_photographs[m_photoSlot];p.captured=false;p.digits={};p.score=m_sim.lastPhotoScore;
    p.timestamp=0;p.rate=0;
    p.pixels.clear();p.pixelWidth=p.pixelHeight=0;
    p.animation.start(m_photo,2*m_photoSlot,m_captureX,m_captureY);
    const float resultX=m_sim.playerY>=224||m_sim.playerX>=0?160:352;
    int delay=4;float y=96;
    const auto row=[&](const std::vector<int>&digits){
        float x=resultX;
        for(int sprite:digits){
            format::AnmAnimation a;a.start(m_photo,35,x,y);a.setSprite(sprite);a.setInteger(10000,delay);
            m_photoResult.push_back(std::move(a));
            if(const auto*s=m_photo.sprite(sprite))x+=s->w-3;
        }
        delay+=4;y+=12;
    };
    if(m_sim.photoBullets==0&&m_sim.photoTargets==0&&!m_sim.photoSelf)row({46,15});
    if(m_sim.photoSuccess){const int multiplier=int(m_sim.photoDistanceMultiplier*10);row({31,15+multiplier/10,26,15+multiplier%10});}
    if(m_sim.photoSelf)row({32,16,26,17});
    if(m_sim.photoSelf&&m_sim.photoSuccess)row({33,16,26,20});
    int divisor=100000;bool significant=false;
    for(int i=0;i<6;++i,divisor/=10){
        const int digit=m_sim.lastPhotoScore/divisor%10;
        significant=significant||digit!=0||i>=4;
        if(significant){
            m_resultDigits[i].start(m_photo,29,resultX+9*i,y+4);m_resultDigits[i].setSprite(15+digit);
            if(m_sim.photoSuccess){
                p.digits[i].start(m_photo,30,(m_photoSlot<5?64.0f:576.0f)+9*i,440-80*(m_photoSlot%5));
                p.digits[i].setSprite(15+digit);
            }
        }
    }
}
void LevelScene::capturePhoto(IDirect3DDevice9*d){
    m_capturePending=false;
    if(!m_worldFrameReady)return;
    auto&p=m_photographs[m_photoSlot];
    if(!p.texture.renderTarget(d,256,256)){log::print("error : photo render target creation failed\r\n");return;}
    Target target(d,p.texture);if(!target.active)return;
    // retail routine crops to the playfield and adds the original three-pixel border.
    const float left=std::clamp(std::floor(m_captureX)-(int(m_captureWidth)-6)/2,128.0f,511.0f);
    const float top=std::clamp(std::floor(m_captureY)-(int(m_captureHeight)-6)/2,16.0f,463.0f);
    const float right=std::clamp(std::floor(m_captureX)+(int(m_captureWidth)-6)/2,128.0f,511.0f);
    const float bottom=std::clamp(std::floor(m_captureY)+(int(m_captureHeight)-6)/2,16.0f,463.0f);
    const float width=std::max(1.0f,right-left),height=std::max(1.0f,bottom-top);
    platform::drawQuad(d,nullptr,0,0,width+6,height+6);
    platform::drawQuad(d,m_worldFrame.get(),3,3,width,height,0xFFFFFFFF,left/640,top/480,right/640,bottom/480);
    m_photo.setSpriteSize(m_photoSlot,width+6,height+6);p.captured=true;
    p.pixelWidth=int(width+6);p.pixelHeight=int(height+6);
    // Slot 10 is the rejected shutter, but retail routine reads its score against the
    // record too; its pixels are still a valid frame of the playfield.
    m_photoReadbackSlot=m_photoSlot;
}
void LevelScene::stageShotRecord(Photograph &photo)
{
    // Watching a replay must not overwrite the player's records.
    if(m_replayMode==ReplayMode::Play)return;
    if(!m_profile||photo.pixels.empty())return;
    // retail routine state 2 stamps the slot before comparing it with +24, so the
    // result page can later promote any shot with its own time and rate.
    photo.timestamp=uint32_t(std::time(nullptr));
    photo.rate=float(m_presentedFrames?100*m_slowFrames/(m_presentedFrames+m_slowFrames):0);
    if(photo.score<=0)return;
    const int mission=m_mission;
    if((m_profile->field(mission,80)&2)!=0)return;
    if(uint32_t(photo.score)<=m_profile->field(mission,24))return;
    uint32_t rateBits=0;std::memcpy(&rateBits,&photo.rate,sizeof(rateBits));
    m_profile->setField(mission,24,uint32_t(photo.score));
    m_profile->setField(mission,60,photo.timestamp);
    m_profile->setField(mission,76,rateBits);
    m_profile->stageBestshot(mission,photo.pixelWidth,photo.pixelHeight,
                             uint32_t(photo.score),photo.rate,photo.pixels);
    log::print("info : bestshot staged mission %d %dx%d score %d\r\n",
               mission,photo.pixelWidth,photo.pixelHeight,photo.score);
}
void LevelScene::applyBestShot(int slot)
{
    // retail routine (S on the success page) copies the displayed slot over the
    // record even when its score is below +24; retail routine then writes both files.
    // Playback never writes the profile: only a live run can replace the record.
    if(m_replayMode!=ReplayMode::Record)return;
    if(!m_profile||slot<0||slot>=int(m_photographs.size()))return;
    Photograph &photo=m_photographs[std::size_t(slot)];
    if(photo.pixels.empty()||photo.pixelWidth<=0||photo.pixelHeight<=0)return;
    const uint32_t score=uint32_t(std::max(0,photo.score));
    uint32_t rateBits=0;std::memcpy(&rateBits,&photo.rate,sizeof(rateBits));
    m_profile->setField(m_mission,24,score);
    m_profile->setField(m_mission,60,photo.timestamp);
    m_profile->setField(m_mission,76,rateBits);
    m_profile->stageBestshot(m_mission,photo.pixelWidth,photo.pixelHeight,score,photo.rate,photo.pixels);
    // 0x4294C0 sets +28192 = 120; 0x429C80 prints "BestShot was overwrited!"
    // at (212,224) while it counts down.
    m_bestOverwriteTicks=120;
    log::print("info : bestshot selected mission %d slot %d score %u\r\n",m_mission,slot,score);
}
void LevelScene::drawPhotos(IDirect3DDevice9*d){
    for(const auto&p:m_photographs){if(p.captured)p.animation.draw(d);for(const auto&a:p.digits)a.draw(d);}
    for(const auto&a:m_photoResult)a.draw(d);
    for(const auto&a:m_resultDigits)a.draw(d);
    m_shotNotice.draw(d);
}
}
