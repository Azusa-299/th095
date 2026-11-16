#pragma once
#include <array>
#include <future>
#include <string>
#include <vector>
#include "../archive.hpp"
#include "../formats/anm.hpp"
#include "../platform/audio.hpp"

namespace th95::scene {
struct HelpInput {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, cancel = false;
    // word_retail routine: repeats are accepted by the list, not by pages.
    bool upRepeat = false, downRepeat = false;
};

// Title mode 9, retail routine. The title and Help share draw layers and a dynamic
// texture slot; the page is animated sprite 128, not a full-screen quad.
class HelpScene {
public:
    static constexpr int kPageCount = 9;
    enum class Stage { Closed = -1, List = 1, Loading = 2, Upload = 3, Page = 4, Closing = 5 };
    ~HelpScene() { close(); }
    bool open(Archive &archive, IDirect3DDevice9 *device, format::AnmTexture &title,
              platform::Audio &audio);
    // True on the tick that returns to title mode 1. Outgoing animations keep
    // updating/rendering after active() becomes false.
    bool update(const HelpInput &input);
    void renderLayer(IDirect3DDevice9 *device, int layer) const;
    bool active() const { return m_stage != Stage::Closed && m_stage != Stage::Closing; }
    int page() const { return m_selected; }
    Stage stage() const { return m_stage; }
    bool failed() const { return m_failed; }
    void close();

private:
    void showList();
    void highlightSelection();
    void beginPageLoad();
    bool uploadPage();
    void updateAnimations();
    void retirePage();
    static constexpr int kInputDelay = 20;
    format::AnmTexture m_page;
    std::array<format::AnmAnimation, kPageCount> m_rows;
    std::array<format::AnmAnimation, 2> m_panels;
    format::AnmAnimation m_heading, m_pageAnimation;
    std::vector<format::AnmAnimation> m_outgoingPages;
    format::AnmTexture *m_title = nullptr;
    Archive *m_archive = nullptr;
    platform::Audio *m_audio = nullptr;
    IDirect3DDevice9 *m_device = nullptr;
    std::future<std::vector<uint8_t>> m_pageRead;
    std::vector<uint8_t> m_pageData;
    std::string m_pageName;
    Stage m_stage = Stage::Closed;
    int m_selected = 0, m_stateTicks = 0;
    bool m_failed = false;
};
} // namespace th95::scene
