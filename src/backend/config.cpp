#include "config.hpp"
#include "utils/struct.hpp"

#include "utils/color_json.hpp"
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

    if constexpr (same_as<T, IslandConfig>) {
        config_path /= "island_config.json";
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

    if (type == Config::ConfigType::IslandConfig) {
        config_path /= "island_config.json";
    }
    return config_path.string();
}

bool Config::exist(Config::ConfigType type) {
    return filesystem::exists(get_path(type));
}

template <typename T>
bool Config::read(T& t, source_location l) {
    string config_path = get_path<T>(l);

    const auto error = glz::read_file_json(
        t,
        config_path,
        std::string{}
        );

    if (error) {
        Log::logger(
            Log::Error,
            "{}:{}: Failed to load config.\n\tpath={},error={}",
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
    const string config_path = get_path<T>(l);

    const auto error = glz::write_file_json<glz::opts{.prettify = true}>(
        t,
        config_path,
        std::string{}
        );

    if (error) {
        Log::logger(
            Log::Error,
            "{}:{}: Failed to save config.\n\tpath={}, error={}",
            l.file_name(),
            l.line(),
            config_path,
            glz::format_error(error)
            );

        return false;
    }

    return true;
}

template std::filesystem::path Config::get_path<Config::IslandConfig>(std::source_location);
template bool Config::read<Config::IslandConfig>(Config::IslandConfig&, std::source_location);
template bool Config::write<Config::IslandConfig>(const Config::IslandConfig&, std::source_location);