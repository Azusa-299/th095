#pragma once
// Shared, translation-unit-local helpers for the level scene split.
//
// The scene was one translation unit; these symbols are used by more than one
// concern (photo capture, rendering, visual sync), so they live here instead
// of being duplicated. Everything has internal linkage and is data-only.

#include <cstdint>
#include <vector>
#include "level.hpp"

namespace th95::scene {
namespace {

// Script ids and draw layers for the 24 bullet types, indexed by type.
[[maybe_unused]] constexpr int bulletScripts[] = {
    0, 16, 32, 48, 64, 80, 96, 111, 127, 143, 159, 175, 211, 219, 227,
    235, 243, 283, 296, 251, 259, 267, 275, 191};
[[maybe_unused]] constexpr int bulletLayers[] = {
    5, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 1, 2, 1, 2, 2, 0, 2, 2, 2, 1, 2, 3};

[[maybe_unused]] inline std::vector<uint8_t> extract(Archive &archive, const char *name)
{
    const auto *entry = archive.find(name);
    return entry ? archive.extract(*entry) : std::vector<uint8_t>{};
}

// Render-to-texture copies stay on the GPU and are legal inside BeginScene.
struct Target {
    IDirect3DDevice9 *d;
    IDirect3DSurface9 *previous = nullptr;
    D3DVIEWPORT9 viewport{};
    bool active = false;
    Target(IDirect3DDevice9 *device, platform::Texture &texture) : d(device)
    {
        IDirect3DSurface9 *surface = nullptr;
        if (!texture.get() || FAILED(d->GetRenderTarget(0, &previous))) return;
        d->GetViewport(&viewport);
        if (SUCCEEDED(texture.get()->GetSurfaceLevel(0, &surface))) {
            active = SUCCEEDED(d->SetRenderTarget(0, surface));
            surface->Release();
        }
        if (active) {
            D3DVIEWPORT9 v{0, 0, UINT(texture.width()), UINT(texture.height()), 0, 1};
            d->SetViewport(&v);
            d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0);
        }
    }
    ~Target()
    {
        if (previous) {
            if (active) {
                d->SetRenderTarget(0, previous);
                d->SetViewport(&viewport);
            }
            previous->Release();
        }
    }
};

} // namespace
} // namespace th95::scene
