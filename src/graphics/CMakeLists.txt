add_library(tide_gfx STATIC
        driver_memory.cpp
        image.cpp
        renderer.cpp
        text.cpp
        texture.cpp
)

target_compile_definitions(tide_gfx PRIVATE SOKOL_GLES3)

target_include_directories(tide_gfx
        PRIVATE ${PROJECT_SOURCE_DIR}/third_party
        ${HARFBUZZ_INCLUDE_DIRS}
)

target_link_libraries(tide_gfx
        PUBLIC tide_utils
        PRIVATE tide_platform
        ${HARFBUZZ_LIBRARIES}
        PkgConfig::FONTCONFIG
        Freetype::Freetype
        OpenGL::GLES3
)

target_compile_options(tide_gfx PRIVATE -Wall -Wextra -Wpedantic -Werror)
