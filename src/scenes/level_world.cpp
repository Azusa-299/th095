#include "level_world.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace th95::scene {
namespace {
uint16_t u16(const std::vector<uint8_t>&d,std::size_t p){return d[p]|uint16_t(d[p+1])<<8;}
uint32_t u32(const std::vector<uint8_t>&d,std::size_t p){uint32_t v;std::memcpy(&v,d.data()+p,4);return v;}
float f32(const std::vector<uint8_t>&d,std::size_t p){float v;std::memcpy(&v,d.data()+p,4);return v;}
// Segment headers carry a bounding box after the two 16-bit ids: three centre
// offsets at +4/+8/+12 and three extents at +16/+20/+24.
float headerFloat(const format::StdSegment&s,int index){
    const std::size_t p=4+std::size_t(index)*4;
    return p+4<=s.header.size()?f32(s.header,p):0.0f;
}
using Vec=std::array<float,3>;
Vec sub(Vec a,Vec b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
float dot(Vec a,Vec b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec cross(Vec a,Vec b){return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};}
Vec normalized(Vec a){const float length=std::sqrt(dot(a,a));for(auto&v:a)v/=length;return a;}
// 0x4A869520 as stored by the camera setup: squared draw distance (about 2100).
constexpr float kDrawDistanceSquared=4410000.0f;
}
bool LevelWorld::load(Archive&a,IDirect3DDevice9*d,const char*stdName){
    std::string stdFile=stdName?stdName:"world01.std";
    std::string anmFile=stdFile;
    const auto dot=anmFile.rfind('.');
    if(dot!=std::string::npos)anmFile.replace(dot,std::string::npos,".anm");
    const auto*entry=a.find(stdFile.c_str());if(!entry)return false;const auto data=a.extract(*entry);
    if(!m_model.parse(data)||!m_texture.load(a,anmFile.c_str(),d))return false;
    m_instances.clear();m_script.clear();m_renderType.clear();
    for(std::size_t p=u32(data,4);p+16<=data.size();p+=16){const auto object=u16(data,p);if(object==65535)break;if(object>=m_model.segments.size())return false;m_instances.push_back({object,{{f32(data,p+4),f32(data,p+8),f32(data,p+12)}}});}
    for(std::size_t p=u32(data,8);p+8<=data.size();){
        if(u32(data,p)==UINT32_MAX)break;
        const auto size=u16(data,p+6);if(size<8||p+size>data.size())return false;
        format::EclCommand c;c.time=u32(data,p);c.opcode=u16(data,p+4);c.offset=uint32_t(p-u32(data,8));c.args.assign(data.begin()+p+8,data.begin()+p+size);m_script.push_back(c);p+=size;
    }
    for(int k=0;k<int(m_texture.scriptCount());++k){
        int type=0;
        if(const auto*s=m_texture.script(k))for(const auto&ins:*s)if(ins.opcode==67){type=int(ins.integer(0))&0xF;break;}
        m_renderType.push_back(type);
    }
    restart();return !m_instances.empty()&&!m_script.empty();
}
void LevelWorld::restart(){m_pc=0;m_clock=0;m_eye={{0,0,-100}};m_facing={{0,270,600}};m_up={{0,1,0}};m_fov=.5235988f;m_fogStart=240;m_fogEnd=800;m_eyeTween={};m_facingTween={};m_fogTween={};}
void LevelWorld::update(float scale){
    const auto vec=[](const auto&c,int n){return Vec{{c.number(n),c.number(n+1),c.number(n+2)}};};
    for(int budget=0;budget<128&&m_pc<m_script.size();++budget){
        const auto&c=m_script[m_pc];if(c.time>std::floor(m_clock))break;++m_pc;
        switch(c.opcode){
        case 1:{const auto i=std::find_if(m_script.begin(),m_script.end(),[&](const auto&v){return int(v.offset)==c.integer(0);});if(i==m_script.end())return;m_pc=std::size_t(i-m_script.begin());m_clock=float(c.integer(1));break;}
        case 2:m_eye=vec(c,0);break;case 4:m_facing=vec(c,0);break;case 6:m_up=vec(c,0);break;
        case 3:m_eyeTween={m_eye,vec(c,2),0,float(c.integer(0)),c.integer(1)};break;
        case 5:m_facingTween={m_facing,vec(c,2),0,float(c.integer(0)),c.integer(1)};break;
        case 7:m_fov=c.number(0);break;
        case 8:m_fogStart=c.number(1);m_fogEnd=c.number(2);break;
        case 9:m_fogTween={{{m_fogStart,m_fogEnd,0}},{{c.number(3),c.number(4),0}},0,float(c.integer(0)),c.integer(1)};break;
        default:break;
        }
    }
    const auto tween=[&](Tween&t,Vec&value){if(t.duration<=0)return;t.time+=scale;float u=std::min(1.0f,t.time/t.duration);if(t.mode==1)u*=u;for(int i=0;i<3;++i)value[i]=t.from[i]+(t.to[i]-t.from[i])*u;};
    tween(m_eyeTween,m_eye);tween(m_facingTween,m_facing);Vec fog{{m_fogStart,m_fogEnd,0}};tween(m_fogTween,fog);m_fogStart=fog[0];m_fogEnd=fog[1];m_clock+=scale;
}
void LevelWorld::draw(IDirect3DDevice9*d,DWORD modulation) const{
    if(!d)return;
    // retail routine: view = LookAtLH(eye, eye+facing, up); op4/op5 carry a
    // direction vector (normalized each frame at retail address), not a target
    // point. Projection is PerspectiveFovLH(fov, 384/448, 30, 1800).
    // Everything is projected on the CPU into viewport space (128,16,384,448)
    // and drawn as XYZRHW quads, mirroring the original's CPU-projection path.
    const Vec z=normalized(m_facing),x=normalized(cross(m_up,z)),y=cross(z,x);
    D3DMATRIX view{};for(int i=0;i<3;++i){view.m[i][0]=x[i];view.m[i][1]=y[i];view.m[i][2]=z[i];}view._41=-dot(x,m_eye);view._42=-dot(y,m_eye);view._43=-dot(z,m_eye);view._44=1;
    D3DMATRIX projection{};const float sy=1/std::tan(m_fov/2);projection._11=sy/(384.0f/448);projection._22=sy;projection._33=1800.0f/1770;projection._34=1;projection._43=-54000.0f/1770;
    D3DVIEWPORT9 saved{};d->GetViewport(&saved);const D3DVIEWPORT9 viewport{128,16,384,448,0,1};d->SetViewport(&viewport);
    d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    struct Vertex {float x,y,z,rhw;DWORD color;float u,v;};
    const auto project=[&](const Vec&p,Vertex&out)->bool{
        const float vx=p[0]*view.m[0][0]+p[1]*view.m[1][0]+p[2]*view.m[2][0]+view._41;
        const float vy=p[0]*view.m[0][1]+p[1]*view.m[1][1]+p[2]*view.m[2][1]+view._42;
        const float vz=p[0]*view.m[0][2]+p[1]*view.m[1][2]+p[2]*view.m[2][2]+view._43;
        if(vz<=0.0f)return false;
        out.x=128+((vx*projection._11)/vz+1)*192;
        out.y=16+(1-(vy*projection._22)/vz)*224;
        out.z=(vz*projection._33+projection._43)/vz;
        out.rhw=1/vz;
        return true;
    };
    // The original renders the world in per-layer passes (retail routine(0..3)):
    // every layer-0 ground quad is drawn before any tree quad, so ground
    // slices further ahead never overdraw nearer trees. Keep that order.
    struct Item {int layer;std::size_t instance;std::size_t record;};
    std::vector<Item> items;
    for(std::size_t i=0;i<m_instances.size();++i) {
        const auto&segment=m_model.segments[m_instances[i].object];
        // 0x4031A0 culls the whole instance before any of its records are
        // emitted: the segment box centre is folded into the instance position,
        // then rejected when it is farther than the draw limit or when its depth
        // along the view direction leaves [80, 1280 + radius]. Retail drops the
        // entire world this way while the STD camera looks up and forward, which
        // is why the field under a declaration card shows only the enm layers.
        const Vec centre{{m_instances[i].position[0]+headerFloat(segment,0),
                          m_instances[i].position[1]+headerFloat(segment,1),
                          m_instances[i].position[2]+headerFloat(segment,2)}};
        const Vec extent{{headerFloat(segment,3),headerFloat(segment,4),headerFloat(segment,5)}};
        const Vec offset=sub(centre,m_eye);
        if(dot(offset,offset)>kDrawDistanceSquared)continue;
        const float depth=dot(offset,z);
        if(depth>std::sqrt(dot(extent,extent))/2+1280.0f||depth<80.0f)continue;
        const int layer=segment.header.size()>2?int(segment.header[2]):0;
        for(std::size_t r=0;r<segment.records.size();++r)items.push_back({layer,i,r});
    }
    std::stable_sort(items.begin(),items.end(),[](const Item&a,const Item&b){return a.layer<b.layer;});
    for(const auto&item:items) {
        const auto&i=m_instances[item.instance];
        const auto&r=m_model.segments[i.object].records[item.record];
        const auto*s=m_texture.sprite(int(r.type));if(!s)continue;
        const Vec center{{i.position[0]+r.x,i.position[1]+r.y,i.position[2]+r.z}};
        const float distance=std::sqrt(dot(sub(center,m_eye),sub(center,m_eye)));
        if(distance>=m_fogEnd)continue;
        const unsigned shade=unsigned(255*(1-std::clamp((distance-m_fogStart)/(m_fogEnd-m_fogStart),0.0f,1.0f)));
        DWORD color=0;
        for(int shift=0;shift<32;shift+=8)color|=std::min<DWORD>(255,shade*((modulation>>shift)&255)/128)<<shift;
        const float u=s->u/m_texture.pageWidth(s->page),v=s->v/m_texture.pageHeight(s->page),du=s->w/m_texture.pageWidth(s->page),dv=s->h/m_texture.pageHeight(s->page);
        const int renderType=int(r.type)<int(m_renderType.size())?m_renderType[size_t(r.type)]:0;
        Vertex quad[4];
        if(renderType==8){
            // retail routine unit quad spans [-150,+140], scaled by record/256 in
            // the ground world matrix (retail routine).
            const float sx=r.width/256,sz=r.height/256;
            const float left=center[0]-150*sx,right=center[0]+140*sx,top=center[1]+140*sz,bottom=center[1]-150*sz;
            if(!project({{left,top,center[2]}},quad[0])||!project({{right,top,center[2]}},quad[1])||
               !project({{left,bottom,center[2]}},quad[2])||!project({{right,bottom,center[2]}},quad[3]))continue;
        }else{
            // Billboard (retail routines): screen size is the projected
            // length of one world unit along the camera right axis times
            // record size / 2, centered on the projected record position.
            Vertex c0,c1;
            if(!project(center,c0)||!project({{center[0]+x[0],center[1]+x[1],center[2]+x[2]}},c1))continue;
            const float dx=c1.x-c0.x,dy=c1.y-c0.y,dz=c1.z-c0.z;
            const float unit=std::sqrt(dx*dx+dy*dy+dz*dz);
            const float hw=unit*r.width/4,hh=unit*r.height/4;
            quad[0]={c0.x-hw,c0.y-hh,c0.z,c0.rhw,0,0,0};
            quad[1]={c0.x+hw,c0.y-hh,c0.z,c0.rhw,0,0,0};
            quad[2]={c0.x-hw,c0.y+hh,c0.z,c0.rhw,0,0,0};
            quad[3]={c0.x+hw,c0.y+hh,c0.z,c0.rhw,0,0,0};
        }
        const float us[]={u,u+du,u,u+du},vs[]={v,v,v+dv,v+dv};
        for(int k=0;k<4;++k){quad[k].color=color;quad[k].u=us[k];quad[k].v=vs[k];}
        d->SetTexture(0,m_texture.pageTexture(s->page));d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
    }
    d->SetTexture(0,nullptr);d->SetViewport(&saved);
}
}
