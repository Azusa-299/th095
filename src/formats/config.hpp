#pragma once
#include <array>
#include <cstdint>
#include <filesystem>

namespace th95::format {
// The retail 200-byte th095.cfg block at DAT_004c478c, read by the title and
// option loaders and edited by title mode 7.
class GameConfig {
public:
    static constexpr std::size_t kSize = 200;
    static constexpr std::size_t kEditBytes = 18;
    static constexpr uint32_t kVersion = 0x95001;

    // Returns true only when the file exists, is exactly kSize bytes and
    // passes all of the retail checks. Otherwise the retail defaults are
    // installed (and thbgm.dat is probed when bgmPath is supplied).
    bool load(const std::filesystem::path &cfgPath,
              const std::filesystem::path &bgmPath = {});
    // Plain CREATE_ALWAYS write of the whole block.
    bool save(const std::filesystem::path &cfgPath) const;
    void reset();
    bool valid() const;
    const std::array<uint8_t, kSize> &bytes() const { return m_bytes; }

    uint8_t displayMode() const { return m_bytes[0xAF]; }
    void setDisplayMode(uint8_t mode) { m_bytes[0xAF] = mode; }
    uint8_t bgmVolume() const { return m_bytes[0xB5]; }
    uint8_t seVolume() const { return m_bytes[0xB6]; }
    void setBgmVolume(uint8_t volume) { m_bytes[0xB5] = volume; }
    void setSeVolume(uint8_t volume) { m_bytes[0xB6] = volume; }
    // The three editable slots live at +0x02, +0x00, +0x06.
    uint16_t keyBinding(int slot) const;
    void setKeyBinding(int slot, uint16_t code);

private:
    static uint16_t read16(const uint8_t *p);
    static void write16(uint8_t *p, uint16_t value);
    std::array<uint8_t, kSize> m_bytes{};
};
} // namespace th95::format
