/*
 * SPDX-License-Identifier: MIT
 */

#include "application_config.h"
#include "test_framework.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

namespace {

std::string write_temp_config(const std::string& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << contents;
    return path.string();
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("config: every setting has one table row") {
    std::set<std::string> keys;
    for (const auto& descriptor : app::setting_descriptors()) {
        CHECK(&app::setting_descriptor(descriptor.id) == &descriptor);
        CHECK(keys.insert(std::string(descriptor.section) + "." + descriptor.key).second);
    }
    CHECK_EQ(app::setting_descriptors().size(),
             static_cast<std::size_t>(app::Setting::MoonrakerApiKey) + 1);
}

TEST_CASE("config: defaults point at the Klipper on this device") {
    const app::MoonrakerConfig config;
    CHECK(config.transport == app::MoonrakerConfig::default_transport());
    CHECK_EQ(config.socket_path, std::string("~/printer_data/comms/moonraker.sock"));
    CHECK_EQ(config.websocket_url(), std::string("ws://127.0.0.1:7125/websocket"));
}

TEST_CASE("config: moonraker section is parsed") {
    const auto path = write_temp_config("kdz_test_config.conf",
                                        "[application]\n"
                                        "dark_mode=yes\n"
                                        "\n"
                                        "[moonraker]\n"
                                        "transport = websocket\n"
                                        "socket_path=/tmp/moonraker.sock\n"
                                        "host = 192.168.1.50\n"
                                        "port=7130\n"
                                        "api_key=abc123\n");
    app::ApplicationConfig config;
    std::string error;
    CHECK(app::load_application_config(path, config, error));
    CHECK(config.dark_mode);
    CHECK(config.moonraker.transport == app::MoonrakerTransportKind::WebSocket);
    CHECK_EQ(config.moonraker.socket_path, std::string("/tmp/moonraker.sock"));
    CHECK_EQ(config.moonraker.host, std::string("192.168.1.50"));
    CHECK_EQ(config.moonraker.port, 7130);
    CHECK_EQ(config.moonraker.websocket_url(), std::string("ws://192.168.1.50:7130/websocket"));
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

TEST_CASE("config: invalid port is rejected") {
    for (const char* port : {"0", "65536", "71a5", "", "-1"}) {
        const auto path = write_temp_config("kdz_test_bad_port.conf",
                                            std::string("[moonraker]\nport=") + port + "\n");
        app::ApplicationConfig config;
        std::string error;
        CHECK(!app::load_application_config(path, config, error));
        CHECK(error.find("port") != std::string::npos);
        CHECK_EQ(config.moonraker.port, 7125);
        std::remove(path.c_str());
    }
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
    CHECK(app::save_application_config(path, config, {app::Setting::DarkMode}, error));

    app::ApplicationConfig loaded;
    CHECK(app::load_application_config(path, loaded, error));
    CHECK(loaded.dark_mode);
    CHECK(loaded.moonraker.transport == app::MoonrakerTransportKind::UnixSocket);
    std::remove(path.c_str());
}

TEST_CASE("config: saving writes only the given settings and keeps comments") {
    const auto path = write_temp_config("kdz_test_save_many.conf",
                                        "# user config\n"
                                        "[moonraker]\n"
                                        "# printer address\n"
                                        "host=10.0.0.2\n"
                                        "host=10.0.0.3\n"
                                        "socket_path=/run/moonraker.sock\n"
                                        "\n"
                                        "[custom]\n"
                                        "value=1\n");
    app::ApplicationConfig config;
    config.moonraker.transport = app::MoonrakerTransportKind::WebSocket;
    config.moonraker.host = "192.168.1.50";
    config.moonraker.port = 7130;
    config.moonraker.socket_path = "/not/saved.sock";
    std::string error;
    CHECK(app::save_application_config(path,
                                       config,
                                       {app::Setting::MoonrakerTransport,
                                        app::Setting::MoonrakerHost,
                                        app::Setting::MoonrakerPort},
                                       error));

    CHECK_EQ(read_file(path),
             std::string("# user config\n"
                         "[moonraker]\n"
                         "# printer address\n"
                         "host=192.168.1.50\n"
                         "socket_path=/run/moonraker.sock\n"
                         "transport=websocket\n"
                         "port=7130\n"
                         "\n"
                         "[custom]\n"
                         "value=1\n"));
    std::remove(path.c_str());
}

TEST_CASE("config: saving creates missing sections") {
    const auto path = (std::filesystem::temp_directory_path() / "kdz_test_save_new.conf").string();
    std::remove(path.c_str());

    app::ApplicationConfig config;
    config.dark_mode = true;
    config.moonraker.transport = app::MoonrakerTransportKind::WebSocket;
    std::string error;
    CHECK(app::save_application_config(
        path, config, {app::Setting::DarkMode, app::Setting::MoonrakerTransport}, error));
    CHECK_EQ(read_file(path),
             std::string("[application]\n"
                         "dark_mode=yes\n"
                         "\n"
                         "[moonraker]\n"
                         "transport=websocket\n"));

    app::ApplicationConfig loaded;
    CHECK(app::load_application_config(path, loaded, error));
    CHECK(loaded.dark_mode);
    CHECK(loaded.moonraker.transport == app::MoonrakerTransportKind::WebSocket);
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
