#include "scene_table.hpp"

#include <array>

namespace th95::scene {
namespace {
constexpr std::array<int, kSceneLevels> kSceneCounts{
    6, 6, 8, 9, 8, 8, 8, 8, 8, 8, 8, 8};
constexpr std::array<int, kSceneLevels> kSceneOffsets{
    0, 6, 12, 20, 29, 37, 45, 53, 61, 69, 77, 85};

// Dumped from off_4A5800. Each 48-byte entry holds the global scene id, the
// level/scene indices, and pointers to the world, enemy, ECL and BGM names.
constexpr std::array<SceneEntry, 93> kEntries{{
    // Level 1
    {0, 0, 0, "world01.std", "enm1.anm", "ecl1_a.ecl", "bgm/th095_01.wav"},
    {1, 0, 1, "world01.std", "enm2.anm", "ecl2_a.ecl", "bgm/th095_01.wav"},
    {2, 0, 2, "world01.std", "enm1.anm", "ecl1_b.ecl", "bgm/th095_01.wav"},
    {3, 0, 3, "world01.std", "enm2.anm", "ecl2_b.ecl", "bgm/th095_01.wav"},
    {4, 0, 4, "world01.std", "enm1.anm", "ecl1_c.ecl", "bgm/th095_01.wav"},
    {5, 0, 5, "world01.std", "enm2.anm", "ecl2_c.ecl", "bgm/th095_01.wav"},
    // Level 2
    {10, 1, 0, "world02.std", "enm3.anm", "ecl3_a.ecl", "bgm/th095_01.wav"},
    {11, 1, 1, "world02.std", "enm4.anm", "ecl4_a.ecl", "bgm/th095_01.wav"},
    {12, 1, 2, "world02.std", "enm3.anm", "ecl3_b.ecl", "bgm/th095_01.wav"},
    {13, 1, 3, "world02.std", "enm4.anm", "ecl4_b.ecl", "bgm/th095_01.wav"},
    {14, 1, 4, "world02.std", "enm3.anm", "ecl3_c.ecl", "bgm/th095_01.wav"},
    {15, 1, 5, "world02.std", "enm4.anm", "ecl4_c.ecl", "bgm/th095_01.wav"},
    // Level 3
    {20, 2, 0, "world03.std", "enm6.anm", "ecl6_a.ecl", "bgm/th095_02.wav"},
    {21, 2, 1, "world03.std", "enm5.anm", "ecl5_a.ecl", "bgm/th095_02.wav"},
    {22, 2, 2, "world03.std", "enm6.anm", "ecl6_b.ecl", "bgm/th095_02.wav"},
    {23, 2, 3, "world03.std", "enm5.anm", "ecl5_b.ecl", "bgm/th095_02.wav"},
    {24, 2, 4, "world03.std", "enm6.anm", "ecl6_c.ecl", "bgm/th095_02.wav"},
    {25, 2, 5, "world03.std", "enm5.anm", "ecl5_c.ecl", "bgm/th095_02.wav"},
    {26, 2, 6, "world03.std", "enm6.anm", "ecl6_d.ecl", "bgm/th095_02.wav"},
    {27, 2, 7, "world03.std", "enm5.anm", "ecl5_d.ecl", "bgm/th095_02.wav"},
    // Level 4
    {30, 3, 0, "world04.std", "enm7.anm", "ecl7_a.ecl", "bgm/th095_02.wav"},
    {31, 3, 1, "world04.std", "enm9.anm", "ecl9_a.ecl", "bgm/th095_02.wav"},
    {32, 3, 2, "world04.std", "enm8.anm", "ecl8_a.ecl", "bgm/th095_02.wav"},
    {33, 3, 3, "world04.std", "enm7.anm", "ecl7_b.ecl", "bgm/th095_02.wav"},
    {34, 3, 4, "world04.std", "enm9.anm", "ecl9_b.ecl", "bgm/th095_02.wav"},
    {35, 3, 5, "world04.std", "enm7.anm", "ecl7_c.ecl", "bgm/th095_02.wav"},
    {36, 3, 6, "world04.std", "enm9.anm", "ecl9_c.ecl", "bgm/th095_02.wav"},
    {37, 3, 7, "world04.std", "enm8.anm", "ecl8_b.ecl", "bgm/th095_02.wav"},
    {38, 3, 8, "world04.std", "enm7.anm", "ecl7_d.ecl", "bgm/th095_02.wav"},
    // Level 5
    {40, 4, 0, "world05.std", "enm10.anm", "ecl10_a.ecl", "bgm/th095_02.wav"},
    {41, 4, 1, "world05.std", "enm11.anm", "ecl11_a.ecl", "bgm/th095_02.wav"},
    {42, 4, 2, "world05.std", "enm10.anm", "ecl10_b.ecl", "bgm/th095_02.wav"},
    {43, 4, 3, "world05.std", "enm11.anm", "ecl11_b.ecl", "bgm/th095_02.wav"},
    {44, 4, 4, "world05.std", "enm10.anm", "ecl10_c.ecl", "bgm/th095_02.wav"},
    {45, 4, 5, "world05.std", "enm11.anm", "ecl11_c.ecl", "bgm/th095_02.wav"},
    {46, 4, 6, "world05.std", "enm10.anm", "ecl10_d.ecl", "bgm/th095_02.wav"},
    {47, 4, 7, "world05.std", "enm11.anm", "ecl11_d.ecl", "bgm/th095_02.wav"},
    // Level 6
    {50, 5, 0, "world06.std", "enm12.anm", "ecl12_a.ecl", "bgm/th095_03.wav"},
    {51, 5, 1, "world06.std", "enm13.anm", "ecl13_a.ecl", "bgm/th095_03.wav"},
    {52, 5, 2, "world06.std", "enm12.anm", "ecl12_b.ecl", "bgm/th095_03.wav"},
    {53, 5, 3, "world06.std", "enm13.anm", "ecl13_b.ecl", "bgm/th095_03.wav"},
    {54, 5, 4, "world06.std", "enm12.anm", "ecl12_c.ecl", "bgm/th095_03.wav"},
    {55, 5, 5, "world06.std", "enm13.anm", "ecl13_c.ecl", "bgm/th095_03.wav"},
    {56, 5, 6, "world06.std", "enm12.anm", "ecl12_d.ecl", "bgm/th095_03.wav"},
    {57, 5, 7, "world06.std", "enm13.anm", "ecl13_d.ecl", "bgm/th095_03.wav"},
    // Level 7
    {60, 6, 0, "world07.std", "enm14.anm", "ecl14_a.ecl", "bgm/th095_03.wav"},
    {61, 6, 1, "world07.std", "enm15.anm", "ecl15_a.ecl", "bgm/th095_03.wav"},
    {62, 6, 2, "world07.std", "enm14.anm", "ecl14_b.ecl", "bgm/th095_03.wav"},
    {63, 6, 3, "world07.std", "enm15.anm", "ecl15_b.ecl", "bgm/th095_03.wav"},
    {64, 6, 4, "world07.std", "enm14.anm", "ecl14_c.ecl", "bgm/th095_03.wav"},
    {65, 6, 5, "world07.std", "enm15.anm", "ecl15_c.ecl", "bgm/th095_03.wav"},
    {66, 6, 6, "world07.std", "enm14.anm", "ecl14_d.ecl", "bgm/th095_03.wav"},
    {67, 6, 7, "world07.std", "enm15.anm", "ecl15_d.ecl", "bgm/th095_03.wav"},
    // Level 8
    {70, 7, 0, "world08.std", "enm16.anm", "ecl16_a.ecl", "bgm/th095_04.wav"},
    {71, 7, 1, "world08.std", "enm17.anm", "ecl17_a.ecl", "bgm/th095_04.wav"},
    {72, 7, 2, "world08.std", "enm16.anm", "ecl16_b.ecl", "bgm/th095_04.wav"},
    {73, 7, 3, "world08.std", "enm17.anm", "ecl17_b.ecl", "bgm/th095_04.wav"},
    {74, 7, 4, "world08.std", "enm16.anm", "ecl16_c.ecl", "bgm/th095_04.wav"},
    {75, 7, 5, "world08.std", "enm17.anm", "ecl17_c.ecl", "bgm/th095_04.wav"},
    {76, 7, 6, "world08.std", "enm16.anm", "ecl16_d.ecl", "bgm/th095_04.wav"},
    {77, 7, 7, "world08.std", "enm17.anm", "ecl17_d.ecl", "bgm/th095_04.wav"},
    // Level 9
    {80, 8, 0, "world09.std", "enm18.anm", "ecl18_a.ecl", "bgm/th095_04.wav"},
    {81, 8, 1, "world09.std", "enm19.anm", "ecl19_a.ecl", "bgm/th095_04.wav"},
    {82, 8, 2, "world09.std", "enm18.anm", "ecl18_b.ecl", "bgm/th095_04.wav"},
    {83, 8, 3, "world09.std", "enm19.anm", "ecl19_b.ecl", "bgm/th095_04.wav"},
    {84, 8, 4, "world09.std", "enm18.anm", "ecl18_c.ecl", "bgm/th095_04.wav"},
    {85, 8, 5, "world09.std", "enm19.anm", "ecl19_c.ecl", "bgm/th095_04.wav"},
    {86, 8, 6, "world09.std", "enm18.anm", "ecl18_d.ecl", "bgm/th095_04.wav"},
    {87, 8, 7, "world09.std", "enm19.anm", "ecl19_d.ecl", "bgm/th095_04.wav"},
    // Level 10
    {90, 9, 0, "world10.std", "enm20.anm", "ecl20_a.ecl", "bgm/th095_04.wav"},
    {91, 9, 1, "world10.std", "enm21.anm", "ecl21_a.ecl", "bgm/th095_04.wav"},
    {92, 9, 2, "world10.std", "enm20.anm", "ecl20_b.ecl", "bgm/th095_04.wav"},
    {93, 9, 3, "world10.std", "enm21.anm", "ecl21_b.ecl", "bgm/th095_04.wav"},
    {94, 9, 4, "world10.std", "enm20.anm", "ecl20_c.ecl", "bgm/th095_04.wav"},
    {95, 9, 5, "world10.std", "enm21.anm", "ecl21_c.ecl", "bgm/th095_04.wav"},
    {96, 9, 6, "world10.std", "enm20.anm", "ecl20_d.ecl", "bgm/th095_04.wav"},
    {97, 9, 7, "world10.std", "enm21.anm", "ecl21_d.ecl", "bgm/th09_08_2.wav"},
    // Extra stages
    {100, 10, 0, "world05.std", "enm22.anm", "ecl22_a.ecl", "bgm/th095_03.wav"},
    {101, 10, 1, "world05.std", "enm22.anm", "ecl22_b.ecl", "bgm/th095_03.wav"},
    {102, 10, 2, "world06.std", "enm23.anm", "ecl23_a.ecl", "bgm/th095_03.wav"},
    {103, 10, 3, "world06.std", "enm23.anm", "ecl23_b.ecl", "bgm/th095_03.wav"},
    {104, 10, 4, "world09.std", "enm24.anm", "ecl24_a.ecl", "bgm/th095_03.wav"},
    {105, 10, 5, "world09.std", "enm24.anm", "ecl24_b.ecl", "bgm/th095_03.wav"},
    {106, 10, 6, "world10.std", "enm25.anm", "ecl25_a.ecl", "bgm/th095_03.wav"},
    {107, 10, 7, "world10.std", "enm25.anm", "ecl25_b.ecl", "bgm/th095_03.wav"},
    {110, 11, 0, "world05.std", "enm22.anm", "ecl22_a.ecl", "bgm/th095_03.wav"},
    {111, 11, 1, "world05.std", "enm22.anm", "ecl22_b.ecl", "bgm/th095_03.wav"},
    {112, 11, 2, "world06.std", "enm23.anm", "ecl23_a.ecl", "bgm/th095_03.wav"},
    {113, 11, 3, "world06.std", "enm23.anm", "ecl23_b.ecl", "bgm/th095_03.wav"},
    {114, 11, 4, "world09.std", "enm24.anm", "ecl24_a.ecl", "bgm/th095_03.wav"},
    {115, 11, 5, "world09.std", "enm24.anm", "ecl24_b.ecl", "bgm/th095_03.wav"},
    {116, 11, 6, "world10.std", "enm25.anm", "ecl25_a.ecl", "bgm/th095_03.wav"},
    {117, 11, 7, "world10.std", "enm25.anm", "ecl25_b.ecl", "bgm/th095_03.wav"},
}};
} // namespace

int sceneCount(int level)
{
    if (level < 0 || level >= kSceneLevels) {
        return 0;
    }
    return kSceneCounts[std::size_t(level)];
}

const SceneEntry *sceneEntryAt(int level, int scene)
{
    if (level < 0 || level >= kSceneLevels || scene < 0 || scene >= sceneCount(level)) {
        return nullptr;
    }
    return &kEntries[std::size_t(kSceneOffsets[std::size_t(level)] + scene)];
}

const SceneEntry *sceneEntry(int id)
{
    const SceneEntry *entry = sceneEntryAt(id / 10, id % 10);
    return entry && entry->id == id ? entry : nullptr;
}

} // namespace th95::scene
