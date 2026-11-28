cmake_minimum_required(VERSION 3.25)

project(Tide-island VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TIDE_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(TIDE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

find_package(OpenGL REQUIRED)
find_package(Freetype REQUIRED)

add_subdirectory(src/lyricsmpris)
add_subdirectory(src/utils)
add_subdirectory(src/backend)
add_subdirectory(src/graphics)
add_subdirectory(src/scene)
add_subdirectory(src/backend/sysinfo)

set(SRC
        src/app/main.cpp
        src/app/API.cpp
        src/app/main_loop.cpp
        src/app/island.cpp
        src/app/settings.cpp
        src/app/views/clock_view.cpp
        src/music/spotify_bridge.cpp
        src/command/command.cpp
        src/app/state_manager.cpp
        src/app/views/music_player.cpp
        src/app/views/sysinfo.cpp
        src/music/media.cpp
)

add_executable(Tide-island ${SRC})

target_compile_definitions(Tide-island PRIVATE SOKOL_GLES3 TIDE_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resource")

target_include_directories(Tide-island PRIVATE
        src/
        third_party/
        third_party/wayland/
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${WAYLAND_EGL_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIR}
        ${HARFBUZZ_INCLUDE_DIRS}
)

target_link_libraries(Tide-island PRIVATE
        ${WAYLAND_CLIENT_LIBRARIES}
        ${WAYLAND_EGL_LIBRARIES}
        ${HARFBUZZ_LIBRARIES}
        PkgConfig::FONTCONFIG
        OpenGL::GLES3
        OpenGL::EGL
        Freetype::Freetype
        lyricsmpris_glaze
        LyricsMpris::LyricsMpris
        tide_utils
        tide_platform
        tide_gfx
        tide_scene
        tide_sysinfo
)

target_compile_options(Tide-island PRIVATE -Wall -Wextra -Wpedantic -Werror)

target_compile_options(Tide-island PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
)

target_compile_options(Tide-island PRIVATE
        -Wno-unused-but-set-variable
)