#include "gfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <objidl.h>
#include <gdiplus.h>

#include "log.hpp"

namespace th95::platform {
namespace {

ULONG_PTR g_gdiplusToken = 0;
unsigned g_deviceGeneration = 0;

struct Vertex {
    float x;
    float y;
    float z;
    float rhw;
    DWORD color;
    float u;
    float v;
};

bool uploadBitmap(IDirect3DDevice9 *device, Gdiplus::Bitmap &bitmap, IDirect3DTexture9 **out,
                  int *outWidth, int *outHeight)
{
    const int w = static_cast<int>(bitmap.GetWidth());
    const int h = static_cast<int>(bitmap.GetHeight());
    if (w <= 0 || h <= 0) {
        return false;
    }

    Gdiplus::Bitmap argb(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&argb);
        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        g.DrawImage(&bitmap, 0, 0, w, h);
    }

    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData locked{};
    if (argb.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &locked) !=
        Gdiplus::Ok) {
        return false;
    }

    IDirect3DTexture9 *texture = nullptr;
    if (FAILED(device->CreateTexture(static_cast<UINT>(w), static_cast<UINT>(h), 1, 0,
                                     D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr))) {
        argb.UnlockBits(&locked);
        return false;
    }

    D3DLOCKED_RECT dst{};
    if (SUCCEEDED(texture->LockRect(0, &dst, nullptr, 0))) {
        for (int y = 0; y < h; ++y) {
            const auto *srcRow = static_cast<const uint8_t *>(locked.Scan0) +
                                 static_cast<ptrdiff_t>(y) * locked.Stride;
            auto *dstRow = static_cast<uint8_t *>(dst.pBits) + static_cast<ptrdiff_t>(y) * dst.Pitch;
            std::copy(srcRow, srcRow + static_cast<ptrdiff_t>(w) * 4, dstRow);
        }
        texture->UnlockRect(0);
    }

    argb.UnlockBits(&locked);

    *out = texture;
    *outWidth = w;
    *outHeight = h;
    return true;
}

} // namespace

bool gfxInit()
{
    if (g_gdiplusToken) {
        return true;
    }
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) != Gdiplus::Ok) {
        g_gdiplusToken = 0;
        return false;
    }
    return true;
}

void gfxShutdown()
{
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

unsigned gfxDeviceGeneration()
{
    return g_deviceGeneration;
}

void gfxNoteDeviceReset()
{
    ++g_deviceGeneration;
}

Texture::~Texture()
{
    if (m_texture) {
        m_texture->Release();
        m_texture = nullptr;
    }
}

bool Texture::renderTarget(IDirect3DDevice9 *device,int width,int height)
{
    if(!device)return false;
    const unsigned generation=gfxDeviceGeneration();
    if(m_texture&&m_generation==generation&&m_width==width&&m_height==height)return true;
    // A Reset invalidated the previous default-pool surface; drop the handle
    // before asking for a fresh one.
    if(m_texture){m_texture->Release();m_texture=nullptr;}
    IDirect3DTexture9 *texture=nullptr;
    if(FAILED(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_X8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr)))return false;
    m_texture=texture;m_width=width;m_height=height;m_generation=generation;return true;
}

bool Texture::readPixels(IDirect3DDevice9 *device, int width, int height,
                         std::vector<uint8_t> &bgra) const
{
    bgra.clear();
    if (!device || !m_texture || width <= 0 || height <= 0 || width > m_width ||
        height > m_height) {
        return false;
    }
    IDirect3DSurface9 *source = nullptr;
    if (FAILED(m_texture->GetSurfaceLevel(0, &source))) {
        return false;
    }
    IDirect3DSurface9 *staging = nullptr;
    const auto created = device->CreateOffscreenPlainSurface(
        UINT(m_width), UINT(m_height), D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &staging, nullptr);
    bool ok = false;
    if (SUCCEEDED(created) && SUCCEEDED(device->GetRenderTargetData(source, staging))) {
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(staging->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            bgra.resize(std::size_t(width) * std::size_t(height) * 4);
            for (int y = 0; y < height; ++y) {
                const auto *src = static_cast<const uint8_t *>(locked.pBits) +
                                  static_cast<ptrdiff_t>(y) * locked.Pitch;
                auto *dst = bgra.data() + static_cast<std::size_t>(y) * std::size_t(width) * 4;
                std::copy(src, src + static_cast<ptrdiff_t>(width) * 4, dst);
            }
            staging->UnlockRect();
            ok = true;
        }
    }
    if (staging) {
        staging->Release();
    }
    source->Release();
    return ok;
}

