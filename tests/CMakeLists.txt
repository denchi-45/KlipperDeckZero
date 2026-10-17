#
# Unit tests for the non-UI parts (Moonraker client, config).
#
# Built from the root project when KDZ_BUILD_TESTS=ON, or standalone:
#   cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
#

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(klipper-deck-zero_tests LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_testing()
endif()

get_filename_component(KDZ_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
include("${KDZ_SOURCE_DIR}/cmake/moonraker.cmake")

add_executable(kdz_tests
    test_main.cpp
    test_status_merge.cpp
    test_jsonrpc_client.cpp
    test_session.cpp
    test_config.cpp
    test_printer_format.cpp
    test_settings_form.cpp
    test_restart_confirm.cpp
    test_temperature_panel.cpp
    test_control_panel.cpp
    test_extruder_panel.cpp
    test_file_browser.cpp
    "${KDZ_SOURCE_DIR}/src/app/application_config.cpp"
    "${KDZ_SOURCE_DIR}/src/app/control_panel.cpp"
    "${KDZ_SOURCE_DIR}/src/app/extruder_panel.cpp"
    "${KDZ_SOURCE_DIR}/src/app/file_browser.cpp"
    "${KDZ_SOURCE_DIR}/src/app/file_list_parser.cpp"
    "${KDZ_SOURCE_DIR}/src/app/gcode.cpp"
    "${KDZ_SOURCE_DIR}/src/app/gcode_files.cpp"
    "${KDZ_SOURCE_DIR}/src/app/restart_confirm.cpp"
    "${KDZ_SOURCE_DIR}/src/app/settings_form.cpp"
    "${KDZ_SOURCE_DIR}/src/app/settings_store.cpp"
    "${KDZ_SOURCE_DIR}/src/app/temperature_panel.cpp"
    "${KDZ_SOURCE_DIR}/src/viewmodel/printer_format.cpp"
)

target_include_directories(kdz_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${KDZ_SOURCE_DIR}/src/app"
    "${KDZ_SOURCE_DIR}/src/viewmodel"
)

target_link_libraries(kdz_tests PRIVATE kdz_moonraker)

add_test(NAME kdz_tests COMMAND kdz_tests)
