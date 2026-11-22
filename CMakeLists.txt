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
    frontends/apc/apc_frontend.c
    frontends/apc/apc_protocol.c
)
target_link_libraries(gridpico_browser_fixture PRIVATE gridpico_native_fixture)

set_target_properties(gridpico_native_fixture gridpico_native_proof gridpico_browser_fixture
    PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
    foreach(target IN ITEMS gridpico_native_fixture gridpico_native_proof gridpico_browser_fixture)
        target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()
