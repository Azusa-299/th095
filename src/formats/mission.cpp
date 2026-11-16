#include "mission.hpp"
#include <algorithm>

namespace th95::format {
namespace {
uint16_t u16(const uint8_t *p) {return uint16_t(p[0])|uint16_t(p[1])<<8;}
uint32_t u32(const uint8_t *p) {return uint32_t(u16(p))|uint32_t(u16(p+2))<<16;}
}
bool MissionMessages::parse(const std::vector<uint8_t> &data)
{
    m_messages.clear();
    if(data.size()<4)return false;
    const auto count=u32(data.data());
    if(count==0||count>(data.size()-4)/4)return false;
    std::map<std::pair<int,int>,MissionMessage> messages;
    for(uint32_t i=0;i<count;++i) {
        const auto offset=u32(data.data()+4+i*4);
        if(offset<4+count*4||offset>data.size()||data.size()-offset<204)return false;
        const auto *p=data.data()+offset;
        MissionMessage message;
        message.category=u16(p);message.index=u16(p+2);message.face=p[4];
        message.minimumScore=u32(p+8);
        for(int row=0;row<3;++row) {
            std::array<char,64> text{};
            uint8_t key=uint8_t(7*message.category+11*message.index+58);
            for(int j=0;j<64;++j) {
                text[j]=char(uint8_t(p[12+row*64+j]+key));
                key=uint8_t(key+j+23*(row+1));
            }
            const auto end=std::find(text.begin(),text.end(),'\0');
            if(end==text.end())return false;
            message.lines[row].assign(text.begin(),end);
        }
        if(!messages.emplace(std::make_pair(message.category,message.index),std::move(message)).second)return false;
    }
    m_messages=std::move(messages);return true;
}
const MissionMessage *MissionMessages::find(int category,int index) const
{
    const auto it=m_messages.find({category,index});
    return it==m_messages.end()?nullptr:&it->second;
}
unsigned MissionMessages::codePage() const
{
    // The archive has no locale field. Recognise the unmodified CN status
    // string, regardless of the archive filename. th095c.exe's retail routine uses
    // SimHei / charset 0x86; the bytes in this translation are CP936.
    const auto *status=find(98,1);
    return status&&status->lines[0]=="\xDF\x40\xD1\x65\x8C\xA2\xD2\xAA\xB0\x6C\xC9\xFA\xD3\xD0\xC8\xA4\xB5\xC4\xCA\xC2\xC7\xE9\xC1\xCB\xA1\xA3"?936:932;
}
} // namespace th95::format
