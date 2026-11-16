#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace th95::format {
// Port of retail routines. The runtime record layout keeps the 120 potential
// SC slots plus the status block; pending bestshot pixels never enter the
// container, they are written next to it as bestshot/bs_*.dat.
class ScoreProfile {
public:
    bool load(const std::filesystem::path &path);
    bool parse(const std::vector<uint8_t> &bytes);
    // retail routine: a pending bestshot is flushed first, then records that exist are
    // stamped with their chunk header, followed by the status block, LZSS
    // compressed and XOR-encrypted exactly like the retail container.
    bool save(const std::filesystem::path &path) const;
    std::vector<uint8_t> serialize() const;
    void setSavedSelection(int group,int scene);
    // Status block +20 holds the eight-character replay user name. The name
    // registration page pre-fills its edit buffer from here and writes it back
    // after a replay is stored.
    std::string replayName() const;
    void setReplayName(const std::string &name);
    // Status block +26 holds one rotating failure-comment index per level
    // (runtime profile +34 + level, read/written by 0x4288B0).
    uint8_t failureCommentIndex(int level) const;
    void setFailureCommentIndex(int level,uint8_t value);
    uint32_t field(int mission,int offset) const;
    void setField(int mission,int offset,uint32_t value);
    bool present(int mission) const;
    // retail routine stamps a fresh SC record when its mission reaches the success
    // page; without this header retail routine would drop the record on the next
    // flush because serialize() only emits present() slots.
    void createRecord(int mission);
    int savedGroup() const;
    int savedScene() const;
    // retail routine stages the winning photo for the next retail routine flush and stores the
    // retail routine checksum in the SC record's +64 word.
    void stageBestshot(int mission,int width,int height,uint32_t score,float rate,
                       const std::vector<uint8_t> &pixels);
    // retail routine always runs retail routine before the game object is freed. The port only
    // rewrites a profile that holds something, so an unreadable file is kept.
    bool hasData() const;
private:
    std::array<uint8_t,1112> m_status{};
    std::array<std::array<uint8_t,96>,120> m_records{};
    std::vector<uint8_t> m_stagedBestshot;
    int m_stagedMission=-1;
};
}
