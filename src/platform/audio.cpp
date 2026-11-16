#include "audio.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <cmath>
#include "../log.hpp"

namespace th95::platform {
namespace {
uint32_t u32(const uint8_t *p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
uint16_t u16(const uint8_t *p) {return uint16_t(p[0])|uint16_t(p[1])<<8;}
std::vector<uint8_t> effect(Archive &archive,const char *name) {const auto *e=archive.find(name);return e?archive.extract(*e):std::vector<uint8_t>{};}
// retail routines: dB = __ftol2((base + 5000) * gain) - 5000 with
// gain = 1 - (1 - volume/100)^power. BGM uses power 2, each SE voice power 3;
// a zero volume is the retail mute case (-10000 == DSBVOLUME_MIN).
long retailAttenuation(int volume,int base,int power)
{
    if(volume<=0)return -10000;
    const float a=1.0f-float(volume)/100.0f;
    float product=a;
    for(int i=1;i<power;++i)product*=a;
    const float gain=1.0f-product;
    return long(std::trunc(double(base+5000)*double(gain)))-5000;
}
}
Audio::~Audio() {
    stop();
    for(auto& voice:m_effectVoices)if(voice.buffer)voice.buffer->Release();
    if(m_soundDevice)m_soundDevice->Release();
}
void Audio::stopMusic()
{
    if(m_output) {
        waveOutReset(m_output);
        for(auto &header:m_headers)if(header.dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(m_output,&header,sizeof(header));
        waveOutClose(m_output);m_output=nullptr;
        // A new device starts from its own volume, so re-apply on open.
        m_appliedVolume=0xffffffffu;
    }
}
void Audio::stop()
{
    stopMusic();
    stopEffects();
}
bool Audio::load(Archive &archive,const std::filesystem::path &path,const char*track)
{
    // Only the music stream is replaced here. Retail keeps the 47 effect
    // voices alive across BGM changes (retail addresses own them, the music
    // loader does not), so stopping them would cut off a menu sound that
    // started on the very tick that switches scene.
    stopMusic();m_cursor=0;m_submitted=0;m_music.clear();m_format={};
    if(!m_effectsPrepared)loadEffects(archive);
    const auto *entry=archive.find("thbgm.fmt");
    if(!entry){log::print("error : missing thbgm.fmt\r\n");return false;}
    const auto fmt=archive.extract(*entry);
    if(fmt.size()<52){log::print("error : invalid thbgm.fmt\r\n");return false;}
    const auto *p=fmt.data();
    if(track){
        const char *requested=std::strrchr(track,'/');
        requested=requested?requested+1:track;
        bool found=false;
        for(std::size_t pos=0;pos+52<=fmt.size();pos+=52)
            if(std::strncmp(reinterpret_cast<const char*>(fmt.data()+pos),requested,16)==0){p=fmt.data()+pos;found=true;break;}
        if(!found){log::print("error : BGM track missing %s\r\n",track);return false;}
    }
    const uint32_t offset=u32(p+16);
    m_loopStart=u32(p+24);m_loopEnd=u32(p+28);
    WAVEFORMATEX wave{};
    wave.wFormatTag=u16(p+32);wave.nChannels=u16(p+34);wave.nSamplesPerSec=u32(p+36);
    wave.nAvgBytesPerSec=u32(p+40);wave.nBlockAlign=u16(p+44);wave.wBitsPerSample=u16(p+46);
    if(wave.wFormatTag!=WAVE_FORMAT_PCM || wave.nChannels!=2 || wave.wBitsPerSample!=16 || wave.nBlockAlign!=4 || wave.nSamplesPerSec!=44100 || wave.nAvgBytesPerSec!=176400 || m_loopStart>=m_loopEnd || m_loopEnd>128*1024*1024 || m_loopStart%4 || m_loopEnd%4) {
        log::print("error : unsupported BGM format/loop\r\n");return false;
    }
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in || in.tellg()<std::streamoff(uint64_t(offset)+m_loopEnd)){log::print("error : missing/short BGM file %s\r\n",path.string().c_str());return false;}
    in.seekg(offset);m_music.resize(m_loopEnd);
    if(!in.read(reinterpret_cast<char *>(m_music.data()),std::streamsize(m_music.size())))return false;
    m_format=wave;
    log::print("info : BGM prepared %s track %.16s PCM %lu Hz stereo; offset=%u loop=[%zu,%zu)\r\n",path.string().c_str(),reinterpret_cast<const char *>(p),static_cast<unsigned long>(wave.nSamplesPerSec),offset,m_loopStart,m_loopEnd);
    return true;
}
bool Audio::start()
{
    if(m_output)return true;
    if(m_music.empty()||m_format.nBlockAlign==0)return false;
    m_cursor=0;m_submitted=0;
    const auto result=waveOutOpen(&m_output,WAVE_MAPPER,&m_format,0,0,CALLBACK_NULL);
    if(result!=MMSYSERR_NOERROR){log::print("error : waveOutOpen failed %u\r\n",unsigned(result));m_output=nullptr;return false;}
    applyBgmVolume();
    for(std::size_t i=0;i<m_headers.size();++i){
        auto &header=m_headers[i];header={};header.lpData=m_buffers[i].data();header.dwBufferLength=DWORD(m_buffers[i].size());
        if(waveOutPrepareHeader(m_output,&header,sizeof(header))!=MMSYSERR_NOERROR){stop();return false;}
    }
    log::print("info : BGM playback started\r\n");
    for(std::size_t i=0;i<m_headers.size();++i)fill(i);
    return m_output!=nullptr;
}
void Audio::fill(std::size_t index)
{
    auto &buffer=m_buffers[index];
    std::size_t written=0;
    while(written<buffer.size()) {
        if(m_cursor>=m_loopEnd)m_cursor=m_loopStart;
        const auto n=std::min(buffer.size()-written,m_loopEnd-m_cursor);
        std::memcpy(buffer.data()+written,m_music.data()+m_cursor,n);m_cursor+=n;written+=n;
    }
    const auto result=waveOutWrite(m_output,&m_headers[index],sizeof(WAVEHDR));
    if(result!=MMSYSERR_NOERROR)log::print("error : waveOutWrite failed %u\r\n",unsigned(result));
    else ++m_submitted;
}
void Audio::update()
{
    if(!m_output)return;
    for(std::size_t i=0;i<m_headers.size();++i)if(m_headers[i].dwFlags&WHDR_DONE)fill(i);
    if(m_submitted>=64&&m_submitted<68){
        MMTIME t{};t.wType=TIME_BYTES;
        if(waveOutGetPosition(m_output,&t,sizeof(t))==MMSYSERR_NOERROR)log::print("info : BGM device position=%lu type=%u submitted=%u\r\n",static_cast<unsigned long>(t.u.cb),unsigned(t.wType),m_submitted);
        m_submitted=68;
    }
}
bool Audio::loadEffects(Archive&archive)
{
    stopEffects();
    for(auto&voice:m_effectVoices)if(voice.buffer){voice.buffer->Release();voice.buffer=nullptr;voice.submitted=false;}
    if(!m_soundDevice){
        if(FAILED(DirectSoundCreate8(nullptr,&m_soundDevice,nullptr))||
           FAILED(m_soundDevice->SetCooperativeLevel(GetDesktopWindow(),DSSCL_NORMAL))){
            log::print("warning : DirectSound effect device unavailable\r\n");return false;
        }
    }
    bool complete=true;
    for(std::size_t id=0;id<soundEffects.size();++id){
        const auto data=effect(archive,soundEffects[id].file);
        WAVEFORMATEX fmt{};const uint8_t*samples=nullptr;uint32_t size=0;
        if(data.size()>=12&&!std::memcmp(data.data(),"RIFF",4)&&!std::memcmp(data.data()+8,"WAVE",4))
        for(std::size_t pos=12;pos+8<=data.size();){
            const uint32_t count=u32(data.data()+pos+4);const auto*chunk=data.data()+pos;
            if(count>data.size()-pos-8)break;
            if(!std::memcmp(chunk,"fmt ",4)&&count>=16){
                fmt.wFormatTag=u16(chunk+8);fmt.nChannels=u16(chunk+10);
                fmt.nSamplesPerSec=u32(chunk+12);fmt.nAvgBytesPerSec=u32(chunk+16);
                fmt.nBlockAlign=u16(chunk+20);fmt.wBitsPerSample=u16(chunk+22);
            }else if(!std::memcmp(chunk,"data",4)){samples=chunk+8;size=count;}
            pos+=8+std::size_t(count)+(count&1);
        }
        if(!samples||!size||fmt.wFormatTag!=WAVE_FORMAT_PCM||
           (fmt.nChannels!=1&&fmt.nChannels!=2)||(fmt.wBitsPerSample!=8&&fmt.wBitsPerSample!=16)||
           fmt.nBlockAlign!=fmt.nChannels*fmt.wBitsPerSample/8||size%fmt.nBlockAlign){complete=false;continue;}
        DSBUFFERDESC desc{};desc.dwSize=sizeof(desc);desc.dwBufferBytes=size;desc.lpwfxFormat=&fmt;
        desc.dwFlags=DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN|DSBCAPS_GETCURRENTPOSITION2|DSBCAPS_GLOBALFOCUS;
        auto&voice=m_effectVoices[id];
        if(FAILED(m_soundDevice->CreateSoundBuffer(&desc,&voice.buffer,nullptr))){complete=false;continue;}
        void*first=nullptr,*second=nullptr;DWORD firstSize=0,secondSize=0;
        if(FAILED(voice.buffer->Lock(0,size,&first,&firstSize,&second,&secondSize,0))){
            voice.buffer->Release();voice.buffer=nullptr;complete=false;continue;
        }
        std::memcpy(first,samples,firstSize);if(secondSize)std::memcpy(second,samples+firstSize,secondSize);
        voice.buffer->Unlock(first,firstSize,second,secondSize);
    }
    m_effectsPrepared=true;
    log::print("%s : 47 reusable DirectSound effect buffers prepared\r\n",complete?"info":"warning");
    return complete;
}
void Audio::sound(int id,float x)
{
    if(id<0||id>=int(m_effectVoices.size()))return;
    auto*buffer=m_effectVoices[id].buffer;if(!buffer)return;
    m_effectVoices[id].submitted=false;
    buffer->Stop();buffer->SetCurrentPosition(0);
    buffer->SetVolume(std::clamp<LONG>(seAttenuation(soundEffects[id].volume),DSBVOLUME_MIN,DSBVOLUME_MAX));
    buffer->SetPan(LONG(std::clamp(x/192.0f,-1.0f,1.0f)*1000));
    if(FAILED(buffer->Play(0,0,0)))log::print("warning : effect %d playback failed\r\n",id);
    else m_effectVoices[id].submitted=true;
}
void Audio::stopEffect(int id)
{
    if(id>=0&&id<int(m_effectVoices.size())&&m_effectVoices[id].buffer){
        m_effectVoices[id].buffer->Stop();m_effectVoices[id].buffer->SetCurrentPosition(0);
        m_effectVoices[id].submitted=false;
    }
}
void Audio::stopEffects(){for(int id=0;id<int(m_effectVoices.size());++id)stopEffect(id);}
void Audio::playEffect(int id,float x){sound(id,x);}
bool Audio::effectPlaying(int id)const
{
    if(id<0||id>=int(m_effectVoices.size())||!m_effectVoices[id].buffer)return false;
    DWORD status=0;return SUCCEEDED(m_effectVoices[id].buffer->GetStatus(&status))&&(status&DSBSTATUS_PLAYING);
}
bool Audio::effectHasPlaybackProgress(int id)const
{
    if(id<0||id>=int(m_effectVoices.size())||!m_effectVoices[id].buffer)return false;
    DWORD cursor=0,status=0;
    return m_effectVoices[id].submitted&&
           SUCCEEDED(m_effectVoices[id].buffer->GetCurrentPosition(&cursor,nullptr))&&
           SUCCEEDED(m_effectVoices[id].buffer->GetStatus(&status))&&
           (cursor>0||!(status&DSBSTATUS_PLAYING));
}
// The retail volume path refreshes DAT_004ca1ac / DAT_004ca1b0 on every
// interaction tick, then case 8 re-applies the BGM volume to the live stream.
void Audio::setVolumes(int bgm,int se)
{
    m_bgmVolume=std::clamp(bgm,0,100);
    m_seVolume=std::clamp(se,0,100);
    applyBgmVolume();
}
LONG Audio::bgmAttenuation() const
{
    // retail routine case 8 passes *(int*)(manager+0x52c4), which is
    // DAT_004ca1ac: the BGM volume global itself is the base offset.
    return LONG(retailAttenuation(m_bgmVolume,m_bgmVolume,2));
}
LONG Audio::seAttenuation(int base) const
{
    // base is word[ECX*8 + retail address], i.e. soundEffects[id].volume.
    return LONG(retailAttenuation(m_seVolume,base,3));
}
void Audio::applyBgmVolume()
{
    if(!m_output)return;
    // Retail clamps positive dB at the device; waveOutSetVolume is a linear
    // 16-bit amplitude, while DirectSound dB is 1/100 dB.
    const long db=std::clamp<long>(bgmAttenuation(),-10000,0);
    const double amplitude=std::pow(10.0,double(db)/2000.0);
    const uint16_t level=uint16_t(std::lround(amplitude*65535.0));
    const DWORD packed=DWORD(level)|DWORD(level)<<16;
    // Mode 7 calls setVolumes every interactive tick; skip the device call
    // while the resulting amplitude is unchanged.
    if(packed==m_appliedVolume)return;
    m_appliedVolume=packed;
    waveOutSetVolume(m_output,packed);
}
void Audio::playSelect() {playEffect(12);}
void Audio::playConfirm() {playEffect(10);}
void Audio::playCancel() {playEffect(11);}
void Audio::playShutter() {playEffect(41);}
void Audio::playFocus() {playEffect(44);}
void Audio::playFocusIn() {playEffect(46);}
void Audio::playPhotoSuccess() {playEffect(33);}
void Audio::playPhotoInvalid() {playEffect(37);}
bool Audio::hasPlaybackProgress() const
{
    if(!m_output)return false;
    MMTIME position{};
    position.wType=TIME_BYTES;
    if(waveOutGetPosition(m_output,&position,sizeof(position))!=MMSYSERR_NOERROR)return false;
    if(position.wType==TIME_BYTES)return position.u.cb>0;
    if(position.wType==TIME_SAMPLES)return position.u.sample>0;
    if(position.wType==TIME_MS)return position.u.ms>0;
    return false;
}
} // namespace th95::platform
