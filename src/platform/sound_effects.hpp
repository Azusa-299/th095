#pragma once
#include <array>

namespace th95::platform {
struct SoundEffect {const char* file;int volume;int priority;};
// JP 1.02a: retail routine (sample, hundredths of a dB, priority), retail routine (names).
inline constexpr std::array<SoundEffect,47> soundEffects{{
    {"se_plst00.wav",-1900,0},{"se_plst00.wav",-2100,0},
    {"se_enep00.wav",-1200,5},{"se_enep00.wav",-1500,5},
    {"se_pldead00.wav",-1100,100},{"se_power0.wav",-700,100},{"se_power1.wav",-700,100},
    {"se_tan00.wav",-1900,50},{"se_tan01.wav",-2200,50},{"se_tan02.wav",-2400,50},
    {"se_ok00.wav",-500,100},{"se_cancel00.wav",-400,100},{"se_select00.wav",-800,10},
    {"se_gun00.wav",-1500,10},{"se_cat00.wav",-1000,100},{"se_tan00.wav",-1100,50},
    {"se_lazer00.wav",-1300,50},{"se_lazer01.wav",-1400,50},{"se_enep01.wav",-900,100},
    {"se_damage00.wav",-880,0},{"se_item00.wav",-1500,0},{"se_tan00.wav",-300,20},
    {"se_tan01.wav",-1800,20},{"se_tan02.wav",-1800,20},{"se_kira00.wav",-1100,50},
    {"se_kira01.wav",-1300,50},{"se_kira02.wav",-1500,50},{"se_timeout.wav",-500,100},
    {"se_graze.wav",-1100,20},{"se_powerup.wav",-800,90},{"se_graze.wav",-1200,20},
    {"se_kira00.wav",-500,50},{"se_pause.wav",-800,100},{"se_cardget.wav",-800,100},
    {"se_option.wav",-800,100},{"se_damage01.wav",-700,0},{"se_timeout2.wav",-300,100},
    {"se_invalid.wav",0,100},{"se_slash.wav",0,100},{"se_slash.wav",-600,100},
    {"se_ok00.wav",-300,100},{"se_shutter.wav",-300,100},{"se_ch00.wav",-300,100},
    {"se_ch01.wav",-300,100},{"se_focus.wav",-300,100},{"se_nice.wav",-500,100},
    {"se_focusin.wav",-500,100}
}};
}
