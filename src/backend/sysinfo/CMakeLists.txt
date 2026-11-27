add_library(tide_sysinfo STATIC
        backlight.cpp
        pipewire.cpp
        power.cpp
        sysinfo.cpp
)

target_link_libraries(tide_sysinfo
        PUBLIC tide_utils
        PRIVATE tide_platform
        PkgConfig::PIPEWIRE
)

target_compile_options(tide_sysinfo PRIVATE -Wall -Wextra -Wpedantic -Werror)
