#include "config.hpp"
#include <fstream>
#include "../log.hpp"

namespace th95::format {
namespace {
constexpr std::array<uint8_t, 6> kBounds{{2, 3, 2, 2, 3, 3}};
}
uint16_t GameConfig::read16(const uint8_t *p)
{
    return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8);
}
void GameConfig::write16(uint8_t *p, uint16_t value)
{
    p[0] = uint8_t(value);
    p[1] = uint8_t(value >> 8);
}
void GameConfig::reset()
{
    // retail routine: zero the block, then install the retail defaults. The
    // first 18 bytes and the key table come from the runtime copy whose PE
    // image content is zero.
    m_bytes.fill(0);
    write16(m_bytes.data() + 0xA4, uint16_t(kVersion));
    write16(m_bytes.data() + 0xA6, uint16_t(kVersion >> 16));
    write16(m_bytes.data() + 0xA8, 600);
    write16(m_bytes.data() + 0xAA, 600);
    m_bytes[0xAD] = 1;
    m_bytes[0xAE] = 1;
    m_bytes[0xAF] = 0;
    m_bytes[0xB0] = 0;
    m_bytes[0xB1] = 2;
    m_bytes[0xB2] = 0;
    m_bytes[0xB3] = 1;
    m_bytes[0xB4] = 2;
    m_bytes[0xB5] = 100;
    m_bytes[0xB6] = 80;
}
bool GameConfig::valid() const
{
    const uint32_t version = uint32_t(read16(m_bytes.data() + 0xA4)) |
                             uint32_t(read16(m_bytes.data() + 0xA6)) << 16;
    if (version != kVersion) return false;
    for (std::size_t i = 0; i < kBounds.size(); ++i)
        if (m_bytes[0xAC + i] >= kBounds[i]) return false;
    return true;
}
bool GameConfig::load(const std::filesystem::path &cfgPath,
                      const std::filesystem::path &bgmPath)
{
    reset();
    bool fromFile = false;
    std::ifstream in(cfgPath, std::ios::binary | std::ios::ate);
    if (in && in.tellg() == std::streamoff(kSize)) {
        in.seekg(0);
        std::array<uint8_t, kSize> data{};
        if (in.read(reinterpret_cast<char *>(data.data()), std::streamsize(kSize))) {
            m_bytes = data;
            if (valid()) fromFile = true;
        }
    }
    if (fromFile) {
        log::print("info : th095.cfg accepted from %s\r\n", cfgPath.string().c_str());
        return true;
    }
    // retail routine failure path: defaults again, then probe ./thbgm.dat.
    reset();
    if (!bgmPath.empty()) {
        std::error_code ec;
        m_bytes[0xAD] = std::filesystem::exists(bgmPath, ec) ? 1 : 2;
        if (ec) m_bytes[0xAD] = 2;
    }
    log::print("info : th095.cfg missing or rejected; retail defaults (bgm flag %u)\r\n",
               unsigned(m_bytes[0xAD]));
    return false;
}
bool GameConfig::save(const std::filesystem::path &cfgPath) const
{
    if (cfgPath.empty()) return false;
    std::ofstream out(cfgPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char *>(m_bytes.data()), std::streamsize(kSize));
    return static_cast<bool>(out);
}
uint16_t GameConfig::keyBinding(int slot) const
{
    static constexpr std::array<std::size_t, 3> kOffsets{{0x02, 0x00, 0x06}};
    if (slot < 0 || slot >= int(kOffsets.size())) return 0;
    return read16(m_bytes.data() + kOffsets[std::size_t(slot)]);
}
void GameConfig::setKeyBinding(int slot, uint16_t code)
{
    static constexpr std::array<std::size_t, 3> kOffsets{{0x02, 0x00, 0x06}};
    if (slot < 0 || slot >= int(kOffsets.size())) return;
    write16(m_bytes.data() + kOffsets[std::size_t(slot)], code);
}
} // namespace th95::format
