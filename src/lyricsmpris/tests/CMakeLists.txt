add_executable(lyricsmpris_core_tests core.cpp)
target_link_libraries(lyricsmpris_core_tests PRIVATE LyricsMpris::LyricsMpris)
add_test(NAME lyricsmpris_core COMMAND lyricsmpris_core_tests)

add_executable(lyricsmpris_http_tests http.cpp)
target_link_libraries(lyricsmpris_http_tests PRIVATE LyricsMpris::LyricsMpris PkgConfig::LYRICS_CURL)
add_test(NAME lyricsmpris_http COMMAND lyricsmpris_http_tests)
set_tests_properties(lyricsmpris_http PROPERTIES TIMEOUT 15)

find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_executable(lyricsmpris_runtime_tests runtime.cpp)
target_link_libraries(lyricsmpris_runtime_tests PRIVATE LyricsMpris::LyricsMpris PkgConfig::LYRICS_SYSTEMD)
add_test(NAME lyricsmpris_runtime COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:lyricsmpris_runtime_tests>)
set_tests_properties(lyricsmpris_runtime PROPERTIES TIMEOUT 20)
