#include "input.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace th95::platform {

void Input::poll()
{
#if defined(_WIN32)
    m_state = {};
    m_state.focused = GetForegroundWindow() == GetActiveWindow();
    const int keys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE,
                        VK_SHIFT, 'Z', 'X', 'S', 'L',
                        VK_NUMPAD8, VK_NUMPAD2, VK_NUMPAD4, VK_NUMPAD6,
                        VK_NUMPAD7, VK_NUMPAD9, VK_NUMPAD1, VK_NUMPAD3};
    bool held[256]{};
    for (const int key : keys) {
        held[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
        const bool pressed = m_state.focused && held[key] && !m_keys[key];
        // Retail 0x419AE0 sets bits 0x10/0x20/0x40/0x80 from the arrow keys,
        // the numpad directions and the four numpad diagonals; W is not a
        // movement binding in the keyboard path.
        if (key == VK_UP || key == VK_NUMPAD8 || key == VK_NUMPAD7 || key == VK_NUMPAD9)
            m_state.up = m_state.up || pressed;
        if (key == VK_DOWN || key == VK_NUMPAD2 || key == VK_NUMPAD1 || key == VK_NUMPAD3)
            m_state.down = m_state.down || pressed;
        if (key == VK_LEFT || key == VK_NUMPAD4 || key == VK_NUMPAD7 || key == VK_NUMPAD1)
            m_state.left = m_state.left || pressed;
        if (key == VK_RIGHT || key == VK_NUMPAD6 || key == VK_NUMPAD9 || key == VK_NUMPAD3)
            m_state.right = m_state.right || pressed;
        // retail routine: confirm is bit 0x1000 (Enter) or bit 0x2 (Z); Space is not
        // a menu key (VK_SPACE feeds the 0x2000 'D' action there).
        if (key == VK_RETURN || key == 'Z') m_state.confirm = m_state.confirm || pressed;
        // Cancel is bit 0x8 (Esc) or bit 0x1 (X); Shift sets 0x5, so its
        // cancel bit is part of the retail mask as well.
        if (key == VK_ESCAPE || key == 'X' || key == VK_SHIFT) m_state.cancel = m_state.cancel || pressed;
        if (key == 'Z') {m_state.shoot=pressed;m_state.shootHeld=m_state.focused&&held[key];}
        if (key == VK_ESCAPE) m_state.pause=pressed;
        // retail routine: word_retail routine bit 0x400 is the S edge, bit 0x8000 is L.
        // S is therefore not a movement binding in the retail input map.
        if (key == 'S') m_state.showRate = pressed;
        if (key == 'L') m_state.lockBest = pressed;
        m_keys[key] = held[key];
    }
    m_state.upHeld = m_state.focused && (held[VK_UP] || held[VK_NUMPAD8] || held[VK_NUMPAD7] || held[VK_NUMPAD9]);
    m_state.downHeld = m_state.focused && (held[VK_DOWN] || held[VK_NUMPAD2] || held[VK_NUMPAD1] || held[VK_NUMPAD3]);
    m_state.leftHeld = m_state.focused && (held[VK_LEFT] || held[VK_NUMPAD4] || held[VK_NUMPAD7] || held[VK_NUMPAD1]);
    m_state.rightHeld = m_state.focused && (held[VK_RIGHT] || held[VK_NUMPAD6] || held[VK_NUMPAD9] || held[VK_NUMPAD3]);
    // Retail DIK/VK offset 0x10 yields bits 0x1|0x4 (X / Shift); gameplay
    // tests bit 0x1 for slow movement and camera charging. Keep X's menu
    // cancel edge while exposing its held gameplay action.
    m_state.shiftHeld = m_state.focused && (held[VK_SHIFT] || held['X']);
    const bool heldDirections[4] = {m_state.upHeld, m_state.downHeld,
                                    m_state.leftHeld, m_state.rightHeld};
    for (int i = 0; i < 4; ++i) {
        m_repeatTicks[i] = heldDirections[i] ? m_repeatTicks[i] + 1 : 0;
        const bool repeat = m_repeatTicks[i] >= 26 && (m_repeatTicks[i] - 26) % 8 == 0;
        if (i == 0) m_state.upRepeat = repeat;
        else if (i == 1) m_state.downRepeat = repeat;
        else if (i == 2) m_state.leftRepeat = repeat;
        else m_state.rightRepeat = repeat;
    }
    // retail routine scans the 32-button state for the first entry with bit 7;
    // retail stores the resulting index (32 when none) into DAT_004a5894.
    int firstButton = 32;
    if (m_state.focused) {
        for (int i = 0; i < 32; ++i) {
            const int vk = detail::kVkForDik[i];
            if (vk && (GetAsyncKeyState(vk) & 0x8000)) { firstButton = i; break; }
        }
    }
    m_state.buttonIndex = firstButton;
#else
    // A browser backend replaces this method with DOM/gamepad events.
    m_state = {};
#endif
}

} // namespace th95::platform
