add_library(tide_platform STATIC
        config.cpp
        egl_vendor.cpp
        timer.cpp
        wayland.cpp
        ${PROJECT_SOURCE_DIR}/third_party/wayland/wlr-layer-shell-unstable-v1-protocol.c
        ${PROJECT_SOURCE_DIR}/third_party/wayland/xdg-shell-client-protocol.c
        ${PROJECT_SOURCE_DIR}/third_party/wayland/fractional-scale-v1-protocol.c
        ${PROJECT_SOURCE_DIR}/third_party/wayland/viewporter-protocol.c
        ${PROJECT_SOURCE_DIR}/third_party/wayland/ext-background-effect-v1-protocol.c
)

target_include_directories(tide_platform
        PUBLIC ${PROJECT_SOURCE_DIR}/src
        PRIVATE ${PROJECT_SOURCE_DIR}/third_party/wayland
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${WAYLAND_EGL_INCLUDE_DIRS}
)

target_link_libraries(tide_platform
        PUBLIC tide_utils
        PRIVATE ${WAYLAND_CLIENT_LIBRARIES}
        ${WAYLAND_EGL_LIBRARIES}
        OpenGL::EGL
        OpenGL::GLES3
)

target_compile_options(tide_platform PRIVATE -Wall -Wextra)