bool Texture::fromMemory(IDirect3DDevice9 *device, const void *data, std::size_t size)
{
    if (!device || !data || size == 0 || !g_gdiplusToken) {
        return false;
    }

    bool ok = false;
    IStream *stream = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        ULONG written = 0;
        stream->Write(data, static_cast<ULONG>(size), &written);
        LARGE_INTEGER zero{};
        stream->Seek(zero, STREAM_SEEK_SET, nullptr);

        Gdiplus::Bitmap source(stream, FALSE);
        if (source.GetLastStatus() == Gdiplus::Ok) {
            ok = uploadBitmap(device, source, &m_texture, &m_width, &m_height);
        }
        stream->Release();
    }

    if (!ok) {
        log::print("error : image decode failed (%zu bytes)\r\n", size);
    }
    return ok;
}

bool Texture::fromPixels(IDirect3DDevice9 *device, const void *pixels, int width, int height)
{
    if (!device || !pixels || width <= 0 || height <= 0) {
        return false;
    }
    if (FAILED(device->CreateTexture(static_cast<UINT>(width), static_cast<UINT>(height), 1, 0,
                                     D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_texture, nullptr))) {
        return false;
    }

    D3DLOCKED_RECT locked{};
    if (FAILED(m_texture->LockRect(0, &locked, nullptr, 0))) {
        m_texture->Release();
        m_texture = nullptr;
        return false;
    }
    const auto *src = static_cast<const uint8_t *>(pixels);
    for (int y = 0; y < height; ++y) {
        auto *dst = static_cast<uint8_t *>(locked.pBits) + static_cast<ptrdiff_t>(y) * locked.Pitch;
        std::copy(src + static_cast<ptrdiff_t>(y) * width * 4,
                  src + static_cast<ptrdiff_t>(y + 1) * width * 4, dst);
    }
    m_texture->UnlockRect(0);
    m_width = width;
    m_height = height;
    return true;
}

bool Texture::fromText(IDirect3DDevice9 *device, const std::wstring &text, int pixelHeight,
                       DWORD color)
{
    if (!device || text.empty() || !g_gdiplusToken) {
        return false;
    }

    const wchar_t *fontName = L"Segoe UI";
    {
        Gdiplus::FontFamily probe(L"Segoe UI");
        if (!probe.IsAvailable()) {
            fontName = L"MS Gothic";
        }
    }
    Gdiplus::FontFamily family(fontName);
    Gdiplus::Font font(&family, static_cast<Gdiplus::REAL>(pixelHeight), Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::Bitmap probe(8, 8, PixelFormat32bppARGB);
    Gdiplus::RectF bounds;
    {
        Gdiplus::Graphics g(&probe);
        g.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font, Gdiplus::PointF(0, 0),
                        &bounds);
    }

    const int w = std::max(1, static_cast<int>(bounds.Width) + 8);
    const int h = std::max(1, static_cast<int>(bounds.Height) + 8);

    Gdiplus::Bitmap bitmap(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bitmap);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color),
                                                 GetBValue(color)));
        g.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, Gdiplus::PointF(4.0f, 4.0f),
                     &brush);
    }

    return uploadBitmap(device, bitmap, &m_texture, &m_width, &m_height);
}

