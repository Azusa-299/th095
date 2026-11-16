#include "archive.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>

#include "crypto.hpp"
#include "log.hpp"
#include "lzss.hpp"

namespace th95 {
namespace {

struct KeySet {
    uint8_t key;
    uint8_t step;
    uint32_t block;
    uint32_t limit;
};

// byte_retail routine: 8 key sets selected by (sum of name bytes) & 7.
constexpr std::array<KeySet, 8> kFileKeys{{
    {27, 55, 64, 10240},
    {81, 233, 64, 12288},
    {193, 81, 128, 12800},
    {3, 25, 1024, 30720},
    {171, 205, 512, 10240},
    {18, 52, 128, 12800},
    {53, 151, 128, 10240},
    {153, 55, 1024, 8192},
}};

uint32_t readU32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::size_t keyIndexFor(std::string_view name)
{
    unsigned sum = 0;
    for (const unsigned char c : name) {
        sum += c;
    }
    return sum & 7u;
}

} // namespace

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool Archive::open(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log::print("error : %s is not found.\r\n", path.string().c_str());
        return false;
    }

    std::vector<uint8_t> data(std::istreambuf_iterator<char>(in), {});
    return openBytes(std::move(data), path.string());
}

bool Archive::openBytes(std::vector<uint8_t> data, std::string sourceName)
{
    close();
    m_data = std::move(data);
    if (m_data.size() < 16) {
        log::print("error : archive too small\r\n");
        close();
        return false;
    }

    log::print("info : %s open arcfile\r\n", sourceName.c_str());

    if (!parseTable()) {
        close();
        return false;
    }
    return true;
}

void Archive::close()
{
    m_data.clear();
    m_entries.clear();
    m_tableOffset = 0;
}

bool Archive::parseTable()
{
    std::vector<uint8_t> header(m_data.begin(), m_data.begin() + 16);
    crypto::decrypt(header, 27, 55, 16, 16);

    const uint32_t magic = readU32(header.data());
    if (magic != kArchiveMagic) {
        log::print("error : bad magic 0x%08X\r\n", magic);
        return false;
    }

    const uint32_t rawSize = readU32(header.data() + 4) - 123456789u;
    const uint32_t compSize = readU32(header.data() + 8) - 987654321u;
    const uint32_t count = readU32(header.data() + 12) - 135792468u;

    if (compSize == 0 || compSize > m_data.size() || count == 0) {
        log::print("error : bad table header\r\n");
        return false;
    }

    m_tableOffset = m_data.size() - compSize;

    std::vector<uint8_t> blob(m_data.begin() + static_cast<ptrdiff_t>(m_tableOffset),
                              m_data.end());
    crypto::decrypt(blob, 62, 155, 128, compSize);

    const std::vector<uint8_t> table = lzss::decode(blob, rawSize);
    if (table.empty()) {
        log::print("error : table decode failed (%u bytes)\r\n", rawSize);
        return false;
    }

    /*
     * Entries are variable length: NUL-terminated name padded up to a 4-byte
     * boundary, then three u32 (offset, raw size, reserved).
     */
    m_entries.reserve(count);
    std::size_t pos = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto end = std::find(table.begin() + static_cast<ptrdiff_t>(pos), table.end(), 0);
        if (end == table.end()) {
            log::print("error : entry table truncated at %u\r\n", i);
            return false;
        }

        Entry entry;
        entry.name.assign(reinterpret_cast<const char *>(table.data() + pos),
                          static_cast<std::size_t>(end - (table.begin() + pos)));
        pos = static_cast<std::size_t>(end - table.begin()) + 1;
        pos = (pos + 3) & ~static_cast<std::size_t>(3);

        if (pos + 12 > table.size()) {
            log::print("error : entry table truncated at %u\r\n", i);
            return false;
        }
        entry.offset = readU32(table.data() + pos);
        entry.size = readU32(table.data() + pos + 4);
        pos += 12;

        m_entries.push_back(std::move(entry));
    }

    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const uint32_t next =
            (i + 1 < m_entries.size()) ? m_entries[i + 1].offset : static_cast<uint32_t>(m_tableOffset);
        m_entries[i].compSize = next - m_entries[i].offset;
    }

    log::print("info : entries %u, table %u bytes\r\n", count, rawSize);
    return true;
}

const Entry *Archive::find(std::string_view name) const
{
    for (const Entry &entry : m_entries) {
        if (iequals(entry.name, name)) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<uint8_t> Archive::extract(const Entry &entry) const
{
    if (!isOpen() || entry.size == 0 || entry.compSize == 0) {
        return {};
    }
    if (static_cast<std::size_t>(entry.offset) + entry.compSize > m_data.size()) {
        return {};
    }

    const KeySet &keys = kFileKeys[keyIndexFor(entry.name)];

    std::vector<uint8_t> blob(m_data.begin() + entry.offset,
                              m_data.begin() + entry.offset + entry.compSize);
    crypto::decrypt(blob, keys.key, keys.step, keys.block, keys.limit);

    // retail routine: when compressed size equals raw size the payload is stored,
    // only the XOR layer applies.
    if (entry.compSize == entry.size) {
        return blob;
    }

    return lzss::decode(blob, entry.size);
}

} // namespace th95
