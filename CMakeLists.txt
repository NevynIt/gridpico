cmake_minimum_required(VERSION 3.16)
project(gridpico LANGUAGES C)

add_library(gridpico_native_fixture STATIC
    core/midi/native_graph.c
    core/processors/transpose.c
    core/session/session.c
    sim/platform/midi_fixture.c
)
target_include_directories(gridpico_native_fixture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gridpico_native_proof app/native_proof.c)
target_link_libraries(gridpico_native_proof PRIVATE gridpico_native_fixture)

add_executable(gridpico_browser_fixture
    app/browser_fixture.c
    app/runner_io.c
    frontends/apc/apc_frontend.c
    frontends/apc/apc_protocol.c
)
target_link_libraries(gridpico_browser_fixture PRIVATE gridpico_native_fixture)

add_executable(gridpico_patch_demo
    app/patch_demo.c
    core/midi/patch_graph.c
    core/session/patch_session.c
    sim/platform/patch_fixture.c
)
target_include_directories(gridpico_patch_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(gridpico_patch_runtime STATIC
    app/patch_runtime.c
    core/midi/patch_graph.c
    core/session/patch_session.c
    frontends/apc/apc_patch.c
    frontends/apc/apc_protocol.c
    frontends/cli/cli_frontend.c
    sim/platform/patch_fixture.c
)
target_include_directories(gridpico_patch_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gridpico_patch_runner app/patch_runner.c app/runner_io.c)
target_link_libraries(gridpico_patch_runner PRIVATE gridpico_patch_runtime)

add_library(gridpico_local_runtime STATIC
    app/local_runtime.c
    core/midi/patch_graph.c
    core/session/patch_session.c
    frontends/apc/apc_patch.c
    frontends/apc/apc_protocol.c
    frontends/cli/cli_frontend.c
)
target_include_directories(gridpico_local_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gridpico_local_router app/local_router.c)
target_link_libraries(gridpico_local_router PRIVATE gridpico_local_runtime)

set(gridpico_asset_dir "${CMAKE_CURRENT_BINARY_DIR}/GRIDPICO")
add_custom_target(gridpico_offline_assets
    COMMAND "${CMAKE_COMMAND}"
            "-Dsource_dir=${CMAKE_CURRENT_SOURCE_DIR}"
            "-Dasset_dir=${gridpico_asset_dir}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/app/stage_gridpico_assets.cmake"
    BYPRODUCTS
        "${gridpico_asset_dir}/INDEX.HTM"
        "${gridpico_asset_dir}/DIAG.HTM"
        "${gridpico_asset_dir}/VERSION.TXT"
    COMMENT "Staging GRIDPICO offline HTML assets"
    VERBATIM
)

set(fixture_targets
    gridpico_native_fixture
    gridpico_native_proof
    gridpico_browser_fixture
    gridpico_patch_demo
    gridpico_patch_runtime
    gridpico_patch_runner
    gridpico_local_runtime
    gridpico_local_router
)
set_target_properties(${fixture_targets}
    PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
    foreach(target IN LISTS fixture_targets)
        target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()
