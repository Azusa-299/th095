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
void LevelScene::ascii(IDirect3DDevice9*d,const char*text,float x,float y,DWORD tint){
    // The retail fixed advance is nine pixels; glyphs are 14.
    for(const unsigned char*p=reinterpret_cast<const unsigned char*>(text);*p;++p){m_ascii.draw(d,*p-32,x,y,tint);x+=9;}
}
void LevelScene::drawWorld(IDirect3DDevice9*d,bool aimingEffects){
    DWORD background=0x80808080,objects=0x80808080;
    if(aimingEffects){
        // Retail only modulates the world animation while aiming (its colour
        // object is the sole owner of the 0x60xxxxxx constant); bullets,
        // enemies, the player and effects keep their normal modulation. Tinting
        // them here made every sprite a flat dark silhouette inside the frame.
        const unsigned gray=64+unsigned(64*std::clamp((.35f-m_sim.charge)/.35f,0.0f,1.0f));
        background=0x60000000|gray*0x010101;
    }
    m_world.draw(d,background);
    const RECT field{128,16,512,464};d->SetScissorRect(&field);d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
    for(int layer=0;layer<=6;++layer){
        // The declaration backdrop is two field-sized quads. Row 1 is the base
        // plate (enm1 page 1 is solid black, enm2 page 1 a near-black brown) and
        // is drawn opaque so it masks the 3D field; row 0 adds the card's colour
        // camo with the sprite alpha on top. Blending row 0 through its own page
        // alpha over that black base reproduces the retail field tone, while
        // drawing it opaque washed the field out to the raw texture colour.
        // Both rows are older than any bullet of the same layer (retail draws a
        // layer in insertion order), so they must come first: drawing them after
        // the bullets hid every layer-0 bullet behind the card backdrop.
        if(layer==0)for(const auto&e:m_entryEffects){
            if(e.rows[1].alive())e.rows[1].drawOpaque(d,objects);
            if(e.rows[0].alive())e.rows[0].draw(d,objects);
        }
        for(const auto&b:m_sim.bullets())if(bulletLayers[std::clamp(b.type,0,23)]==layer){const auto i=m_bulletVisuals.find(b.id);if(i!=m_bulletVisuals.end())i->second.animation.draw(d,objects);}
        if(layer==3)for(const auto&e:m_sim.enemies())if(!e.hidden){const auto i=m_enemyVisuals.find(e.id);if(i!=m_enemyVisuals.end())i->second.animation.draw(d,objects);}
        if(layer==4&&!m_sim.dead)m_playerVisual.draw(d,objects);
        if(layer==6){
            m_hitpoint.draw(d,objects);
            // Retail registers the ascii brush before the text strip, so it
            // sits behind the name. 0x4089F0 dims both from the player's Y.
            const unsigned bannerAlpha=unsigned(std::clamp((m_sim.playerY-64)*191/64+64,64.0f,255.0f));
            const DWORD bannerMod=(DWORD(bannerAlpha*128/255)<<24)|0x00808080;
            if(m_spellBrush.alive())m_spellBrush.draw(d,bannerMod);
            if(m_spellName.alive())m_spellName.draw(d,bannerMod);
        }
        if(layer==6)for(const auto&[id,circle]:m_spawnCircles)circle.draw(d,objects);
        // TH95_FILTER210 debug filter removed; draw everything on its layer.
        for(const auto&e:m_effects)if(e.animation.layer()==layer&&!(e.managed&&e.age<4.0f))e.animation.draw(d,objects);
    }
    m_deathFlash.draw(d);for(const auto&a:m_deathParticles)a.draw(d);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
}
void LevelScene::drawAimOutside(IDirect3DDevice9*d){
    // retail routine protects the inside using depth. Four non-overlapping scissors
    // produce the same mask on the D3D9 host without a depth surface.
    const LONG l=std::clamp(LONG(320+m_sim.cameraX-m_sim.photoWidth/2),128L,512L);
    const LONG r=std::clamp(LONG(320+m_sim.cameraX+m_sim.photoWidth/2),128L,512L);
    const LONG t=std::clamp(LONG(16+m_sim.cameraY-m_sim.photoHeight/2),16L,464L);
    const LONG b=std::clamp(LONG(16+m_sim.cameraY+m_sim.photoHeight/2),16L,464L);
    const RECT outside[]={{128,16,512,t},{128,b,512,464},{128,t,l,b},{r,t,512,b}};
    for(const auto&rect:outside)if(rect.left<rect.right&&rect.top<rect.bottom){
        d->SetScissorRect(&rect);for(const auto&a:m_aimOutside)a.draw(d);
    }
}
void LevelScene::renderGameplay(IDirect3DDevice9*d){
    if(!m_loaded||!d)return;
    // The result picture is the playfield captured while aiming: retail freezes
    // the shutter frame, so the untinted aiming snapshot is reused instead of the
    // tinted frame drawn on release.
    const bool needWorldFrame=m_sim.cameraState==1;
    if(needWorldFrame&&m_worldFrame.renderTarget(d,640,480)){
        Target target(d,m_worldFrame);
        if(target.active){drawWorld(d);m_worldFrameReady=true;}
    }
    if(m_capturePending)capturePhoto(d);
    if(m_sim.cameraState==1)drawWorld(d,true);else drawWorld(d);
    const RECT field{128,16,512,464};d->SetScissorRect(&field);d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
    if(m_sim.cameraState==1){drawAimOutside(d);d->SetScissorRect(&field);}
    for(const auto&a:m_camera)a.draw(d);
    if(m_sim.cameraState==0||m_sim.cameraState==3)m_cameraStatus.draw(d);
    for(const auto&a:m_percent)a.draw(d);
    if(m_sim.cameraState==1)for(const auto&a:m_aim)a.draw(d);
    if(m_sim.flash)platform::drawQuad(d,nullptr,128,16,384,448,(DWORD(m_sim.flash*24)<<24)|0xFFFFFF);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    for(const auto&a:m_borders)a.draw(d);
    // front.anm script 12 markers sit on layer 8, i.e. over the layer-7 border
    // plate; they stay outside the field scissor because they live in the
    // 128px side gutters.
    for(const auto&leaf:m_leafMarks)leaf.draw(d);
    m_intro.draw(d);for(const auto&a:m_timer)a.draw(d);
    const unsigned alpha=unsigned(std::clamp((m_sim.playerY-64)*191/64+64,64.0f,255.0f));const DWORD white=(alpha<<24)|0xFFFFFF;
    char text[64];
    std::snprintf(text,sizeof(text),"HiScore %.7d",std::max(m_sessionHighScore,m_sim.highScore));ascii(d,text,128,20,white);
    std::snprintf(text,sizeof(text),"  Score %.7d",m_sim.score);ascii(d,text,128,32,white);
    std::snprintf(text,sizeof(text),"Photo %02d/%02d",m_sim.photos,m_sim.requiredPhotos);ascii(d,text,400,20,(alpha<<24)|0xDFEFFF);
    drawPhotos(d);
    // Retail paints the case-0x23 orange leaf and white flash on layer 8,
    // above the layer-7 photo frame, so they must be drawn after drawPhotos.
    for(const auto&flash:m_leafFlashes)flash.draw(d);
    std::snprintf(text,sizeof(text),"%2d-%d",1,m_mission+1);ascii(d,text,464,32,white);
}
void LevelScene::render(IDirect3DDevice9*d){
    if(!m_loaded||!d)return;
    if(!m_menuOpen){
        renderGameplay(d);
    }else if(m_menuCapturePending){
        if(m_gameFrame.renderTarget(d,640,480)){Target target(d,m_gameFrame);if(target.active)renderGameplay(d);}
        else renderGameplay(d);
        if(m_menuFrame.renderTarget(d,128,128)){
            Target target(d,m_menuFrame);
            if(target.active){
                platform::drawQuad(d,m_gameFrame.get(),0,0,128,128,0xFFFFFFFF,128.0f/640,16.0f/480,512.0f/640,464.0f/480);
                m_menuCapturePending=false;
            }
        }
    }
    if(m_menuOpen){
        if(m_gameFrame.get())platform::drawQuad(d,m_gameFrame.get(),0,0,640,480);
        if(m_menuKind==MenuKind::ReplayList||m_menuKind==MenuKind::ReplayName){
            drawReplaySave(d);
            return;
        }
        for(int i=0;i<m_menuCount;++i)m_menu[i].draw(d);
        // 0x429C80 only prints the state-5 statistics; the eight-tick close wait
        // (state 6) keeps drawing the fading animations but not the numbers.
        if(m_menuKind==MenuKind::Success&&!m_playbackOutcome){
            // The best-photo animation keeps drawing during the eight-tick
            // close wait (retail draws slots 23/24 in every state); only the
            // statistics block belongs to state 5.
            m_bestPhoto.draw(d);
            if(m_menuAction==MenuAction::None){
                char text[80];const DWORD color=(DWORD(std::min(255,m_menuTicks*255/32))<<24)|0xFFFFFF;
                // 0x429C80 compares the shown slot with 0x429450's highest-scoring
                // slot: only that one gets the "Best Shot" prefix, others "Shot".
                if(m_bestSlot>=0){
                    const bool best=m_bestSlot==runtimeBestSlot();
                    std::snprintf(text,sizeof(text),best?"  Best Shot   %.6d":"       Shot   %.6d",
                                  m_photographs[std::size_t(m_bestSlot)].score);
                    ascii(d,text,230,290,color);
                }
                std::snprintf(text,sizeof(text),"      Score  %.7d",m_sim.score);ascii(d,text,230,40,color);
                std::snprintf(text,sizeof(text)," High Score  %.7d",std::max(m_sessionHighScore,m_sim.highScore));ascii(d,text,230,54,color);
                std::snprintf(text,sizeof(text),"Total Score %.8u",m_profileTotal+unsigned(std::max(0,std::max(m_sim.score,m_sessionHighScore)-m_profileHigh)));ascii(d,text,230,68,color);
                const double slow=m_presentedFrames?100*m_slowFrames/(m_presentedFrames+m_slowFrames):0;
                std::snprintf(text,sizeof(text),"  Slow Rate      %2.0f%%",slow);ascii(d,text,230,82,(color&0xFF000000)|0xD0D0E0);
                if(m_bestOverwriteTicks>0)ascii(d,"BestShot was overwrited!",212,224,0xFFFFFFFF);
            }
        }
        if(m_menuKind==MenuKind::Failure&&m_menuAction==MenuAction::None)
            for(const auto&a:m_failureText)a.draw(d);
    }
}
void LevelScene::drawReplaySave(IDirect3DDevice9*d){
    // 0x429C80 states 13/14: plain 9px ascii rows over the frozen result frame.
    char text[96],level[8],scene[8];
    auto dateParts=[&](uint32_t timestamp){
        const std::time_t stamp=timestamp;
        const std::tm*tm=std::localtime(&stamp);
        const int mon=tm?tm->tm_mon+1:0,day=tm?tm->tm_mday:0;
        const int hour=tm?tm->tm_hour:0,minute=tm?tm->tm_min:0;
        return std::array<int,4>{mon,day,hour,minute};
    };
    if(m_menuKind==MenuKind::ReplayList){
        ascii(d,"Select Replay Number",160,32);
        for(int k=0;k<format::kReplayNumberedSlots;++k){
            const DWORD color=k==m_replaySelection?0xFFFFFFFFu:0xFF404040u;
            const auto&slot=m_replaySlots[std::size_t(k)];
            if(slot.exists){
                const auto&header=slot.header;
                if(header.level==10)std::strcpy(level,"EX");
                else std::snprintf(level,sizeof(level),"%2d",header.level+1);
                std::snprintf(scene,sizeof(scene),"%d",header.scene+1);
                const auto date=dateParts(header.timestamp);
                std::snprintf(text,sizeof(text),"No.%.2d %s %s-%s %.2d/%.2d %.2d:%.2d %6d",
                              k+1,header.name.c_str(),level,scene,
                              date[0],date[1],date[2],date[3],int(header.score));
            }else{
                // 0x429C80 formats the empty slot as
                // "No.%.2d %s %s-%s %s ------" with the four dashed fields,
                // which keeps the "-- -" level/scene separators intact.
                std::snprintf(text,sizeof(text),
                              "No.%.2d %s %s-%s %s ------",k+1,
                              "--------","--","-","--/-- --:--");
            }
            ascii(d,text,144,64+18*k,color);
        }
        return;
    }

    ascii(d,"Replay Name Regist",160,32);
    const auto&header=m_replay.header;
    if(header.level==10)std::strcpy(level,"EX");
    else std::snprintf(level,sizeof(level)," %d",header.level+1);
    std::snprintf(scene,sizeof(scene),"%d",header.scene+1);
    const auto date=dateParts(header.timestamp);
    const std::string name(m_replayName.begin(),m_replayName.end());
    // 0x429C80 draws the insertion caret before the "No." line.
    ascii(d,"_",198+9*m_replayNameLength,128,0xA0FFFFC0);
    std::snprintf(text,sizeof(text),"No.%.2d %s %s-%s %.2d/%.2d %.2d:%.2d %6d",
                  m_replaySelection+1,name.c_str(),level,scene,
                  date[0],date[1],date[2],date[3],int(header.score));
    ascii(d,text,144,128);
    for(int row=0;row<6;++row){
        for(int col=0;col<16;++col){
            const int index=col+16*row;
            char glyph[2]={kReplayCharacters[index],0};
            if(row==5){
                if(col==13)glyph[0]=char(0x81);
                else if(col==14)glyph[0]=char(0x7F);
                else if(col==15)glyph[0]=char(0x80);
            }
            // 0x429C80 pulses the selected cell between 1.2x and 2.0x over a
            // 32-frame triangle and lifts it by (scale-1)*8 pixels. The glyph
            // origin also moves right by the scale value.
            if(index==m_replayNameCursor){
                const int phase=m_menuTicks%16;
                const float scale=(m_menuTicks%32)>=16
                    ?2.0f-float(phase)*0.8f/16.0f
                    :float(phase)*0.8f/16.0f+1.2f;
                const int glyphId=int(static_cast<unsigned char>(glyph[0]))-32;
                m_ascii.draw(d,glyphId,220+12*col+scale,320+16*row-(scale-1.0f)*8.0f,
                             0xFFFFFFC0u,scale,scale);
            }else ascii(d,glyph,220+12*col,320+16*row,0xC0C0C0C0u);
        }
    }
}
}
