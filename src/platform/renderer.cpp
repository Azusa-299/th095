#include "renderer.hpp"
#include <vector>
#include <objidl.h>
#include <gdiplus.h>

#include "gfx.hpp"
#include "log.hpp"

namespace th95::platform {

Renderer::~Renderer()
{
    shutdown();
}

D3DPRESENT_PARAMETERS Renderer::presentParameters(bool fullscreen) const
{
    D3DPRESENT_PARAMETERS pp{};
    // retail routine always asks for a 640x480 backbuffer. Mode 0 additionally
    // switches to an exclusive 640x480@60 display mode with INTERVAL_ONE;
    // the GPU scales that signal to the panel, exactly like retail does.
    pp.BackBufferWidth = static_cast<UINT>(m_width);
    pp.BackBufferHeight = static_cast<UINT>(m_height);
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = m_hwnd;
    pp.Windowed = fullscreen ? FALSE : TRUE;
    pp.PresentationInterval = fullscreen ? D3DPRESENT_INTERVAL_ONE
                                         : D3DPRESENT_INTERVAL_IMMEDIATE;
    if (fullscreen) {
        pp.FullScreen_RefreshRateInHz = 60;
    }
    return pp;
}

bool Renderer::init(HWND hwnd, int width, int height)
{
    shutdown();
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_fullscreen = false;

    m_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!m_d3d) {
        log::print("error : Direct3DCreate9 failed\r\n");
        return false;
    }

    D3DPRESENT_PARAMETERS pp = presentParameters(false);
    const HRESULT hr = m_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                           D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &m_device);
    if (FAILED(hr)) {
        log::print("error : CreateDevice failed 0x%08lX\r\n", static_cast<unsigned long>(hr));
        shutdown();
        return false;
    }

    log::print("info : Direct3D %dx%d ready\r\n", width, height);
    return true;
}

bool Renderer::resetDevice(bool fullscreen)
{
    if (!m_device) {
        return false;
    }
    D3DPRESENT_PARAMETERS pp = presentParameters(fullscreen);
    const HRESULT hr = m_device->Reset(&pp);
    if (FAILED(hr)) {
        log::print("error : device Reset fullscreen=%u failed 0x%08lX\r\n",
                   unsigned(fullscreen), static_cast<unsigned long>(hr));
        // Leave the device in the mode it had before the refused switch so
        // the next Present still has a valid backbuffer.
        D3DPRESENT_PARAMETERS restore = presentParameters(m_fullscreen);
        if (FAILED(m_device->Reset(&restore))) {
            log::print("error : device Reset restore failed\r\n");
        } else {
            gfxNoteDeviceReset();
        }
        return false;
    }
    m_fullscreen = fullscreen;
    gfxNoteDeviceReset();
    return true;
}

bool Renderer::setFullscreen(bool fullscreen)
{
    if (!m_device || fullscreen == m_fullscreen) {
        return m_device != nullptr;
    }
    if (!resetDevice(fullscreen)) {
        return false;
    }
    log::print("info : display device fullscreen=%u\r\n", unsigned(fullscreen));
    return true;
}

void Renderer::shutdown()
{
    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
    if (m_d3d) {
        m_d3d->Release();
        m_d3d = nullptr;
    }
    m_fullscreen = false;
}

bool Renderer::beginFrame()
{
    if (!m_device) {
        return false;
    }
    // An exclusive device is lost on Alt+Tab or a display change. Reset as
    // soon as the driver allows it, like the retry loop in retail routine.
    const HRESULT coop = m_device->TestCooperativeLevel();
    if (coop == D3DERR_DEVICELOST) {
        return false;
    }
    if (coop == D3DERR_DEVICENOTRESET && !resetDevice(m_fullscreen)) {
        return false;
    }
    if (FAILED(m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0))) {
        return false;
    }
    return SUCCEEDED(m_device->BeginScene());
}

void Renderer::endFrame()
{
    if (m_device) {
        m_device->EndScene();
    }
}

void Renderer::present()
{
    if (m_device) {
        m_device->Present(nullptr, nullptr, nullptr, nullptr);
    }
}

bool Renderer::capture(const std::filesystem::path &path)
{
    if(!m_device)return false;
    IDirect3DSurface9 *target=nullptr,*copy=nullptr;
    if(FAILED(m_device->GetRenderTarget(0,&target)))return false;
    D3DSURFACE_DESC desc{};target->GetDesc(&desc);
    HRESULT result=m_device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr);
    if(SUCCEEDED(result))result=m_device->GetRenderTargetData(target,copy);
    target->Release();
    if(FAILED(result)){if(copy)copy->Release();return false;}
    D3DLOCKED_RECT locked{};
    if(FAILED(copy->LockRect(&locked,nullptr,D3DLOCK_READONLY))){copy->Release();return false;}
    bool saved=false;
    {
        Gdiplus::Bitmap bitmap(desc.Width,desc.Height,locked.Pitch,PixelFormat32bppRGB,static_cast<BYTE *>(locked.pBits));
        UINT count=0,size=0;Gdiplus::GetImageEncodersSize(&count,&size);
        std::vector<BYTE> memory(size);
        auto *encoders=reinterpret_cast<Gdiplus::ImageCodecInfo *>(memory.data());
        if(size&&Gdiplus::GetImageEncoders(count,size,encoders)==Gdiplus::Ok)
            for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0){saved=bitmap.Save(path.c_str(),&encoders[i].Clsid,nullptr)==Gdiplus::Ok;break;}
    }
    copy->UnlockRect();copy->Release();
    log::print("%s : frame capture %s\r\n",saved?"info":"error",path.string().c_str());
    return saved;
}
} // namespace th95::platform
