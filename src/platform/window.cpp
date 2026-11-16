#include "window.hpp"
#include "../log.hpp"
#include <mmsystem.h>

namespace th95::platform {
namespace {

constexpr const char *kClassName = "th95_reimpl";

LARGE_INTEGER g_freq{};
LARGE_INTEGER g_start{};

// The whole picture is a fixed 640x480 backbuffer. Without DPI awareness
// Windows bitmap-stretches the window at 125%/150% scaling.
// (Per-monitor v2 makes D3D9 CreateDevice fail with D3DERR_INVALIDCALL on
// this host, so the port asks for system awareness instead.)
void enableDpiAwareness()
{
    SetProcessDPIAware();
}

} // namespace

void timeInit()
{
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start);
}

double timeNow()
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - g_start.QuadPart) /
           static_cast<double>(g_freq.QuadPart);
}

FramePacer::FramePacer(){
    m_timer=CreateWaitableTimerExW(nullptr,nullptr,2,TIMER_ALL_ACCESS);
    if(!m_timer){m_timer=CreateWaitableTimerW(nullptr,FALSE,nullptr);m_period=timeBeginPeriod(1)==TIMERR_NOERROR;}
}
FramePacer::~FramePacer(){if(m_timer)CloseHandle(m_timer);if(m_period)timeEndPeriod(1);}
void FramePacer::waitUntil(double deadline){
    for(;;){
        const double remaining=deadline-timeNow();if(remaining<=0)return;
        if(remaining>.0004&&m_timer){
            LARGE_INTEGER due;due.QuadPart=-LONGLONG((remaining-.0003)*10000000);
            if(SetWaitableTimer(m_timer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(m_timer,100);
            else Sleep(1);
        }else if(remaining>.002)Sleep(1);else YieldProcessor();
    }
}

Window::~Window()
{
    destroy();
}

LRESULT CALLBACK Window::wndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_NCCREATE) {
        auto *self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTA *>(lparam)->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }

    if (auto *self = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA))) {
        return self->handleMessage(msg, wparam, lparam);
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

LRESULT Window::handleMessage(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_SETCURSOR:
        // retail routine: the arrow stays visible in windowed mode and while the
        // window is deactivated; fullscreen hides it over the client area so
        // the game keeps its retail cursor behavior.
        if (LOWORD(lparam) == HTCLIENT) {
            SetCursor((m_windowed || !m_active)
                          ? static_cast<HCURSOR>(LoadCursorA(nullptr, reinterpret_cast<LPCSTR>(IDC_ARROW)))
                          : nullptr);
            return TRUE;
        }
        break;
    case WM_ACTIVATEAPP:
        // Retail stores wParam in retail addresses and still runs the
        // default handler; the port only needs the active flag.
        m_active = wparam != 0;
        break;
    case WM_ERASEBKGND:
        // D3D owns every client pixel. Letting the class brush erase the
        // window would flash black between Present and the next frame.
        return 1;
    case WM_CLOSE:
        log::print("info : window WM_CLOSE\r\n");
        DestroyWindow(m_hwnd);
        return 0;
    case WM_DESTROY:
        log::print("info : window WM_DESTROY\r\n");
        m_quit = true;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(m_hwnd, msg, wparam, lparam);
}

bool Window::create(const char *title, int width, int height)
{
    // Must run before the first HWND exists.
    enableDpiAwareness();
    m_instance = GetModuleHandleA(nullptr);
    m_width = width;
    m_height = height;

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::wndProc;
    wc.hInstance = m_instance;
    wc.hCursor = LoadCursorA(nullptr, reinterpret_cast<LPCSTR>(IDC_ARROW));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;

    if (!RegisterClassExA(&wc)) {
        return false;
    }

    const DWORD style = kDisplayStyleWindowed;
    RECT rc{0, 0, width, height};
    AdjustWindowRect(&rc, style, FALSE);

    m_hwnd = CreateWindowExA(0, kClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, m_instance,
                             this);
    if (!m_hwnd) {
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    return true;
}

void Window::setDisplayMode(bool windowed)
{
    if (!m_hwnd) return;
    const DWORD style = displayStyle(windowed);
    // A maximized window keeps WS_MAXIMIZE. Switching the style and forcing a
    // 640x480 rect while zoomed leaves SetWindowPos fighting the zoom state,
    // so restore first.
    if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
    const DWORD visible = DWORD(GetWindowLongPtrA(m_hwnd, GWL_STYLE)) & WS_VISIBLE;
    SetWindowLongPtrA(m_hwnd, GWL_STYLE, LONG_PTR(style | visible));
    // Mode 0 covers the monitor and mode 1 is a 640x480 window. Both are
    // sized to the same 640:480 client ratio, so the fixed backbuffer is
    // only ever scaled uniformly.
    RECT area{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoA(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
        area = windowed ? monitor.rcWork : monitor.rcMonitor;
    const int areaWidth = area.right - area.left;
    const int areaHeight = area.bottom - area.top;
    int width = 0, height = 0;
    if (windowed) {
        // Retail passes a WINDOW rect of 640x480. The port must keep a
        // 640x480 CLIENT area, otherwise D3D stretches its 640x480
        // backbuffer and the picture warps. AdjustWindowRect derives the
        // correct outer size.
        RECT rc{0, 0, m_width, m_height};
        AdjustWindowRect(&rc, style, FALSE);
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
    } else {
        // retail routine mode 0: window rect 640x480 at (0,0) with style
        // 0x00cf0000. Exclusive fullscreen then owns the whole display and
        // the GPU scales the 640x480 signal to the panel, exactly like
        // retail; if the driver refuses, the window simply stays small.
        RECT rc{0, 0, m_width, m_height};
        AdjustWindowRect(&rc, style, FALSE);
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
    }
    int x = area.left + (areaWidth - width) / 2;
    int y = area.top + (areaHeight - height) / 2;
    if (!windowed) {
        // Retail creates the fullscreen window at (0,0).
        x = area.left;
        y = area.top;
    }
    if (x < area.left) x = area.left;
    if (y < area.top) y = area.top;
    SetWindowPos(m_hwnd, HWND_TOP, x, y, width, height,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
    // Keep the cursor visible in both modes so the title-bar buttons are
    // reachable. Retail hides it in mode 0 (WM_SETCURSOR handler).
    m_windowed = windowed;
}

void Window::destroy()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_instance) {
        UnregisterClassA(kClassName, m_instance);
    }
}

bool Window::poll()
{
    MSG msg{};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_quit = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return !m_quit;
}

} // namespace th95::platform
