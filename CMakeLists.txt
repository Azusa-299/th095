cmake_minimum_required(VERSION 3.20)
project(th95_reimpl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

set(TH95_CORE_SRC
    src/log.cpp
    src/crypto.cpp
    src/lzss.cpp
    src/archive.cpp
    src/formats/score.cpp
    src/formats/replay.cpp
)

add_library(th95-core STATIC ${TH95_CORE_SRC})
target_include_directories(th95-core PUBLIC src)

set(TH95_SIM_SRC
    src/formats/ecl.cpp
    src/scenes/level_sim.cpp
    src/scenes/level_sim_vm.cpp
    src/scenes/level_sim_actor.cpp
    src/scenes/level_sim_tick.cpp
    src/scenes/level_sim_photo.cpp
)

option(TH95_DIAGNOSTIC "Tag baseline reports as diagnostic builds" OFF)

# Headless deterministic baseline: fixed inputs, per-frame hashes, golden diff.
add_executable(th95-baseline
    tools/baseline.cpp
    src/sim/input_script.cpp
    src/sim/frame_trace.cpp
    src/scenes/scene_table.cpp
    ${TH95_SIM_SRC}
)
target_include_directories(th95-baseline PRIVATE src)
target_link_libraries(th95-baseline PRIVATE th95-core)
if(MINGW)
    target_link_options(th95-baseline PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(TH95_DIAGNOSTIC)
    target_compile_definitions(th95-baseline PRIVATE TH95_DIAGNOSTIC=1)
endif()

set(SRC
    src/main.cpp
    src/formats/anm.cpp
    src/formats/bestshot.cpp
    src/formats/mission.cpp
    src/formats/music_cmt.cpp
    src/formats/config.cpp
    src/platform/window.cpp
    src/platform/renderer.cpp
    src/platform/gfx.cpp
    src/platform/audio.cpp
    src/platform/input.cpp
    src/scenes/title.cpp
    src/scenes/help.cpp
    src/scenes/scene_table.cpp
    src/scenes/level_world.cpp
    src/scenes/level_load.cpp
    src/scenes/level_update.cpp
    src/scenes/level_replay.cpp
    src/scenes/level_photo.cpp
    src/scenes/level_render.cpp
    src/scenes/mission_select.cpp
    src/scenes/music_room.cpp
    src/scenes/option.cpp
    src/scenes/startup.cpp
    ${TH95_SIM_SRC}
)

add_executable(th95 ${SRC})

target_include_directories(th95 PRIVATE src)

target_link_libraries(th95 PRIVATE th95-core
    d3d9
    dsound
    dxguid
    gdiplus
    ole32
    user32
    gdi32
    winmm
    shell32
)

if(MINGW)
    target_link_options(th95 PRIVATE -static -static-libgcc -static-libstdc++)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(th95-fixtures tests/fixtures.cpp src/formats/anm.cpp
        src/formats/bestshot.cpp
        src/formats/mission.cpp src/formats/music_cmt.cpp src/formats/config.cpp
        src/platform/gfx.cpp src/platform/audio.cpp src/scenes/help.cpp
        src/scenes/level_world.cpp
        src/scenes/scene_table.cpp
        src/scenes/level_load.cpp src/scenes/level_update.cpp
        src/scenes/level_replay.cpp
        src/scenes/level_photo.cpp src/scenes/level_render.cpp
        src/scenes/mission_select.cpp src/scenes/music_room.cpp src/scenes/option.cpp
        ${TH95_SIM_SRC})
    target_include_directories(th95-fixtures PRIVATE src)
    target_link_libraries(th95-fixtures PRIVATE th95-core d3d9 dsound dxguid gdiplus gdi32 user32 ole32 winmm)
    if(MINGW)
        target_link_options(th95-fixtures PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    set(SCORE_FIXTURE_ARGS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/th95/全开档/scoreth095.dat")
        configure_file("${CMAKE_SOURCE_DIR}/th95/全开档/scoreth095.dat"
            "${CMAKE_BINARY_DIR}/score-fixture-full.dat" COPYONLY)
        list(APPEND SCORE_FIXTURE_ARGS "${CMAKE_BINARY_DIR}/score-fixture-full.dat")
    endif()
    foreach(variant th095 th095c)
        if(EXISTS "${CMAKE_SOURCE_DIR}/build/${variant}.dat")
            add_test(NAME ${variant}-fixtures COMMAND th95-fixtures
                "${CMAKE_SOURCE_DIR}/build/${variant}.dat" ${SCORE_FIXTURE_ARGS})
            set_tests_properties(${variant}-fixtures PROPERTIES TIMEOUT 30)
        endif()
    endforeach()
    add_executable(th95-numeric-tests tests/numeric_tests.cpp)
    target_include_directories(th95-numeric-tests PRIVATE src)
    add_test(NAME numeric-unit COMMAND th95-numeric-tests)
    # Dynamic baseline: fixed input, per-frame state/layout/audio/effect hashes.
    # The JP archive is the recorded reference; the CN data differs in LV1-3..6.
    if(EXISTS "${CMAKE_SOURCE_DIR}/build/th095.dat")
        foreach(index RANGE 0 5)
            math(EXPR number "${index} + 1")
            add_test(NAME baseline-lv1-${number} COMMAND th95-baseline
                --archive "${CMAKE_SOURCE_DIR}/build/th095.dat"
                --mission ${index}
                --script "${CMAKE_SOURCE_DIR}/tests/baselines/lv1.input"
                --check "${CMAKE_SOURCE_DIR}/tests/baselines/lv1-${number}.jsonl"
                --report "${CMAKE_BINARY_DIR}/baseline-lv1-${number}.json"
                --fail-on-unsupported)
            set_tests_properties(baseline-lv1-${number} PROPERTIES TIMEOUT 120)
        endforeach()
    endif()
endif()
