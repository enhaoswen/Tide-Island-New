#include "log.hpp"
#include "config.hpp"

#include <source_location>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <system_error>
#include <format>
#include <string_view>

using namespace std;
using namespace std::filesystem;

// Remember to change cuntion `get_default_config` if you add new config files.

config default_config {
    {"island_width", 140},
    {"island_height", 38},
    {"zone", 40},
    {"anchor_top", 2.0f},
    {"radius", 0.0f},
    {"color", vector<float>{0.0f, 0.0f, 0.0f, 1.0f}}
};

namespace {

// i got no choice man...
IslandConf conf_to_island_conf(const config& conf) {
    if (conf.contains("island_width")) {
        
    }
}

path get_config_path(ConfigType type){
    const char* home = getenv("HOME");

    if (home == nullptr) {
        Log::fatal("HOME is not set");
    }
    
    switch (type) {
        case ConfigType::IslandConfig:
            return path(home) / ".config" / "Tide Island" / "config.json";

        default:
            Log::fatal("Unknown config type");
    }
}

config& get_default_config(char type, source_location location = source_location::current()) {
    switch (type) {
        case static_cast<char>(ConfigType::IslandConfig):
            return default_config;

        case static_cast<char>(ConfigType::Count):
            Log::fatal("ConfigType::Count is used to get the count of config types. It should not be used. \"{}\": {}", location.file_name(), location.line());

        default:
            Log::fatal("Unknown config type {} from \"{}\": {}", type, location.file_name(), location.line());
    }
}

string create_str_config(const config& conf) {
    array<string, 7> types = {"int", "float", "string", "bool", "vector<float>", "vector<int>", "vector<string>"};
    string result;

    for (const auto& [key, value] : conf) {

    if (const auto* p = get_if<int>(&value)) {
        result += format("{}: {} = {}\n", types[value.index()], key, *p);

    } else {

        // you just have to know that this load the default config if the value is incorrect (if the key exist).
        // for example, "island_width" is a float, but if the config file has it as a string, it will load the default value of 140.

        if (default_config.contains(key)) {
            result += format(
                "{}: {} = {}\n",
                types[default_config[key].index()],
                key,
                visit([](const auto& value) {
                    return format("{}", value);
                }, default_config[key])
            );

            Log::logger(Log::Error, "Config key {} has an invalid value type. Using default value instead.", key);

        } else {
            Log::logger(Log::Error, "Unknown config key: {}", key);
        }
    }
    }
    return result;
}

void create_config_file(const path& parent_path) {
    error_code parent_ec;
    if (!exists(parent_path, parent_ec)) {
        create_directories(parent_path, parent_ec);
    }

    if (parent_ec) {
        Log::logger(
            Log::Error,
            "Failed to create config directory {}: {} ({})", 
            parent_path.string(), parent_ec.message(), parent_ec.value());
        Log::logger(Log::Error, "So use the default config file instead.");
        return;
    }

    for (char i = 0; i < static_cast<char>(ConfigType::Count); ++i) {
        auto type = static_cast<ConfigType>(i);
        path cfg_path = get_config_path(type);
        error_code ec;

        bool is_exist = exists(cfg_path, ec);

        if (ec) {
            Log::logger(
                Log::Error,
                "Failed to access config file {}: {} ({})", 
                cfg_path.string(), ec.message(), ec.value());
            
            Log::logger(Log::Error, "So use the default config file instead.");
            continue;

            // here we just skip the current config. so we need to add default config somewhere else.
        }

        if (is_exist) {
            auto sz = file_size(cfg_path, ec);

            if (ec) {
                Log::logger(
                    Log::Error,
                    "Failed to load config file {}: {} ({})", 
                    cfg_path.string(), ec.message(), ec.value());
                
                Log::logger(Log::Error, "So use the default config file instead.");
                continue;
            }

            if (sz == 0) {
                // add info
                ofstream config_file(cfg_path);
                if (config_file.is_open()) {
                    config_file << create_str_config(get_default_config(i));
                } else {
                    Log::logger(Log::Error, "Failed to open and write to {}", cfg_path.string());
                    Log::logger(Log::Error, "So use the default config file instead.");
                }
            }
        }
    }
}
} // namespace

void init() {
    create_config_file(get_config_path(ConfigType::IslandConfig));
}

variant<IslandConf, void*> Config::read(ConfigType type) {
    error_code ec;
    bool is_exist = exists(get_config_path(type), ec);
    
    if (ec) {
        Log::logger(
            Log::Error,
            "Failed to load config file {}: {} ({})", 
            get_config_path(type).string(), ec.message(), ec.value());
        
        Log::logger(Log::Error, "So use the default config file instead.");

        config& conf = get_default_config(static_cast<char>(type));



    }
}