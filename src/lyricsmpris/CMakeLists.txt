cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(lyricsmpris LANGUAGES CXX)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LYRICS_SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(LYRICS_CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(LYRICS_UTF8 REQUIRED IMPORTED_TARGET libutf8proc)

add_library(lyricsmpris_glaze INTERFACE)
target_include_directories(lyricsmpris_glaze SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/glaze/include"
)
target_compile_features(lyricsmpris_glaze INTERFACE cxx_std_23)

add_library(lyricsmpris STATIC
    Lyrics.cpp
    Client.cpp
    detail/Normalize.cpp
    detail/Http.cpp
    detail/Mpris.cpp
    detail/Providers.cpp
    detail/Resolver.cpp
)
add_library(LyricsMpris::LyricsMpris ALIAS lyricsmpris)
target_compile_features(lyricsmpris PUBLIC cxx_std_23)
target_include_directories(lyricsmpris PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(lyricsmpris PRIVATE
    PkgConfig::LYRICS_SYSTEMD PkgConfig::LYRICS_CURL
    lyricsmpris_glaze PkgConfig::LYRICS_UTF8
)
target_compile_options(lyricsmpris PRIVATE -Wall -Wextra -Wpedantic)

option(LYRICSMPRIS_BUILD_TESTS "Build lyricsmpris module tests" OFF)
if(LYRICSMPRIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
