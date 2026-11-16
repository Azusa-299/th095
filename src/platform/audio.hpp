#pragma once
#include <array>
#include <filesystem>
#include <vector>
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include "../archive.hpp"
#include "sound_effects.hpp"

namespace th95::platform {
class Audio {
public:
    ~Audio();
    bool load(Archive &archive, const std::filesystem::path &bgmPath,
              const char *track=nullptr);
    bool start();
    void update();
    void stop();
    bool loadEffects(Archive&);
    void playEffect(int id,float x=0);
    void stopEffect(int id);
    void stopEffects();
    bool effectPlaying(int id) const;
    bool effectHasPlaybackProgress(int id) const;
    // Runtime BGM/SE volumes from th095.cfg +0xB5/+0xB6 (retail routine writes
    // them to DAT_004ca1ac / DAT_004ca1b0). Mode 7 refreshes them per tick.
    void setVolumes(int bgm, int se);
    // Exact retail hundredths-of-a-dB values from retail routine (BGM, square
    // gain) and retail routine (each SE voice, cubic gain). Exposed so fixtures
    // can assert the byte-confirmed conversion.
    LONG bgmAttenuation() const;
    LONG seAttenuation(int base) const;
    void playSelect();
    void playConfirm();
    void playCancel();
    void playShutter();
    void playFocus();
    void playFocusIn();
    void playPhotoSuccess();
    void playPhotoInvalid();
    bool hasPlaybackProgress() const;
private:
    void fill(std::size_t buffer);
    void sound(int id,float x);
    void stopMusic();
    struct EffectVoice {
        IDirectSoundBuffer* buffer=nullptr;
        bool submitted=false;
    };
    // The retail engine owns one independently restartable buffer per ID.
    std::array<EffectVoice,47> m_effectVoices{};
    // The 47 samples belong to the archive, not to the current BGM track, so
    // they survive track changes exactly like the retail DirectSound voices.
    bool m_effectsPrepared=false;
    IDirectSound8* m_soundDevice=nullptr;
    HWAVEOUT m_output = nullptr;
    WAVEFORMATEX m_format{};
    std::vector<uint8_t> m_music;
    std::array<std::array<char,8192>,4> m_buffers{};
    std::array<WAVEHDR,4> m_headers{};
    std::size_t m_cursor = 0, m_loopStart = 0, m_loopEnd = 0;
    unsigned m_submitted = 0;
    int m_bgmVolume = 100, m_seVolume = 80;
    // Last volume word handed to waveOut; 0xffffffff forces the next apply.
    DWORD m_appliedVolume = 0xffffffffu;
    void applyBgmVolume();
};
} // namespace th95::platform
