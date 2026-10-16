/*
 * SPDX-License-Identifier: MIT
 */

#include "application_config.h"
#include "test_framework.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string write_temp_config(const std::string& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << contents;
    return path.string();
}

} // namespace

TEST_CASE("config: moonraker section is parsed") {
    const auto path = write_temp_config("kdz_test_config.conf",
                                        "[application]\n"
                                        "dark_mode=yes\n"
                                        "\n"
                                        "[moonraker]\n"
                                        "transport = websocket\n"
                                        "socket_path=/tmp/moonraker.sock\n"
                                        "url = ws://192.168.1.50:7125/websocket\n"
                                        "api_key=abc123\n");
    app::ApplicationConfig config;
    std::string error;
    CHECK(app::load_application_config(path, config, error));
    CHECK(config.dark_mode);
    CHECK(config.moonraker.transport == app::MoonrakerTransportKind::WebSocket);
    CHECK_EQ(config.moonraker.socket_path, std::string("/tmp/moonraker.sock"));
    CHECK_EQ(config.moonraker.url, std::string("ws://192.168.1.50:7125/websocket"));
    CHECK_EQ(config.moonraker.api_key, std::string("abc123"));
    std::remove(path.c_str());
}

TEST_CASE("config: invalid transport is rejected") {
    const auto path = write_temp_config("kdz_test_bad.conf", "[moonraker]\ntransport=serial\n");
    app::ApplicationConfig config;
    std::string error;
    CHECK(!app::load_application_config(path, config, error));
    CHECK(error.find("transport") != std::string::npos);
    std::remove(path.c_str());
}

TEST_CASE("config: saving dark_mode keeps the moonraker section") {
    const auto path = write_temp_config("kdz_test_save.conf",
                                        "[application]\n"
                                        "dark_mode=no\n"
                                        "\n"
                                        "[moonraker]\n"
                                        "transport=unix\n");
    app::ApplicationConfig config;
    config.dark_mode = true;
    std::string error;
    CHECK(app::save_application_config(path, config, error));

    app::ApplicationConfig loaded;
    CHECK(app::load_application_config(path, loaded, error));
    CHECK(loaded.dark_mode);
    CHECK(loaded.moonraker.transport == app::MoonrakerTransportKind::UnixSocket);
    std::remove(path.c_str());
}

TEST_CASE("config: expand_user_path") {
    CHECK_EQ(app::expand_user_path("/abs/path"), std::string("/abs/path"));
    CHECK_EQ(app::expand_user_path("~"), std::string("~"));
    const char* home = std::getenv("HOME");
    if (home && *home) {
        CHECK_EQ(app::expand_user_path("~/printer_data/comms/moonraker.sock"),
                 std::string(home) + "/printer_data/comms/moonraker.sock");
    }
}
