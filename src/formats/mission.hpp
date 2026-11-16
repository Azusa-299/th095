#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace th95::format {
struct MissionMessage {
    uint16_t category=0, index=0;
    uint8_t face=0;
    uint32_t minimumScore=0;
    std::array<std::string,3> lines;
};
// retail routines: offset table, 12-byte headers and three
// independently encrypted 64-byte strings (CP932 JP / CP936 CN). Keep the source bytes
// intact until the platform chooses the appropriate font/code page.
class MissionMessages {
public:
    bool parse(const std::vector<uint8_t> &data);
    const MissionMessage *find(int category,int index) const;
    std::size_t size() const {return m_messages.size();}
    unsigned codePage() const;
private:
    std::map<std::pair<int,int>,MissionMessage> m_messages;
};
} // namespace th95::format
