#pragma once

namespace th95::scene {

// One entry of the retail scene table at off_4A5800. The global id is also
// the value emitted by ECL scripts and stored in a replay body at +0.
struct SceneEntry {
    int id;
    int level;
    int scene;
    const char *world;
    const char *enemy;
    const char *ecl;
    const char *bgm;
};

constexpr int kSceneLevels = 12;

int sceneCount(int level);
const SceneEntry *sceneEntryAt(int level, int scene);
// Maps the mission index used by the select screen (level*10 + scene) to an
// entry. RETURNS nullptr for an unknown id.
const SceneEntry *sceneEntry(int id);

} // namespace th95::scene
