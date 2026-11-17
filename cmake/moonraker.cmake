#
# Moonraker client library and its third-party dependencies.
#
# Usable from the root project and from tests/CMakeLists.txt when the tests
# are configured standalone (no LVGL / SDL needed).
#
# Inputs (optional):
#   KDZ_SOURCE_DIR          repository root (defaults to this file's parent)
#   KDZ_EXTRA_INCLUDE_DIRS  system include dirs added to fetched deps (cross builds)
#   KDZ_THREAD_LIBS         thread libraries to link (defaults to Threads::Threads)
#
# Output target:
#   kdz_moonraker
#

include_guard(GLOBAL)
include(FetchContent)

if(NOT KDZ_SOURCE_DIR)
    get_filename_component(KDZ_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

# =========================
# nlohmann/json (header only)
# =========================
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    set(JSON_Install OFF CACHE INTERNAL "")
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# =========================
# IXWebSocket (TLS / zlib disabled: Moonraker is plain ws:// on the LAN)
# =========================
if(NOT TARGET ixwebsocket)
    FetchContent_Declare(
        ixwebsocket
        GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
        GIT_TAG        v11.4.6
        GIT_SHALLOW    TRUE
    )
    set(USE_TLS OFF CACHE BOOL "" FORCE)
    set(USE_ZLIB OFF CACHE BOOL "" FORCE)
    set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ixwebsocket)
    if(KDZ_EXTRA_INCLUDE_DIRS)
        target_include_directories(ixwebsocket SYSTEM BEFORE PRIVATE ${KDZ_EXTRA_INCLUDE_DIRS})
    endif()
endif()

if(NOT KDZ_THREAD_LIBS)
    find_package(Threads REQUIRED)
    set(KDZ_THREAD_LIBS Threads::Threads)
endif()

# =========================
# kdz_moonraker
# =========================
if(NOT TARGET kdz_moonraker)
    file(GLOB_RECURSE KDZ_MOONRAKER_SOURCES CONFIGURE_DEPENDS
        "${KDZ_SOURCE_DIR}/src/moonraker/*.cpp"
    )

    add_library(kdz_moonraker STATIC ${KDZ_MOONRAKER_SOURCES})

    target_include_directories(kdz_moonraker
        PUBLIC
            "${KDZ_SOURCE_DIR}/src/moonraker"
            "${KDZ_SOURCE_DIR}/src/model"
    )

    target_link_libraries(kdz_moonraker
        PUBLIC
            nlohmann_json::nlohmann_json
        PRIVATE
            ixwebsocket
            ${KDZ_THREAD_LIBS}
    )

    if(KDZ_EXTRA_INCLUDE_DIRS)
        target_include_directories(kdz_moonraker SYSTEM BEFORE PRIVATE ${KDZ_EXTRA_INCLUDE_DIRS})
    endif()
endif()