bool Texture::fromGameTextAtlas(IDirect3DDevice9 *device,
                                const std::array<std::string,3> &lines,COLORREF color,
                                unsigned codePage,int textHeight)
{
    return fromGameTextRows(device,{lines[0],lines[1],lines[2]},color,codePage,
                            textHeight,24,256,2*(textHeight-1));
}

bool Texture::fromGameTextRows(IDirect3DDevice9 *device,
                               const std::vector<std::string> &lines,COLORREF color,
                               unsigned codePage,int textHeight,int rowStep,
                               int textureHeight,int fontHeight,int alignWidth,
                               bool underline)
{
    if(!device||textHeight<1||textHeight>22||rowStep<textHeight||textureHeight<rowStep||fontHeight<1)return false;
    // retail routine / retail routine / retail routine: A4R4G4B4 GDI bitmap, inverted alpha,
    // black shadow, transparent-edge colour expansion, then triangle filter.
    // The retail font buckets are 30/34/36/38 pixels; Music Room uses size 15.
    constexpr int width=1024,height=64;const int processedHeight=textHeight*2+6;
    if(processedHeight>height)return false;
    BITMAPV4HEADER info{};info.bV4Size=sizeof(info);
    info.bV4Width=width;info.bV4Height=-(height+1);
    info.bV4Planes=1;info.bV4BitCount=16;info.bV4V4Compression=BI_BITFIELDS;
    info.bV4SizeImage=width*height*2;
    info.bV4RedMask=0x0F00;info.bV4GreenMask=0x00F0;
    info.bV4BlueMask=0x000F;info.bV4AlphaMask=0xF000;
    HDC dc=CreateCompatibleDC(nullptr);void *raw=nullptr;
    HBITMAP bitmap=CreateDIBSection(nullptr,reinterpret_cast<BITMAPINFO *>(&info),DIB_RGB_COLORS,&raw,nullptr,0);
    HFONT font=CreateFontW(fontHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,codePage==936?GB2312_CHARSET:SHIFTJIS_CHARSET,
                           OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,
                           FIXED_PITCH|FF_ROMAN,codePage==936?L"SimHei":L"ＭＳ ゴシック");
    if(!dc||!bitmap||!font){if(font)DeleteObject(font);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}
    const auto oldBitmap=SelectObject(dc,bitmap),oldFont=SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT);
    // D3DX8's surface conversion is provided by the matching D3DX9 API on
    // the D3D9 host. Keep filter=4 and the original source/destination rects.
    using LoadSurface=HRESULT (WINAPI *)(IDirect3DSurface9 *,const PALETTEENTRY *,const RECT *,
        const void *,D3DFORMAT,UINT,const PALETTEENTRY *,const RECT *,DWORD,D3DCOLOR);
    static const HMODULE d3dx=LoadLibraryExW(L"d3dx9_43.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static const auto loadSurface=[]() {
        LoadSurface function=nullptr;
        if(d3dx) {
            const auto address=GetProcAddress(d3dx,"D3DXLoadSurfaceFromMemory");
            static_assert(sizeof(address)==sizeof(function));
            std::memcpy(&function,&address,sizeof(function));
        }
        return function;
    }();
    IDirect3DTexture9 *texture=nullptr;
    IDirect3DSurface9 *surface=nullptr;
    bool ok=loadSurface&&SUCCEEDED(device->CreateTexture(512,textureHeight,1,0,D3DFMT_A4R4G4B4,D3DPOOL_MANAGED,&texture,nullptr));
    if(ok) {
        D3DLOCKED_RECT locked{};
        ok=SUCCEEDED(texture->LockRect(0,&locked,nullptr,0));
        if(ok) {
            for(int y=0;y<textureHeight;++y)std::fill_n(static_cast<uint8_t *>(locked.pBits)+y*locked.Pitch,512*2,uint8_t(0));
            texture->UnlockRect(0);
            ok=SUCCEEDED(texture->GetSurfaceLevel(0,&surface));
        }
    }
    for(std::size_t index=0;index<lines.size();++index) {
        if(!ok)break;
        const int row=int(index);
        // The spell strip sprite (text.anm sprite 5) is 320x17 on an 18px row
        // pitch: 15 text rows plus a two-row underline, so the underline rows
        // must be copied as well or the strip's bottom two rows stay empty.
        const int copyHeight=textHeight+(underline?2:0);
        if(row*rowStep+copyHeight>textureHeight){ok=false;break;}
        const auto &text=lines[index];
        const int count=MultiByteToWideChar(codePage,0,text.data(),int(text.size()),nullptr,0);
        std::wstring wide(count,L'\0');
        if(count)MultiByteToWideChar(codePage,0,text.data(),int(text.size()),wide.data(),count);
        auto *pixels=static_cast<uint16_t *>(raw);
        std::fill_n(pixels,width*(height+1),uint16_t(0));
        for(int i=0;i<width*processedHeight;++i)pixels[i]^=0xF000;
        // retail routine right-aligns the string at sprite u+w; the DIB is twice the
        // final strip width, hence the 2x alignWidth.
        int textX=0;
        SIZE extent{};
        if(count)GetTextExtentPoint32W(dc,wide.data(),count,&extent);
        if(alignWidth>0&&count){
            textX=std::max(0,alignWidth*2-int(extent.cx));
        }
        SetTextColor(dc,RGB(0,0,0));TextOutW(dc,textX+3,3,wide.data(),count);
        SetTextColor(dc,color);TextOutW(dc,textX,0,wide.data(),count);GdiFlush();
        for(int i=0;i<width*processedHeight;++i)pixels[i]^=0xF000;
        // retail routine / retail routine: average the RGB nibbles of opaque neighbours
        // into fully transparent pixels, retaining their zero alpha.
        for(int y=0;y<processedHeight;++y)for(int x=0;x<width;++x) {
            auto &pixel=pixels[y*width+x];if(pixel>>12)continue;
            unsigned red=0,green=0,blue=0,neighbors=0;
            const auto add=[&](uint16_t p){if(p>>12){red+=(p>>8)&15;green+=(p>>4)&15;blue+=p&15;++neighbors;}};
            if(x>0)add(pixels[y*width+x-1]);
            if(x<width-1)add(pixels[y*width+x+1]);
            if(y>0)add(pixels[(y-1)*width+x]);
            if(y<height-1)add(pixels[(y+1)*width+x]);
            if(neighbors>1){red/=neighbors;green/=neighbors;blue/=neighbors;}
            pixel=uint16_t((red<<8)|(green<<4)|blue);
        }
        // The retail spell strip carries a two-row red gradient bar below the
        // 15-row glyph band. It is not painted across the whole 320px strip:
        // retail frames (LV1-3/4/5 declaration) measure a 169px solid body
        // anchored to the string's right end, 0xffbbbb fading to 0xff6666
        // left to right, with a ~24px alpha ramp on the left and a ~11px ramp
        // on the right. Two final rows means four DIB rows at full alpha.
        if(underline&&count){
            const int bandTop=textHeight*2;
            const int solidRight=textX+int(extent.cx);
            const int solidLeft=solidRight-338;
            constexpr int fadeIn=48,fadeOut=22;
            for(int y=bandTop;y<bandTop+4;++y){
                const int begin=std::max(0,solidLeft-fadeIn);
                const int end=std::min(int(width),solidRight+fadeOut);
                for(int x=begin;x<end;++x){
                    int alpha=15;
                    if(x<solidLeft)alpha=15*(x-(solidLeft-fadeIn))/fadeIn;
                    else if(x>=solidRight)alpha=15*((solidRight+fadeOut)-x)/fadeOut;
                    if(alpha<=0)continue;
                    const int t=std::clamp((x-solidLeft)*255/std::max(1,solidRight-solidLeft),0,255);
                    const int nibble=(187-85*t/255)>>4;
                    pixels[y*width+x]=uint16_t((alpha<<12)|(0xF<<8)|(nibble<<4)|nibble);
                }
            }
        }
        const RECT source{0,0,width,copyHeight*2},destination{0,row*rowStep,512,row*rowStep+copyHeight};
        ok=SUCCEEDED(loadSurface(surface,nullptr,&destination,pixels,D3DFMT_A4R4G4B4,width*2,nullptr,&source,4,0));
    }
    SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);
    DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);
    if(surface)surface->Release();
    if(!ok){if(texture)texture->Release();log::print("error : original GDI/D3DX text pipeline failed\r\n");return false;}
    if(m_texture)m_texture->Release();
    m_texture=texture;m_width=512;m_height=textureHeight;
    return true;
}

