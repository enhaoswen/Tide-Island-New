cmake_minimum_required(VERSION 3.31)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(lyricsmpris LANGUAGES CXX)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LYRICS_SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(LYRICS_CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(LYRICS_UTF8 REQUIRED IMPORTED_TARGET libutf8proc)

if(NOT TARGET glaze::glaze)
    find_package(glaze 9.0.0 EXACT CONFIG QUIET)
    if(NOT glaze_FOUND)
        include(FetchContent)
        FetchContent_Declare(glaze
            # v9.0.0, pinned by commit and archive checksum.
            URL https://codeload.github.com/stephenberry/glaze/tar.gz/d78832c82289c61a9315bfbc35332cec9f4e93ca
            URL_HASH SHA256=72e88f46a96e3cd014fb26af644c540a11811407fb1332fb0a4e068ceec5ba3f
        )
        FetchContent_MakeAvailable(glaze)
    endif()
endif()

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
    glaze::glaze PkgConfig::LYRICS_UTF8
)
target_compile_options(lyricsmpris PRIVATE -Wall -Wextra -Wpedantic)

option(LYRICSMPRIS_BUILD_TESTS "Build lyricsmpris module tests" OFF)
if(LYRICSMPRIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
