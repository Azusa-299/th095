#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "../archive.hpp"
#include "../platform/gfx.hpp"

namespace th95::format {
struct AnmSprite {
    uint32_t id = 0;
    float u = 0, v = 0, w = 0, h = 0;
    std::size_t page = 0;
};
struct AnmInstruction {
    uint16_t opcode = 0, mask = 0;
    int16_t time = 0;
    uint32_t offset = 0;
    std::vector<uint8_t> args;
    int integer(std::size_t index) const;
    float number(std::size_t index) const;
};
// Version 4 ANM: linked entries, explicit offset tables and THTX payloads.
// Atlas packing is independent of the screen positions in animation scripts.
class AnmTexture {
public:
    bool load(Archive &archive, const std::string &name, IDirect3DDevice9 *device);
    bool loadData(const std::vector<uint8_t> &data, const std::string &name,
                  IDirect3DDevice9 *device);
    // '@' pages reference a runtime texture slot. Help changes title page 13
    // while retaining sprite 128's original UVs, dimensions and animation.
    bool bindExternalPage(std::size_t page, const AnmTexture *source,
                          std::size_t sourcePage = 0);
    bool bindExternalTexture(std::size_t page, const platform::Texture *source);
    const std::vector<AnmSprite> &sprites() const { return m_sprites; }
    const std::vector<AnmInstruction> *script(int id) const;
    // Retail 0x404B80 resolves a script by its index in the file's descriptor
    // table (*(this+4)+4*id), not by the id field stored in that descriptor.
    // Most ANMs use 0..N for both, but ascii.anm stores 0xFFFFFFFE/0xFFFFFFFF
    // in its two slots and CardInf starts slot 1 (sprite 110). -1 is a real
    // script id here, so absence has to be reported separately from the id.
    std::optional<int> scriptIdAt(std::size_t slot) const {
        if (slot >= m_scriptOrder.size()) return std::nullopt;
        return m_scriptOrder[slot];
    }
    const AnmSprite *sprite(int id) const;
    // Coverage audits enumerate every script in a resource without knowing
    // which ids the scene happens to start.
    std::vector<int> scriptIds() const {
        std::vector<int> ids;
        ids.reserve(m_scripts.size());
        for (const auto &entry : m_scripts) ids.push_back(entry.first);
        return ids;
    }
    bool setSpriteSize(int id,float width,float height);
    std::size_t pageCount() const { return m_pages.size(); }
    std::size_t scriptCount() const { return m_scripts.size(); }
    void draw(IDirect3DDevice9 *device, int spriteId, float x, float y,
              DWORD tint = 0xFFFFFFFF, float scaleX = 1, float scaleY = 1,
              float scrollU = 0, float scrollV = 0, bool additive = false,
              float rotation = 0, bool opaqueTexture = false) const;
    // Whole-page diagnostic quad. Help instead binds its texture to title
    // page 13 and draws sprite 128 through script 144.
    void drawPage(IDirect3DDevice9 *device, std::size_t page, float x, float y,
                  DWORD tint = 0xFFFFFFFF, float scaleX = 1, float scaleY = 1) const;
    int pageWidth(std::size_t page) const;
    int pageHeight(std::size_t page) const;
    IDirect3DTexture9 *pageTexture(std::size_t page) const;
private:
    struct Page {
        int width = 0, height = 0;
        platform::Texture texture;
        const AnmTexture *external = nullptr;
        const platform::Texture *externalTexture = nullptr;
        std::size_t externalPage = 0;
    };
    std::vector<Page> m_pages;
    std::vector<AnmSprite> m_sprites;
    std::map<int, std::vector<AnmInstruction>> m_scripts;
    std::vector<int> m_scriptOrder;
};
// Title subset of the original VM, retail routine. Unknown operations are logged.
class AnmAnimation {
public:
    bool start(const AnmTexture &file, int scriptId, float offsetX = 0, float offsetY = 0,
               uint32_t randomSeed = 1);
    void interrupt(int label);
    // speed is the retail global ANM scale (the global dword holding 1.0).
    // Keeping it fractional lets slowed death/defeat effects interpolate on
    // every presented frame instead of waiting for an integer tick.
    void update(float speed = 1.0f);
    void draw(IDirect3DDevice9 *device, DWORD modulation = 0x80808080) const;
    // Same as draw() but ignores the page alpha channel; the retail declaration
    // backdrops (enm scripts 0/1) blend on the sprite alpha alone.
    void drawOpaque(IDirect3DDevice9 *device, DWORD modulation = 0x80808080) const;
    bool alive() const { return m_alive; }
    int layer() const { return m_layer; }
    int spriteId() const { return m_sprite; }
    int scriptId() const { return m_scriptId; }
    float x() const { return m_x + m_offsetX; }
    float y() const { return m_y + m_offsetY; }
    float alpha() const { return m_alpha; }
    float rotation() const { return m_rotation; }
    void setSprite(int sprite) {m_sprite=sprite;}
    void setOffset(float x,float y) {m_offsetX=x;m_offsetY=y;}
    void setScale(float x,float y) {m_scaleX=x;m_scaleY=y;}
    void setAlpha(float alpha) {m_alpha=alpha;}
    void setRotation(float angle) {m_rotation=angle;}
    void setEnabled(bool enabled) {m_enabled=enabled;}
    void setInteger(int id,int value) {if(auto *v=intVariable(id))*v=value;}
    float scaleX() const {return m_scaleX;}
    float scaleY() const {return m_scaleY;}
    bool enabled() const {return m_enabled;}
    void animateColor(DWORD color,int ticks) {
        tween(1,{{float((color>>16)&255),float((color>>8)&255),float(color&255)}},ticks,0);
    }
    // Retail particles store their ARGB tint directly (bullet.anm object
    // +544) at creation; there is no tween for the target colour.
    void setColor(DWORD color) {
        m_rgb={{float((color>>16)&255),float((color>>8)&255),float(color&255)}};
    }
private:
    void drawInternal(IDirect3DDevice9 *device, DWORD modulation, bool opaqueTexture) const;
    struct Tween {
        std::array<float, 3> from{}, to{};
        float duration = 0, elapsed = 0;
        int mode = 0;
    };
    float value(const AnmInstruction &instruction, std::size_t n);
    int intValue(const AnmInstruction &instruction, std::size_t n) const;
    int *intVariable(int id);
    uint32_t random();
    void tween(int channel, const std::array<float,3> &to, int duration, int mode);
    void advance(float speed, bool scriptTime=true);
    const AnmTexture *m_file = nullptr;
    const std::vector<AnmInstruction> *m_script = nullptr;
    std::size_t m_cursor = 0;
    int m_scriptId = 0, m_sprite = -1, m_layer = 0;
    float m_time = 0;
    int m_anchorX = 0, m_anchorY = 0, m_pending = 0;
    std::size_t m_returnCursor = 0;
    float m_returnTime = 0;
    float m_wait = 0;
    bool m_returnStopped = false, m_hasReturn = false, m_enabled = true;
    float m_x = 0, m_y = 0, m_offsetX = 0, m_offsetY = 0;
    float m_scaleX = 1, m_scaleY = 1, m_rotation = 0, m_rotationVelocity = 0;
    float m_scrollU = 0, m_scrollV = 0, m_velocityU = 0, m_velocityV = 0;
    std::array<float,3> m_rgb{{255,255,255}};
    float m_alpha = 255;
    std::array<float,4> m_variables{};
    std::array<int,6> m_intVariables{};
    std::array<Tween,5> m_tweens;
    // Retain the render flags used by the retail VM even where the D3D9
    // sprite path does not currently consume the corresponding bit.
    uint32_t m_flags = 0;
    int m_vertexCount = 0;
    uint32_t m_rng = 1;
    bool m_alive = false, m_visible = false, m_stopped = false, m_additive = false;
};
} // namespace th95::format
