#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace th95::format {
struct MusicTrackComment {
    std::string bgm;
    std::string title;
    std::array<std::string,8> comments{};
};
// sprt/musiccmt.txt (retail address / retail address): '@' starts one track with a BGM
// filename, one list caption and eight comment lines. '#' lines are skipped.
// Bytes stay in their original CP932/CP936 encoding until the font pipeline.
class MusicComments {
public:
    bool parse(const std::vector<uint8_t> &data);
    const std::vector<MusicTrackComment> &tracks() const { return m_tracks; }
    std::size_t size() const { return m_tracks.size(); }
private:
    std::vector<MusicTrackComment> m_tracks;
};
} // namespace th95::format
