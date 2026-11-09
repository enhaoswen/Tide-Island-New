#include "config.hpp"

#include "utils/get_type_name.hpp"
#include "utils/log.hpp"

#include <string>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

using namespace std;
using namespace std::filesystem;

template <typename T>
path Config::get_path(source_location l) {
    const char* const home = getenv("HOME");
    if (home == nullptr)
        Log::fatal("HOME is not set");

    filesystem::path config_path =
        filesystem::path(home) / ".config" / "Tide Island";

    if constexpr (same_as<T, Config::TideConfig>) {
        config_path /= "tide_config.json";
    }
    else {
        Log::fatal(
            "{}:{}: Tide Island does not support this type, type={}",
            l.file_name(),
            l.line(),
            Log::type_name<T>()
            );
    }

    return config_path.string();
}

path Config::get_path(Config::ConfigType type) {
    const char* const home = getenv("HOME");
    if (home == nullptr)
        Log::fatal("HOME is not set");

    filesystem::path config_path =
        filesystem::path(home) / ".config" / "Tide Island";

    if (type == Config::ConfigType::TideConfig) {
        config_path /= "tide_config.json";
    }
    return config_path.string();
}

bool Config::exist(Config::ConfigType type) {
    return filesystem::exists(get_path(type));
}

template <typename T>
bool Config::read(T& t, source_location l) {
    string config_path = get_path<T>();

    const auto error = glz::read_file_json(
        t,
        config_path,
        std::string{}
        );

    if (error) {
        Log::fatal(
            "{}:{}: Failed to load config.\npath={},error={}",
            l.file_name(),
            l.line(),
            config_path,
            glz::format_error(error)
            );

        return false;
    }
    return true;
}

template <typename T>
bool Config::write(const T& t, source_location l) {
    const string config_path = get_path<T>();

    const auto error = glz::write_file_json(
        t,
        config_path,
        std::string{},
        glz::opts{.prettify = true}
        );

    if (error) {
        Log::fatal(
            "{}:{}: Failed to save config.\npath={}, error={}",
            l.file_name(),
            l.line(),
            config_path,
            glz::format_error(error)
            );

        return false;
    }

    return true;
}