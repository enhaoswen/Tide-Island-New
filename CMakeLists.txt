cmake_minimum_required(VERSION 3.25)

project(Tide-island VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

set(SRC
        src/app/main.cpp
        src/app/API.cpp
        src/app/main_loop.cpp
        src/app/island.cpp
        src/app/views/clock_view.cpp
        src/music/spotify_bridge.cpp
        src/command/command.cpp
        src/app/state_manager.cpp
        src/app/views/music_player.cpp
        src/app/views/music_player.hpp
        src/app/views/sysinfo.cpp
        src/app/views/sysinfo.hpp
        src/scene/context.cpp
        src/scene/context.hpp
        src/scene/struct.hpp
        src/scene/animation.cpp
        src/scene/animation.hpp
        src/scene/layout.cpp
        src/scene/render.hpp
        src/scene/input.cpp
        src/music/media.cpp
        src/music/media.hpp
        src/backend/sysinfo/sysinfo.cpp
        src/backend/sysinfo/sysinfo.hpp
        src/backend/sysinfo/source.hpp
        src/backend/sysinfo/pipewire.cpp
        src/backend/sysinfo/backlight.cpp
        src/backend/sysinfo/power.cpp
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
        PkgConfig::PIPEWIRE
        tide_utils
        tide_platform
        tide_gfx
)

target_compile_options(Tide-island PRIVATE -Wall -Wextra)

target_compile_options(Tide-island PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
)

target_compile_options(Tide-island PRIVATE
        -Wno-unused-but-set-variable
)