#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <array>
#include <vector>

#include <windows.h>
#include <d3d9.h>

namespace th95::platform {

// GDI+ must stay initialised for the whole process lifetime: textures hold
// GDI+ objects until they are destroyed.
bool gfxInit();
void gfxShutdown();

// IDirect3DDevice9::Reset drops every D3DPOOL_DEFAULT resource. Renderer
// bumps this generation after a successful Reset so render targets rebuild
// themselves instead of keeping dangling handles.
unsigned gfxDeviceGeneration();
void gfxNoteDeviceReset();


// Thin D3D9 texture wrapper. Images are decoded through GDI+ so the archive's
// stored JPEG/PNG payloads can be uploaded directly.
class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    Texture(Texture &&other) noexcept
        : m_texture(other.m_texture), m_width(other.m_width), m_height(other.m_height),
          m_generation(other.m_generation)
    {
        other.m_texture = nullptr;
        other.m_width = 0;
        other.m_height = 0;
    }

    Texture &operator=(Texture &&other) noexcept
    {
        if (this != &other) {
            if (m_texture) {
                m_texture->Release();
            }
            m_texture = other.m_texture;
            m_width = other.m_width;
            m_height = other.m_height;
            m_generation = other.m_generation;
            other.m_texture = nullptr;
            other.m_width = 0;
            other.m_height = 0;
        }
        return *this;
    }

    bool fromMemory(IDirect3DDevice9 *device, const void *data, std::size_t size);
    bool fromText(IDirect3DDevice9 *device, const std::wstring &text, int pixelHeight,
                  DWORD color);
    // text.anm sprites 38..40 occupy three 512x22 strips at y=0/24/48.
    bool fromGameTextAtlas(IDirect3DDevice9 *device,
                          const std::array<std::string,3> &lines, COLORREF color,
                          unsigned codePage=932,int textHeight=19);
    // Same retail addresses pipeline for the 512x17 strips used by Music
    // Room: rowStep and target texture height come from the ANM page.
    // alignWidth>0 right-aligns every row inside that width (retail routine uses the
    // sprite's u+w so the spell-card name hugs the right edge of its strip).
    bool fromGameTextRows(IDirect3DDevice9 *device,const std::vector<std::string> &lines,
                          COLORREF color,unsigned codePage,int textHeight,int rowStep,
                          int textureHeight,int fontHeight,int alignWidth=0,
                          bool underline=false);

    // Uploads a tightly packed A8R8G8B8 buffer (w * h * 4 bytes).
    bool fromPixels(IDirect3DDevice9 *device, const void *pixels, int width, int height);
    bool renderTarget(IDirect3DDevice9 *device, int width, int height);
    // Reads the top-left width x height region back into a tightly packed
    // BGRA buffer. Must be called outside BeginScene/EndScene.
    bool readPixels(IDirect3DDevice9 *device, int width, int height,
                    std::vector<uint8_t> &bgra) const;

    IDirect3DTexture9 *get() const { return m_texture; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    IDirect3DTexture9 *m_texture = nullptr;
    int m_width = 0;
    int m_height = 0;
    // Only render targets are default-pool resources; images use managed
    // textures and survive Reset on their own.
    unsigned m_generation = 0;
};

// Axis-aligned quad in screen space, top-left origin.
void drawQuad(IDirect3DDevice9 *device, IDirect3DTexture9 *texture, float x, float y, float w,
              float h, DWORD tint = 0xFFFFFFFF, float u0 = 0.0f, float v0 = 0.0f,
              float u1 = 1.0f, float v1 = 1.0f, bool additive = false,
              float rotation = 0, bool opaqueTexture = false);

// Rotated quad given by its unrotated axis-aligned corners (x0,y0)-(x1,y1),
// rotated around an arbitrary center. Splits of a large sprite share the
// parent's rotation center so consecutive segments join seamlessly.
void drawQuadSegment(IDirect3DDevice9 *device, IDirect3DTexture9 *texture,
                     float x0, float y0, float x1, float y1,
                     float centerX, float centerY, float rotation,
                     DWORD tint, float u0, float v0, float u1, float v1,
                     bool additive);

// Textured triangle strip in screen space (top-left origin, texture
// coordinates normalised). The retail ANM polygon path (opcode 84, mode 9)
// feeds the effect system a ring band through this primitive.
struct TriangleVertex {
    float x;
    float y;
    float u;
    float v;
};

void drawTriangleStrip(IDirect3DDevice9 *device, IDirect3DTexture9 *texture,
                       const TriangleVertex *vertices, int count, DWORD tint,
                       bool additive);

} // namespace th95::platform
