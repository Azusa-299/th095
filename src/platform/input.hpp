#pragma once

namespace th95::platform {

// A frame of logical menu input. A web backend can populate the same
// structure from DOM events or a canvas gamepad without touching scenes.
struct InputState {
    bool focused = true;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;
    bool cancel = false;
    bool upHeld = false;
    bool downHeld = false;
    bool leftHeld = false;
    bool rightHeld = false;
    bool upRepeat = false;
    bool downRepeat = false;
    bool leftRepeat = false;
    bool rightRepeat = false;
    bool shiftHeld = false;
    bool shoot = false, shootHeld = false, pause = false;
    // retail routine / retail routine toggles: S shows the last shot's rate at half size,
    // L flips the selected record's lock bit (the "L" marker on the photo).
    bool showRate = false, lockBest = false;
    // First pressed slot of the retail 32-button array (retail routine).
    // 32 means no button held, matching DAT_004a5894's initial value.
    int buttonIndex = 32;
};

namespace detail {
// FUN_00419910's keyboard path copies the DirectInput state from offset 0x30
// into DAT_004c4428, so slot i is DIK code i. Mode 7 scans i < 0x20.
// Values are Win32 virtual-key codes; slot 0 (DIK 0) is unused.
inline constexpr int kVkForDik[32] = {
    0x00, 0x1B, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', 0xBD, 0xBB, 0x08, 0x09,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', 0xDB, 0xDD, 0x0D, 0x11, 'A', 'S'
};
}

// Returns the DIK index for a Win32 virtual-key code, or 32 when the key is
// outside the range title mode 7 can bind. Header-only so headless fixtures
// can assert the table without linking the platform input object.
inline int buttonIndexFromVirtualKey(int vk)
{
    for (int i = 1; i < 32; ++i)
        if (detail::kVkForDik[i] == vk) return i;
    return 32;
}

class Input {
public:
    void poll();
    const InputState &state() const { return m_state; }

private:
    InputState m_state{};
    bool m_keys[256]{};
    int m_repeatTicks[4]{};
};

} // namespace th95::platform
