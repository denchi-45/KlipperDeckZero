#
# Unit tests for the non-UI parts (Moonraker client, config).
#
# Built from the root project when KDZ_BUILD_TESTS=ON, or standalone:
#   cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
#

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(klipper_deck_zero_tests LANGUAGES C CXX)
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
    "${KDZ_SOURCE_DIR}/src/app/application_config.cpp"
    "${KDZ_SOURCE_DIR}/src/viewmodel/printer_format.cpp"
)

target_include_directories(kdz_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${KDZ_SOURCE_DIR}/src/app"
    "${KDZ_SOURCE_DIR}/src/viewmodel"
)

target_link_libraries(kdz_tests PRIVATE kdz_moonraker)

add_test(NAME kdz_tests COMMAND kdz_tests)
