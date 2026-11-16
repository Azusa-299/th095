#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace th95 {

inline constexpr uint32_t kArchiveMagic = 0x31414854u; // "THA1"

struct Entry {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;     // declared decompressed size
    uint32_t compSize = 0; // derived: next entry offset - this offset
};

// Port of retail routine (mount) and the retail routine read path.
class Archive {
public:
    bool open(const std::filesystem::path &path);
    // Mount an already-fetched DAT payload. Web hosts can fetch the archive
    // through JavaScript and hand the bytes to the same parser used on disk.
    bool openBytes(std::vector<uint8_t> data, std::string sourceName = "<memory>");
    void close();

    bool isOpen() const { return !m_data.empty(); }
    const std::vector<Entry> &entries() const { return m_entries; }
    std::size_t size() const { return m_data.size(); }

    const Entry *find(std::string_view name) const;

    // Returns the decoded payload; shorter than entry.size when the stream ends
    // early (invalid or truncated input).
    std::vector<uint8_t> extract(const Entry &entry) const;

private:
    bool parseTable();

    std::vector<uint8_t> m_data;
    std::vector<Entry> m_entries;
    std::size_t m_tableOffset = 0;
};

bool iequals(std::string_view a, std::string_view b);

} // namespace th95
