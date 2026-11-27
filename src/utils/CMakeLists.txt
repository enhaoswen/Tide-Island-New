add_library(tide_utils STATIC color.cpp)

target_include_directories(tide_utils PUBLIC ${PROJECT_SOURCE_DIR}/src)

target_compile_features(tide_utils PUBLIC cxx_std_23)

target_link_libraries(tide_utils PUBLIC lyricsmpris_glaze)

target_compile_options(tide_utils PRIVATE -Wall -Wextra)