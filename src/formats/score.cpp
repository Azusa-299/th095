#include "score.hpp"
#include "bestshot.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include "../crypto.hpp"
#include "../lzss.hpp"

namespace th95::format {
namespace {
uint16_t u16(const uint8_t *p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
uint32_t u32(const uint8_t *p){return uint32_t(u16(p))|uint32_t(u16(p+2))<<16;}
void put16(uint8_t *p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
void put32(uint8_t *p,uint32_t v){put16(p,uint16_t(v));put16(p+2,uint16_t(v>>16));}
// The validator requires the stored word to equal the sum of every other
// dword in the chunk, so it is written last.
uint32_t chunkSum(const uint8_t *data,std::size_t size)
{
    uint32_t sum=0;
    for(std::size_t i=0;i+4<=size;i+=4)if(i!=8)sum+=u32(data+i);
    return sum;
}
}
bool ScoreProfile::load(const std::filesystem::path &path)
{
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file){*this={};return false;}
    const auto size=file.tellg();
    if(size<24||size>16*1024*1024){*this={};return false;}
    std::vector<uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0);file.read(reinterpret_cast<char *>(data.data()),data.size());
    if(!file){*this={};return false;}
    return parse(data);
}
bool ScoreProfile::parse(const std::vector<uint8_t> &bytes)
{
    *this={};
    if(bytes.size()<24||u32(bytes.data())!=0x35394854||u16(bytes.data()+8)!=2||
       u32(bytes.data()+4)!=bytes.size()||u32(bytes.data()+16)!=bytes.size()-24)return false;
    const auto decodedSize=u32(bytes.data()+20);
    // A bounded reader rejects malformed declarations before allocating.
    if(decodedSize<16||decodedSize>16*1024*1024)return false;
    std::vector<uint8_t> compressed(bytes.begin()+24,bytes.end());
    crypto::decrypt(compressed,172,53,16);
    const auto body=lzss::decode(compressed,decodedSize);
    if(body.size()!=decodedSize)return false;
    ScoreProfile profile;
    for(std::size_t offset=0;offset<body.size();) {
        if(body.size()-offset<16)return false;
        const auto *p=body.data()+offset;
        const auto size=u32(p+4);
        if(size<16||size>body.size()-offset)return false;
        const auto magic=u16(p),kind=u16(p+2);
        if(!((magic==0x5453&&kind==0&&size==1112)||(magic==0x4353&&kind==1&&size==96)))return false;
        uint32_t sum=0;
        for(std::size_t i=0;i+4<=size;i+=4)sum+=u32(p+i);
        if(sum-u32(p+8)!=u32(p+8))return false;
        if(magic==0x5453)std::copy_n(p,1112,profile.m_status.begin());
        else {
            const auto index=u32(p+12);if(index>=120)return false;
            std::copy_n(p,96,profile.m_records[index].begin());
        }
        offset+=size;
    }
    *this=profile;return true;
}
uint32_t ScoreProfile::field(int mission,int offset) const
{
    if(mission<0||mission>=120||offset<0||offset>92)return 0;
    return u32(m_records[mission].data()+offset);
}
void ScoreProfile::setField(int mission,int offset,uint32_t value)
{
    if(mission<0||mission>=120||offset<0||offset>92)return;
    for(int i=0;i<4;++i)m_records[mission][std::size_t(offset)+i]=uint8_t(value>>(i*8));
}
bool ScoreProfile::present(int mission) const {return (field(mission,0)&0xFFFF)==0x4353;}
bool ScoreProfile::hasData() const
{
    if(m_stagedMission>=0)return true;
    for(int mission=0;mission<120;++mission)if(present(mission))return true;
    return false;
}
void ScoreProfile::createRecord(int mission)
{
    if(mission<0||mission>=120||present(mission))return;
    auto &record=m_records[std::size_t(mission)];
    put16(record.data(),0x4353);
    put16(record.data()+2,1);
    put32(record.data()+4,96);
    put32(record.data()+12,uint32_t(mission));
}
std::vector<uint8_t> ScoreProfile::serialize() const
{
    // retail routine writes every existing record first, then the status block.
    std::vector<uint8_t> body;
    body.reserve(120*96+1112);
    for(int mission=0;mission<120;++mission) {
        if(!present(mission))continue;
        std::array<uint8_t,96> record=m_records[std::size_t(mission)];
        put16(record.data(),0x4353);
        put16(record.data()+2,1);
        put32(record.data()+4,96);
        put32(record.data()+12,uint32_t(mission));
        put32(record.data()+8,chunkSum(record.data(),record.size()));
        body.insert(body.end(),record.begin(),record.end());
    }
    std::array<uint8_t,1112> status=m_status;
    put16(status.data(),0x5453);
    put16(status.data()+2,0);
    put32(status.data()+4,1112);
    put32(status.data()+8,chunkSum(status.data(),status.size()));
    body.insert(body.end(),status.begin(),status.end());

    std::vector<uint8_t> payload=lzss::encode(body);
    crypto::encrypt(payload,172,53,16);
    std::vector<uint8_t> out(24+payload.size(),0);
    put32(out.data(),0x35394854);
    put32(out.data()+4,uint32_t(out.size()));
    put16(out.data()+8,2);
    put32(out.data()+16,uint32_t(payload.size()));
    put32(out.data()+20,uint32_t(body.size()));
    std::memcpy(out.data()+24,payload.data(),payload.size());
    return out;
}
void ScoreProfile::stageBestshot(int mission,int width,int height,uint32_t score,float rate,
                                 const std::vector<uint8_t> &pixels)
{
    if(mission<0||mission>=120)return;
    auto file=buildBestshotFile(mission,width,height,score,rate,pixels);
    if(file.empty())return;
    // The header and the 80 zero metadata bytes are the first 104 bytes.
    setField(mission,64,bestshotChecksum(file.data(),file.data()+24,pixels));
    m_stagedBestshot=std::move(file);
    m_stagedMission=mission;
}
bool ScoreProfile::save(const std::filesystem::path &path) const
{
    // retail routine creates bestshot/ and flushes the pending photo before the
    // container itself. A failed photo write does not abort the save.
    if(!m_stagedBestshot.empty()&&m_stagedMission>=0) {
        auto directory=path.parent_path()/L"bestshot";
        std::error_code ec;
        std::filesystem::create_directories(directory,ec);
        if(!ec) {
            std::ofstream photo(directory/std::filesystem::path(bestshotFileName(m_stagedMission)),
                                std::ios::binary|std::ios::trunc);
            if(photo)photo.write(reinterpret_cast<const char *>(m_stagedBestshot.data()),
                                 std::streamsize(m_stagedBestshot.size()));
        }
    }
    const auto bytes=serialize();
    // A written file must survive the same validation the loader applies.
    ScoreProfile check;
    if(!check.parse(bytes))return false;
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    if(!file)return false;
    file.write(reinterpret_cast<const char *>(bytes.data()),std::streamsize(bytes.size()));
    return bool(file);
}
void ScoreProfile::setSavedSelection(int group,int scene)
{
    put16(m_status.data()+22,uint16_t(int16_t(group)));
    put16(m_status.data()+24,uint16_t(int16_t(scene)));
}
int ScoreProfile::savedGroup() const {return int16_t(u16(m_status.data()+22));} // runtime +30
int ScoreProfile::savedScene() const {return int16_t(u16(m_status.data()+24));} // runtime +32
std::string ScoreProfile::replayName() const
{
    // The status chunk data starts at m_status+12; retail keeps eight bytes
    // there for the replay user name (runtime profile +20).
    const char *name = reinterpret_cast<const char *>(m_status.data()+12);
    std::size_t length=0;
    while(length<8&&name[length]!='\0')++length;
    std::string result(name,length);
    while(!result.empty()&&result.back()==' ')result.pop_back();
    return result;
}
void ScoreProfile::setReplayName(const std::string &name)
{
    for(std::size_t i=0;i<8;++i) {
        m_status[12+i]=i<name.size()?uint8_t(name[i]):uint8_t(' ');
    }
}
uint8_t ScoreProfile::failureCommentIndex(int level) const
{
    if(level<0||level>=12)return 0;
    // Runtime profile +34 maps to status block +26 (the data starts at +12).
    return m_status[std::size_t(26+level)];
}
void ScoreProfile::setFailureCommentIndex(int level,uint8_t value)
{
    if(level<0||level>=12)return;
    m_status[std::size_t(26+level)]=value;
}
}
