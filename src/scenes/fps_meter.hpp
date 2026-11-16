#pragma once
// The retail frame-rate meter behind the top-of-screen counter.
//
// 0x424720 runs once per presented frame: it bumps a frame counter (+1928) and
// a half-second time base (unk_4C4E30), and only when the window has closed
// does it store frames/elapsed at +1948 == 0x4C4E0C, the float 0x423790 prints.
// Both the counter and the base are then reset, so the printed figure is a
// twice-a-second average that lags a stutter instead of tracking every frame.

#include <cstdint>

namespace th95::scene {

class FpsMeter {
public:
    // Tint ladder read at 0x423790: white at 40 and above, 0xFFA0A0FF from 30,
    // 0xFF5050FF below. Both fallback tints are blue, which is what a sudden
    // drop to a low rate looks like on screen.
    static std::uint32_t tint(double fps)
    {
        if (fps >= 40.0) return 0xFFFFFFFFu;
        if (fps >= 30.0) return 0xFFA0A0FFu;
        return 0xFF5050FFu;
    }
    void addFrame(double seconds)
    {
        if (!(seconds > 0.0)) return;
        ++m_frames;
        m_elapsed += seconds;
        if (m_elapsed < 0.5) return;
        m_fps = double(m_frames) / m_elapsed;
        m_elapsed = 0.0;
        m_frames = 0;
    }
    // Zero until the first window closes, which is what the retail static holds.
    double rate() const { return m_fps; }

private:
    double m_fps = 0.0, m_elapsed = 0.0;
    unsigned m_frames = 0;
};

} // namespace th95::scene
