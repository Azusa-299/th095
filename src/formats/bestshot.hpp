#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace th95::format {
// Retail read path plus the retail write side. The 24 byte
// header stores the payload mode, the source bytes per pixel and the cropped
// photo size. Every mode except 1 is followed by the 80 byte metadata block
// before the LZSS payload.
class Bestshot {
public:
    bool load(const std::filesystem::path &path);
    bool parse(const std::vector<uint8_t> &bytes);
    int width() const {return m_width;}
    int height() const {return m_height;}
    // 256x256 BGRA page with the photo in its top-left corner, matching the
    // title.anm page 1 slot. Alpha uses the retail 0xF0 marker.
    const std::vector<uint8_t> &pixels() const {return m_pixels;}
    // retail routine sum over header, metadata and decoded pixels, compared with the
    // SC record's +64 word by retail routine.
    uint32_t checksum() const {return m_checksum;}
private:
    int m_width=0,m_height=0;
    uint32_t m_checksum=0;
    std::vector<uint8_t> m_pixels;
};

// One-based group/scene names used by both retail routine and retail routine.
std::string bestshotFileName(int mission);
// retail routine is applied to each segment separately.
uint32_t bestshotChecksum(const uint8_t *header24,const uint8_t *metadata80,
                          const std::vector<uint8_t> &pixels);
// Builds the 24 byte header + 80 zero metadata block + compressed BGR
// payload. pixels must hold width*height*3 tightly packed BGR triples.
std::vector<uint8_t> buildBestshotFile(int mission,int width,int height,uint32_t score,
                                       float rate,const std::vector<uint8_t> &pixels);
} // namespace th95::format
