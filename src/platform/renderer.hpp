#pragma once

#include <windows.h>
#include <d3d9.h>
#include <filesystem>

namespace th95::platform {

/*
 * The original drives Direct3D 8 (retail routine). This host uses Direct3D 9
 * with the corresponding fixed-function pipeline operations.
 */
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool init(HWND hwnd, int width, int height);
    void shutdown();

    // retail routine creates the device with Windowed=FALSE for cfg +0xAF==0 and
    // Windowed=TRUE otherwise. D3D9 switches between the two with Reset.
    // Returns false when the driver refuses exclusive mode; the current
    // device mode is kept in that case.
    bool setFullscreen(bool fullscreen);
    bool fullscreen() const { return m_fullscreen; }

    bool beginFrame();
    void endFrame();
    void present();
    bool capture(const std::filesystem::path &path);

    IDirect3DDevice9 *device() const { return m_device; }

private:
    D3DPRESENT_PARAMETERS presentParameters(bool fullscreen) const;
    bool resetDevice(bool fullscreen);

    IDirect3D9 *m_d3d = nullptr;
    IDirect3DDevice9 *m_device = nullptr;
    HWND m_hwnd = nullptr;
    bool m_fullscreen = false;
    int m_width = 0;
    int m_height = 0;
};

} // namespace th95::platform