namespace {

void submitQuad(IDirect3DDevice9 *device, IDirect3DTexture9 *texture, const Vertex *quad, bool additive,
                bool opaqueTexture)
{
    device->SetTexture(0, texture);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    // Declaration backdrops paint their raw texture colours: retail measures
    // ~(148,35,33) on a camo blob whose A4R4G4B4 alpha is only 4/15, so the
    // page alpha is not part of the blend there and the quad is opaque.
    device->SetTextureStageState(0, D3DTSS_ALPHAOP,
                                 texture && !opaqueTexture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
    device->SetTexture(0, nullptr);
}

}

void drawQuadSegment(IDirect3DDevice9 *device, IDirect3DTexture9 *texture,
                     float x0, float y0, float x1, float y1,
                     float centerX, float centerY, float rotation,
                     DWORD tint, float u0, float v0, float u1, float v1,
                     bool additive)
{
    if (!device) {
        return;
    }
    const float z = 0.5f;
    const float rhw = 1.0f;
    Vertex quad[4] = {
        {x0, y0, z, rhw, tint, u0, v0},
        {x1, y0, z, rhw, tint, u1, v0},
        {x0, y1, z, rhw, tint, u0, v1},
        {x1, y1, z, rhw, tint, u1, v1},
    };
    const float c=std::cos(rotation),s=std::sin(rotation);
    for(auto &v:quad) {
        const float dx=v.x-centerX,dy=v.y-centerY;
        v.x=centerX+dx*c-dy*s-0.5f;
        v.y=centerY+dx*s+dy*c-0.5f;
    }
    submitQuad(device,texture,quad,additive,false);
}

void drawQuad(IDirect3DDevice9 *device, IDirect3DTexture9 *texture, float x, float y, float w,
              float h, DWORD tint, float u0, float v0, float u1, float v1,
              bool additive, float rotation, bool opaqueTexture)
{
    if (!device) {
        return;
    }
    const float z = 0.5f;
    const float rhw = 1.0f;
    Vertex quad[4] = {
        {x, y, z, rhw, tint, u0, v0},
        {x + w, y, z, rhw, tint, u1, v0},
        {x, y + h, z, rhw, tint, u0, v1},
        {x + w, y + h, z, rhw, tint, u1, v1},
    };

    const float cx=x+w*0.5f,cy=y+h*0.5f;
    const float c=std::cos(rotation),s=std::sin(rotation);
    for(auto &v:quad) {
        const float dx=v.x-cx,dy=v.y-cy;
        v.x=cx+dx*c-dy*s-0.5f;
        v.y=cy+dx*s+dy*c-0.5f;
    }

    submitQuad(device,texture,quad,additive,opaqueTexture);
}

void drawTriangleStrip(IDirect3DDevice9 *device, IDirect3DTexture9 *texture,
                       const TriangleVertex *vertices, int count, DWORD tint,
                       bool additive)
{
    if (!device || !texture || !vertices || count < 3) {
        return;
    }
    std::vector<Vertex> strip(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        strip[std::size_t(i)] = {vertices[i].x, vertices[i].y, 0.5f, 1.0f, tint,
                                 vertices[i].u, vertices[i].v};
    }
    device->SetTexture(0, texture);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, UINT(count - 2), strip.data(), sizeof(Vertex));
    device->SetTexture(0, nullptr);
}

} // namespace th95::platform
