add_library(tide_scene STATIC
        animation.cpp
        context.cpp
        input.cpp
        layout.cpp
)

target_link_libraries(tide_scene
        PUBLIC tide_utils
        tide_gfx
)

target_compile_options(tide_scene PRIVATE -Wall -Wextra -Wpedantic -Werror)