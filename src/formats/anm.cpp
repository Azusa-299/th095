#include "anm.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "../log.hpp"
#include "../numeric/numeric.hpp"
#include "../vm_diagnostics.hpp"

namespace th95::format {
namespace num = th95::numeric;
namespace vm = th95::vm;
} // namespace th95::format

namespace th95::format {
namespace {
uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
float f32(const uint8_t *p) { const uint32_t bits=u32(p); float v; std::memcpy(&v,&bits,4); return v; }
}
int AnmInstruction::integer(std::size_t index) const { return index < args.size()/4 ? static_cast<int32_t>(u32(args.data()+index*4)) : 0; }
float AnmInstruction::number(std::size_t index) const { return index < args.size()/4 ? f32(args.data()+index*4) : 0; }

bool AnmTexture::load(Archive &archive, const std::string &name, IDirect3DDevice9 *device)
{
    const Entry *entry=archive.find(name);
    if (!entry) { log::print("error : missing %s\r\n",name.c_str()); return false; }
    const auto data=archive.extract(*entry);
    if(data.size()!=entry->size)return false;
    return loadData(data,name,device);
}
bool AnmTexture::loadData(const std::vector<uint8_t> &data,const std::string &name,IDirect3DDevice9 *device)
{
    m_pages.clear(); m_sprites.clear(); m_scripts.clear(); m_scriptOrder.clear();
    const auto fail=[&]() { log::print("error : invalid ANM %s\r\n",name.c_str());m_pages.clear();m_sprites.clear();m_scripts.clear();m_scriptOrder.clear();return false; };
    std::size_t base=0;
    for (int blocks=0;blocks<4096;++blocks) {
        if (base>data.size() || data.size()-base<64) return fail();
        const auto *p=data.data()+base;
        const uint32_t ns=u32(p),nt=u32(p+4),next=u32(p+56);
        const std::size_t span=next?next:data.size()-base;
        if (span<64 || span>data.size()-base || uint64_t(ns)*4+uint64_t(nt)*8+64>span || u32(p+40)!=4) return fail();
        Page page;
        page.width=static_cast<int>(u32(p+12));page.height=static_cast<int>(u32(p+16));
        if (page.width<=0 || page.height<=0 || page.width>8192 || page.height>8192) return fail();
        if (p[52]) {
            const uint32_t texture=u32(p+48);
            if (texture>span || span-texture<16 || std::memcmp(p+texture,"THTX",4)) return fail();
            const auto *header=p+texture;
            const unsigned format=u16(header+6),w=u16(header+8),h=u16(header+10);
            const uint32_t bytes=u32(header+12);
            // THTX format 3 is opaque R5G6B5 used by field backdrops and enemy
            // pages, while format 5 is A4R4G4B4 (bullet/text pages).  Both are
            // packed 16-bit pixels.  Decoding format 3 as 5:5:5 turns the olive
            // backdrop accents into saturated green, so the 6-bit green field
            // must be honoured here.
            const unsigned bpp=format==1?4:(format==3||format==5||format==7)?2:0;
            if (!bpp || !w || !h || w>8192 || h>8192 || bytes>span-texture-16 || uint64_t(w)*h*bpp>bytes) return fail();
            // The page header carries power-of-two dimensions while THTX may
            // store the artwork at another size: enm11 keeps 413x407 inside a
            // 256 page, enm24 468x468 inside 256 and enm20 256x256 inside 512.
            // The header is only the UV base (see draw); the texture itself is
            // uploaded at its stored size and sampled with D3DTADDRESS_WRAP.
            const std::size_t rowBytes=std::size_t(w)*bpp;
            const std::size_t srcStride=(h&&bytes/h>=rowBytes)?std::size_t(bytes)/h:rowBytes;
            std::vector<uint8_t> source(std::size_t(w)*h*4);
            const auto *src=header+16;
            for (unsigned y=0;y<h;++y) {
                const auto *row=src+std::size_t(y)*srcStride;
                for (unsigned x=0;x<w;++x) {
                    uint8_t r,g,b,a=255;
                    if (format==3) { const auto v=u16(row+x*2);b=(v&31)*255/31;g=((v>>5)&63)*255/63;r=((v>>11)&31)*255/31;a=255; }
                    else if (format==5) { const auto v=u16(row+x*2);b=(v&15)*17;g=((v>>4)&15)*17;r=((v>>8)&15)*17;a=((v>>12)&15)*17; }
                    else if (format==7) { b=g=r=row[x*2];a=row[x*2+1]; }
                    else { b=row[x*4];g=row[x*4+1];r=row[x*4+2];if(format==1)a=row[x*4+3]; }
                    source[(std::size_t(y)*w+x)*4]=b;
                    source[(std::size_t(y)*w+x)*4+1]=g;
                    source[(std::size_t(y)*w+x)*4+2]=r;
                    source[(std::size_t(y)*w+x)*4+3]=a;
                }
            }
            if (device && !page.texture.fromPixels(device,source.data(),w,h)) return fail();
        }
        const auto pageIndex=m_pages.size();
        for (uint32_t i=0;i<ns;++i) {
            const uint32_t off=u32(p+64+i*4);
            if(off>span || span-off<20) return fail();
            AnmSprite s;
            s.id=u32(p+off);s.u=f32(p+off+4);s.v=f32(p+off+8);s.w=f32(p+off+12);s.h=f32(p+off+16);s.page=pageIndex;
            if (!std::isfinite(s.u)||!std::isfinite(s.v)||!std::isfinite(s.w)||!std::isfinite(s.h)||s.w<=0||s.h<=0) return fail();
            m_sprites.push_back(s);
        }
        for(uint32_t i=0;i<nt;++i) {
            const auto *desc=p+64+ns*4+i*8;
            const uint32_t id=u32(desc),start=u32(desc+4);
            if(start>span || span-start<8 || m_scripts.count(static_cast<int>(id))) return fail();
            m_scriptOrder.push_back(static_cast<int>(id));
            auto &script=m_scripts[static_cast<int>(id)];
            // Some final scripts (front:18) end in delete directly before
            // THTX, with no 0xffff sentinel. Never decode texture pixels.
            std::size_t scriptEnd=span;
            if(p[52]&&u32(p+48)>start)scriptEnd=std::min(scriptEnd,std::size_t(u32(p+48)));
            for(uint32_t j=0;j<nt;++j) {
                const uint32_t other=u32(p+64+ns*4+j*8+4);
                if(other>start)scriptEnd=std::min(scriptEnd,std::size_t(other));
            }
            std::size_t cursor=start;
            bool terminated=false;
            while(cursor+8<=scriptEnd) {
                const auto *raw=p+cursor;
                const auto op=u16(raw),size=u16(raw+2);
                if(op==0xFFFF) {terminated=true;break;}
                if(size<8 || size>scriptEnd-cursor || script.size()>=100000) return fail();
                AnmInstruction instruction;
                instruction.opcode=op;instruction.time=static_cast<int16_t>(u16(raw+4));instruction.mask=u16(raw+6);instruction.offset=static_cast<uint32_t>(cursor-start);
                instruction.args.assign(raw+8,raw+size);script.push_back(std::move(instruction));cursor+=size;
            }
            if(cursor==scriptEnd&&!script.empty()&&script.back().opcode==1)terminated=true;
            if(!terminated) return fail();
        }
        log::print("info : %s page %zu %dx%d embedded=%u sprites=%u scripts=%u\r\n",name.c_str(),pageIndex,page.width,page.height,unsigned(p[52]),ns,nt);
        m_pages.push_back(std::move(page));
        if(!next) {log::print("info : %s complete: %zu pages, %zu sprites, %zu scripts\r\n",name.c_str(),m_pages.size(),m_sprites.size(),m_scripts.size());return true;}
        base+=next;
    }
    return fail();
}
const std::vector<AnmInstruction> *AnmTexture::script(int id) const { const auto it=m_scripts.find(id);return it==m_scripts.end()?nullptr:&it->second; }
const AnmSprite *AnmTexture::sprite(int id) const { for(const auto &s:m_sprites)if(s.id==static_cast<uint32_t>(id))return &s;return nullptr; }
bool AnmTexture::setSpriteSize(int id,float width,float height) {
    for(auto&s:m_sprites)if(s.id==static_cast<uint32_t>(id)){s.w=width;s.h=height;return true;}
    return false;
}
bool AnmTexture::bindExternalPage(std::size_t page,const AnmTexture *source,std::size_t sourcePage)
{
    if(page>=m_pages.size()||source==this)return false;
    m_pages[page].external=source;m_pages[page].externalPage=sourcePage;
    return true;
}
bool AnmTexture::bindExternalTexture(std::size_t page,const platform::Texture *source)
{
    if(page>=m_pages.size())return false;
    m_pages[page].externalTexture=source;return true;
}
void AnmTexture::draw(IDirect3DDevice9 *device,int id,float x,float y,DWORD tint,float sx,float sy,float su,float sv,bool additive,float rotation,bool opaqueTexture) const
{
    const auto *s=sprite(id);if(!s || s->page>=m_pages.size())return;
    const auto &page=m_pages[s->page];
    const Page *source=&page;
    if(page.external) {
        if(page.externalPage>=page.external->m_pages.size())return;
        source=&page.external->m_pages[page.externalPage];
    }
    auto *texture=page.externalTexture?page.externalTexture->get():source->texture.get();
    if(!texture)return;
    const float iw=1.0f/page.width,ih=1.0f/page.height;
    const float qw=s->w*sx,qh=s->h*sy;
    // TH095 draws the spell-declaration streak with a 16x768 sprite scaled to
    // hundreds of thousands of pixels. Direct3D 9 rasterizers drop huge
    // pre-transformed quads, so subdivide long dimensions into screen-sized
    // segments. Each segment keeps the parent's rotation center and its slice
    // of the UV range, which preserves the affine mapping exactly.
    {
        constexpr float kMaxSegment=1024.0f;
        const int nx=std::max(1,int(std::ceil(qw/kMaxSegment)));
        const int ny=std::max(1,int(std::ceil(qh/kMaxSegment)));
        if(nx>1||ny>1){
            const float cx=x+qw*0.5f,cy=y+qh*0.5f;
            const float du=((s->u+s->w)*iw+su-(s->u*iw+su))/float(nx);
            const float dv=((s->v+s->h)*ih+sv-(s->v*ih+sv))/float(ny);
            for(int iy=0;iy<ny;++iy)for(int ix=0;ix<nx;++ix){
                const float x0=x+qw*float(ix)/float(nx),x1=x+qw*float(ix+1)/float(nx);
                const float y0=y+qh*float(iy)/float(ny),y1=y+qh*float(iy+1)/float(ny);
                platform::drawQuadSegment(device,texture,x0,y0,x1,y1,cx,cy,rotation,tint,
                    s->u*iw+su+du*float(ix),s->v*ih+sv+dv*float(iy),
                    s->u*iw+su+du*float(ix+1),s->v*ih+sv+dv*float(iy+1),additive);
            }
            return;
        }
    }
    platform::drawQuad(device,texture,x,y,qw,qh,tint,s->u*iw+su,s->v*ih+sv,(s->u+s->w)*iw+su,(s->v+s->h)*ih+sv,additive,rotation,opaqueTexture);
}

void AnmTexture::drawPage(IDirect3DDevice9 *device,std::size_t page,float x,float y,DWORD tint,float sx,float sy) const
{
    if(page>=m_pages.size())return;
    const auto &p=m_pages[page];if(!p.texture.get())return;
    platform::drawQuad(device,p.texture.get(),x,y,float(p.width)*sx,float(p.height)*sy,tint);
}
int AnmTexture::pageWidth(std::size_t page) const {return page<m_pages.size()?m_pages[page].width:0;}
int AnmTexture::pageHeight(std::size_t page) const {return page<m_pages.size()?m_pages[page].height:0;}
IDirect3DTexture9 *AnmTexture::pageTexture(std::size_t page) const {
    if(page>=m_pages.size())return nullptr;
    const auto&p=m_pages[page];
    if(p.external)return p.external->pageTexture(p.externalPage);
    return p.externalTexture?p.externalTexture->get():p.texture.get();
}

bool AnmAnimation::start(const AnmTexture &file,int id,float x,float y,uint32_t seed)
{
    *this=AnmAnimation{};m_file=&file;m_script=file.script(id);m_scriptId=id;m_offsetX=x;m_offsetY=y;m_rng=seed?seed:1;
    m_alive=m_script&&!m_script->empty();if(m_alive)update();return m_alive;
}
uint32_t AnmAnimation::random()
{
    // The retail helpers rotate the 16-bit transformed seed twice per call.
    const auto next=[&]() {m_rng=num::rotateSeed16(m_rng);return uint32_t(m_rng);};
    const uint32_t high=next();
    const uint32_t low=next();
    return num::combineSeeds(high,low);
}
int *AnmAnimation::intVariable(int id)
{
    if(id>=10000&&id<=10003)return &m_intVariables[id-10000];
    if(id>=10008&&id<=10009)return &m_intVariables[id-10004];
    // Unknown integer slots are a no-op in the retail VM. Record the id so
    // coverage can be measured instead of stopping the animation.
    vm::sink().record(vm::Fault::UnsupportedVariable,static_cast<int>(vm::Machine::Anm),
                      m_scriptId,0,0,id);
    return nullptr;
}
int AnmAnimation::intValue(const AnmInstruction &instruction,std::size_t n) const
{
    const int id=instruction.integer(n);
    if(!(instruction.mask&(1u<<n)))return id;
    if(id>=10000&&id<=10003)return m_intVariables[id-10000];
    if(id>=10004&&id<=10007)return num::truncToInt(m_variables[id-10004]);
    if(id>=10008&&id<=10009)return m_intVariables[id-10004];
    vm::sink().record(vm::Fault::UnsupportedVariable,static_cast<int>(vm::Machine::Anm),
                      m_scriptId,instruction.opcode,instruction.offset,id);
    return id;
}
float AnmAnimation::value(const AnmInstruction &instruction,std::size_t n)
{
    const float f=instruction.number(n);if(!(instruction.mask&(1u<<n)))return f;
    const int id=num::truncToInt(f);
    if(id>=10004&&id<=10007)return m_variables[id-10004];
    if(id<10010||id>10012){
        vm::sink().record(vm::Fault::UnsupportedVariable,static_cast<int>(vm::Machine::Anm),
                          m_scriptId,instruction.opcode,instruction.offset,id);
        return f;
    }
    const float unit=num::unitFromUint32(random());
    const float signedUnit=num::mulAdd(unit,2.0f,-1.0f);
    if(id==10011)return unit;
    if(id==10012)return signedUnit;
    if(id==10010)return num::mul(signedUnit,3.14159265f);
    return f;
}
void AnmAnimation::tween(int channel,const std::array<float,3> &to,int duration,int mode)
{
    auto &t=m_tweens[channel];t.from=channel==0?std::array<float,3>{{m_x,m_y,0}}:channel==1?m_rgb:channel==2?std::array<float,3>{{m_alpha,0,0}}:channel==3?std::array<float,3>{{m_scaleX,m_scaleY,0}}:std::array<float,3>{{0,0,m_rotation}};
    t.to=to;t.duration=std::max(1,duration);t.elapsed=0;t.mode=mode;
}
void AnmAnimation::interrupt(int label) {m_pending=label;}
void AnmAnimation::advance(float speed, bool scriptTime)
{
    speed=num::max(0.0f,speed);
    for(int c=0;c<5;++c) {
        auto &t=m_tweens[c];if(t.elapsed>=t.duration)continue;
        t.elapsed=num::min(t.duration,num::add(t.elapsed,speed));
        float a=t.duration>0?num::div(t.elapsed,t.duration):1.0f;
        if(t.mode>=1&&t.mode<=3)a=num::pow(a,float(t.mode+1));
        else if(t.mode>=4&&t.mode<=6)a=num::sub(1.0f,num::pow(num::sub(1.0f,a),float(t.mode-2)));
        std::array<float,3> v;
        for(int j=0;j<3;++j)v[j]=num::lerp(t.from[j],t.to[j],a);
        if(c==0){m_x=v[0];m_y=v[1];}else if(c==1)m_rgb=v;else if(c==2)m_alpha=v[0];else if(c==3){m_scaleX=v[0];m_scaleY=v[1];}else m_rotation=v[2];
    }
    m_scrollU=num::mulAdd(m_velocityU,speed,m_scrollU);
    m_scrollV=num::mulAdd(m_velocityV,speed,m_scrollV);
    // Angular velocity is radians per logical tick.
    m_rotation=num::remainder(num::mulAdd(m_rotationVelocity,speed,m_rotation),6.28318530718f);
    m_scrollU=num::sub(m_scrollU,num::floorValue(m_scrollU));
    m_scrollV=num::sub(m_scrollV,num::floorValue(m_scrollV));
    if(scriptTime&&!m_stopped)m_time=num::add(m_time,speed);
}
void AnmAnimation::update(float speed)
{
    if(!m_alive||!m_script)return;
    speed=num::max(0.0f,speed);
    bool advanced=false;
    // Opcode 75 stalls the instruction stream, not the VM's
    // interpolation/rotation timers. Death fragments move during this wait.
    if(m_wait>0){
        m_wait=num::max(0.0f,num::sub(m_wait,speed));
        advance(speed,false);advanced=true;
        if(m_wait>0)return;
    }
    if(m_pending) {
        bool found=false;
        for(std::size_t i=0;i<m_script->size();++i)if((*m_script)[i].opcode==64&&(*m_script)[i].integer(0)==m_pending){
            // retail routine saves the interrupted instruction and timer. Opcode
            // 81 returns there after the page's slide-in branch.
            m_returnCursor=m_cursor;m_returnTime=m_time;m_returnStopped=m_stopped;m_hasReturn=true;
            m_cursor=i+1;m_time=m_cursor<m_script->size()?(*m_script)[m_cursor].time:0;found=true;break;}
        m_pending=0;if(found){m_stopped=false;m_visible=true;}
    }
    for(int budget=0;!m_stopped&&m_cursor<m_script->size();++budget) {
        if(budget>=2048){
            vm::sink().record(vm::Fault::BudgetExhausted,static_cast<int>(vm::Machine::Anm),
                              m_scriptId,0,0,0);
            m_alive=false;return;
        }
        const auto &i=(*m_script)[m_cursor];if(i.time>m_time)break;
        const auto v=[&](std::size_t n){return value(i,n);};
        const auto jump=[&](uint32_t target,int time){
            const auto it=std::find_if(m_script->begin(),m_script->end(),[&](const auto &n){return n.offset==target;});
            if(it==m_script->end()){
                vm::sink().record(vm::Fault::UnresolvedJump,static_cast<int>(vm::Machine::Anm),
                                  m_scriptId,i.opcode,i.offset,int(target));
                m_alive=false;return false;
            }
            m_cursor=std::size_t(it-m_script->begin());m_time=time;return true;
        };
        switch(i.opcode) {
        case 0:break;
        case 1:m_visible=false;m_alive=false;return;
        case 2:m_stopped=true;break;
        case 3:m_sprite=i.integer(0);m_visible=true;break;
        case 4:if(!jump(uint32_t(i.integer(0)),i.integer(1)))return;continue;
        case 5:{auto *counter=intVariable(i.integer(0));if(!counter)break;if(--*counter>0){if(!jump(uint32_t(i.integer(1)),i.integer(2)))return;continue;}break;}
        case 6:{auto *target=intVariable(i.integer(0));if(target)*target=intValue(i,1);break;}
        case 7:{const int n=num::truncToInt(i.number(0))-10004;if(n>=0&&n<4)m_variables[n]=v(1);break;}
        case 8:{auto *target=intVariable(i.integer(0));if(target)*target=num::wrapAdd(*target,intValue(i,1));break;}
        case 9:{const int n=num::truncToInt(i.number(0))-10004;if(n>=0&&n<4)m_variables[n]=num::add(m_variables[n],v(1));break;}
        case 23:{const int n=num::truncToInt(i.number(0))-10004;if(n>=0&&n<4)m_variables[n]=num::mul(v(1),v(2));break;}
        case 13:{const int n=num::truncToInt(i.number(0))-10004;if(n>=0&&n<4)m_variables[n]=num::mul(m_variables[n],v(1));break;}
        case 42:{const int n=num::truncToInt(i.number(0))-10004;if(n>=0&&n<4)m_variables[n]=num::sin(v(1));break;}
        case 43:{const int n=num::truncToInt(i.number(0))-10004;if(n>=0&&n<4)m_variables[n]=num::cos(v(1));break;}
        case 40:{auto *target=intVariable(i.integer(0));const int bound=intValue(i,1);if(target)*target=bound>0?int(random()%uint32_t(bound)):0;break;}
        case 25:{
            // Retail opcode 0x19 is float division. The destination is a
            // float variable; operand masks 0x2/0x4 resolve the two inputs.
            const int n=num::truncToInt(i.number(0))-10004;
            if(n>=0&&n<4)m_variables[n]=num::div(v(1),v(2));
            break;
        }
        case 30:
            // Opcode 0x1e continues when the integer operands are equal and
            // jumps to (offset,time) in the other case.
            if(intValue(i,0)!=intValue(i,1)){
                if(!jump(uint32_t(i.integer(2)),i.integer(3)))return;
                continue;
            }
            break;
        case 48:m_x=v(0);m_y=v(1);break;
        case 49:m_rotation=v(2);break;
        case 50:m_scaleX=v(0);m_scaleY=v(1);break;
        case 51:m_alpha=float(i.integer(0));break;
        case 52:m_rgb={{float(i.integer(0)),float(i.integer(1)),float(i.integer(2))}};break;
        case 53:m_rotationVelocity=v(2);break;
        case 56:tween(0,{{v(2),v(3),0}},intValue(i,0),intValue(i,1));break;
        case 57:tween(1,{{float(intValue(i,2)),float(intValue(i,3)),float(intValue(i,4))}},intValue(i,0),intValue(i,1));break;
        case 58:tween(2,{{float(intValue(i,2)),0,0}},intValue(i,0),intValue(i,1));break;
        case 59:tween(4,{{v(2),v(3),v(4)}},intValue(i,0),intValue(i,1));break;
        case 60:tween(3,{{v(2),v(3),0}},intValue(i,0),intValue(i,1));break;
        case 61:
            // retail routine opcode 0x3d: reverse the sprite's horizontal
            // direction.  Retail enemy turn scripts use this as the first
            // instruction of their left-facing animation.  The original
            // toggles the horizontal transform and marks the visual state
            // dirty; a negative X scale preserves the same centered anchor
            // while reversing the texture UVs in the D3D quad.
            m_scaleX=num::neg(m_scaleX);
            break;
        case 63:m_stopped=true;break;
        case 64:break;
        case 65:m_anchorX=i.integer(0)&3;m_anchorY=(i.integer(0)>>16)&3;break;
        case 66:m_additive=i.integer(0)==1;break;
        case 67:break;
        case 68:m_layer=i.integer(0);break;
        case 69:
            // 0x45 clears the visible flag and leaves the script available
            // for a later interrupt/continuation. Text/title resources use
            // this as a legal end-visible marker before their final delete.
            m_visible=false;
            break;
        case 72:m_visible=(i.integer(0)&1)!=0;break;
        case 73:
            m_flags=(m_flags&~0x800u)|((i.integer(0)&1)?0x800u:0u);
            break;
        case 70:m_velocityU=v(0);break;
        case 71:m_velocityV=v(0);break;
        case 84:
            // 0x54 prepares the retail polygon path. Sprite drawing uses a
            // textured quad, but retaining its state keeps warning scripts
            // valid and avoids terminating them as unsupported.
            m_flags=(m_flags&0xFC3FFFFFu)|0x02400000u;
            m_vertexCount=std::max(0,intValue(i,0));
            break;
        case 75:m_wait=std::max(0.0f,float(intValue(i,0)));++m_cursor;if(m_wait>0){advance(speed,false);return;}continue;
        case 81:
            if(!m_hasReturn){m_alive=false;return;}
            m_cursor=m_returnCursor;m_time=m_returnTime;m_stopped=m_returnStopped;m_hasReturn=false;
            continue;
        case 83:m_x=num::add(m_x,m_offsetX);m_y=num::add(m_y,m_offsetY);m_offsetX=m_offsetY=0;break;
        case 82:case 85:case 86:case 87:break;
        default:
            // Retail treats an unknown operation as a no-op. Record it and
            // keep the instruction stream moving so the animation survives.
            vm::sink().record(vm::Fault::UnsupportedInstruction,static_cast<int>(vm::Machine::Anm),
                              m_scriptId,i.opcode,i.offset,0);
            break;
        }
        if(!m_stopped)++m_cursor;
    }
    if(!advanced)advance(speed);
    else if(!m_stopped)m_time=num::add(m_time,speed);
}
void AnmAnimation::draw(IDirect3DDevice9 *device,DWORD modulation) const
{
    drawInternal(device,modulation,false);
}
void AnmAnimation::drawOpaque(IDirect3DDevice9 *device,DWORD modulation) const
{
    drawInternal(device,modulation,true);
}
void AnmAnimation::drawInternal(IDirect3DDevice9 *device,DWORD modulation,bool opaqueTexture) const
{
    if(!m_enabled||!m_alive||!m_visible||!m_file||m_alpha<=0)return;
    const auto *s=m_file->sprite(m_sprite);if(!s)return;
    const auto byte=[](float v){return DWORD(std::clamp(v,0.0f,255.0f));};
    // retail routine modulates each channel with 128 as unity, saturating at 255.
    const DWORD tint=byte(float((modulation>>24)&255)*m_alpha/128)<<24|
        byte(float((modulation>>16)&255)*m_rgb[0]/128)<<16|
        byte(float((modulation>>8)&255)*m_rgb[1]/128)<<8|
        byte(float(modulation&255)*m_rgb[2]/128);
    float x=m_x+m_offsetX,y=m_y+m_offsetY;
    if(m_vertexCount>=2&&((m_flags>>22)&0xFu)==9u){
        // Retail polygon mode 9 (opcode 84) builds a closed band: scaleY is the
        // ring radius, scaleX the band width, and the texture scrolls along the
        // circumference. The declaration backdrop (effect 210) is a film ring.
        // Retail sub_43A600 takes the segment count from integer variable
        // 10000 (+280) and the V span from variable 10001 (+284); the V value
        // is handed to D3D unnormalised, so the page texture wraps once per
        // page height. (The old 1/pageHeight factor wrongly compressed it.)
        const int points=m_intVariables[0];
        const int segments=points>1?points-1:m_vertexCount-1;
        const int vSpan=m_intVariables[1];
        if(segments<1)return;
        const float iw=1.0f/float(m_file->pageWidth(s->page));
        const float u0=float(s->u)*iw+m_scrollU,u1=float(s->u+s->w)*iw+m_scrollU;
        const float inner=m_scaleY-m_scaleX*0.5f,outer=m_scaleY+m_scaleX*0.5f;
        std::vector<platform::TriangleVertex> strip(std::size_t(segments+1)*2);
        for(int k=0;k<=segments;++k){
            const float a=m_rotation+6.28318530718f*float(k)/float(segments);
            const float c=std::cos(a),sn=std::sin(a);
            const float vv=float(vSpan)*float(k)/float(segments)+m_scrollV;
            strip[std::size_t(k)*2]={x+c*inner,y+sn*inner,u0,vv};
            strip[std::size_t(k)*2+1]={x+c*outer,y+sn*outer,u1,vv};
        }
        platform::drawTriangleStrip(device,m_file->pageTexture(s->page),strip.data(),int(strip.size()),tint,m_additive);
        return;
    }
    if(m_anchorX==0)x-=s->w*m_scaleX*0.5f;else if(m_anchorX==2)x-=s->w*m_scaleX;
    if(m_anchorY==0)y-=s->h*m_scaleY*0.5f;else if(m_anchorY==2)y-=s->h*m_scaleY;
    m_file->draw(device,m_sprite,x,y,tint,m_scaleX,m_scaleY,m_scrollU,m_scrollV,m_additive,m_rotation,opaqueTexture);
}
} // namespace th95::format
