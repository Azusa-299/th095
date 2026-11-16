#pragma once
#include "../formats/anm.hpp"
#include "../formats/ecl.hpp"

namespace th95::scene {
// STD geometry, placements and camera timeline. World coordinates are never
// inferred from atlas coordinates. Only world01's opcodes are needed by LV1.
// Camera: op2 sets the eye point, op4/op5 set the FACING DIRECTION vector the
// camera looks along (retail routine builds LookAtLH(eye, eye+facing, up) and the
// frame start normalizes it), not a look-at target point.
// Sprites: the STD record's script selects an ANM script whose opcode 67 low
// nibble picks the render path (retail routine): 8 draws the ground as a world
// matrix quad spanning [-150,+140]/256 * record size (unit quad in retail routine),
// everything else is a screen-space billboard (retail routine -> retail routine) whose
// on-screen size is pixels-per-world-unit (measured along the camera right
// axis, camera struct +48) * record size / 2, centered on the projected point.
class LevelWorld {
public:
    bool load(Archive&,IDirect3DDevice9*,const char* stdName="world01.std");
    void restart();
    void update(float scale);
    void draw(IDirect3DDevice9*,DWORD modulation=0x80808080) const;
private:
    using Vec=std::array<float,3>;
    struct Instance {unsigned object;Vec position;};
    struct Tween {Vec from{},to{};float time=0,duration=0;int mode=0;};
    format::AnmTexture m_texture;
    format::StdFile m_model;
    std::vector<Instance> m_instances;
    std::vector<int> m_renderType;
    std::vector<format::EclCommand> m_script;
    std::size_t m_pc=0;
    float m_clock=0,m_fov=.5235988f,m_fogStart=240,m_fogEnd=800;
    Vec m_eye{{0,0,-100}},m_facing{{0,270,600}},m_up{{0,1,0}};
    Tween m_eyeTween,m_facingTween,m_fogTween;
};
}
