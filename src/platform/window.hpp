#pragma once

#include <windows.h>

namespace th95::platform {

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool create(const char *title, int width, int height);
    void destroy();
    // retail routine derives two window styles from cfg +0xAF; title mode 7
    // returning 5 asks the main loop (retail address) to re-apply them.
    // Mode 0 is a borderless fullscreen popup here (retail uses an
    // undecorated 640x480 window at 0,0); mode 1 is a fixed 640x480 window.
    void setDisplayMode(bool windowed);
    bool windowed() const { return m_windowed; }

    // Drains the message queue; returns false once the window is closing.
    bool poll();

    HWND handle() const { return m_hwnd; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_instance = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_quit = false;
    bool m_windowed = false;
    // WM_ACTIVATEAPP state; retail retail address checks it in WM_SETCURSOR.
    bool m_active = true;
};

// Mode 0: retail's 0x00cf0000 640x480 window at (0,0) plus an exclusive
// 640x480@60 display mode (retail routine Windowed=FALSE); the GPU scales the
// signal to the panel. Mode 1: retail's 0x100a0000 has no caption; the port
// keeps the caption so the close button stays reachable, and fixes the
// frame at a 640x480 client. WS_SYSMENU on the popup keeps Alt+F4 working.
inline constexpr DWORD kDisplayStyleFullscreen = WS_OVERLAPPEDWINDOW;
inline constexpr DWORD kDisplayStyleWindowed =
    WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
constexpr DWORD displayStyle(bool windowed)
{
    return windowed ? kDisplayStyleWindowed : kDisplayStyleFullscreen;
}

void timeInit();
double timeNow();
// QPC deadlines with a high-resolution waitable timer. The final short
// interval is spun so Sleep's scheduler quantum does not quantise 60 Hz.
class FramePacer {
public:
    FramePacer();
    ~FramePacer();
    void waitUntil(double deadline);
private:
    HANDLE m_timer=nullptr;
    bool m_period=false;
};

} // namespace th95::platform
