#include "log.hpp"
#include "struct.hpp"
#include "config.hpp"

#include <cerrno>
#include <source_location>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <system_error>
#include <format>
#include <cstring>
#include <string_view>
#include <variant>

using namespace std;
using namespace std::filesystem;

namespace {

// Remember to change funtion `get_default_config` if you add new config files.
config default_config{
    {"island_width", 140},
    {"island_height", 38},
    {"zone", 40},
    {"anchor_top", 2.0f},
    {"radius", 19.0f},
    {"color", vector<float>{0.0f, 0.0f, 0.0f, 1.0f}}
};

// we assume that conf is already initialized. so when error occured, we don't give it a default value, but just leave it/.
template <typename T>
void set_config(
    string key,
    const config& conf,
    T& target
) {
    auto it = conf.find(key);

    if (it == conf.end()) {
        Log::logger(Log::Error, R"(key "{}" is not found in your config.)", key);
    }
    else {

        if (holds_alternative<T>(it->second)) {
            target = get<T>(it->second);
        }
        else {
            Log::logger(Log::Error, R"(key "{}" has the wrong type.)", key);
        }
    }
}

template<typename T>
void set_config(
    string key, 
    T& val, 
    config_turn& target
) {

    if (holds_alternative<T>(target)) {
        target = val;
    }
    else {
        Log::logger(Log::Error, R"(key "{}" has the wrong type.)", key);
    }
}

path get_config_path(ConfigType type, source_location location = source_location::current()) {
    const char* home = getenv("HOME");

    if (home == nullptr) {
        Log::fatal("HOME is not set");
    }

    switch (type) {
    case ConfigType::IslandConfig:
        return path(home) / ".config" / "Tide Island" / "config.tide";

    case ConfigType::Count:
        Log::logger(Log::Error, R"("ConfigType::Count" should not be used. "{}": {})", location.file_name(), location.line());
    }
}

config& get_default_config(ConfigType type, source_location location = source_location::current()) {
    switch (type) {
    case ConfigType::IslandConfig:
        return default_config;

    case ConfigType::Count:
        Log::fatal(R"(ConfigType::Count is used to get the count of config types. It should not be used. "{}": {})", location.file_name(), location.line());

    }
}

array<string, 3> split(string& line) {
    array<string, 3> result;

    erase(line, ' ');

    auto colon = line.find(':');
    auto equal = line.find('=', colon + 1);

    if (colon == string_view::npos ||
        equal == string_view::npos) {
        return {};
    }

    result[0] = line.substr(0, colon);
    result[1] = line.substr(colon + 1, equal - colon - 1);
    result[2] = line.substr(equal + 1);

    return result;
}

vector<string> split_and_trim(const string& s, char delim) {
    vector<string> parts;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(delim, start);
        if (pos == string::npos){
            pos = s.size();
        }
        string piece = s.substr(start, pos - start);
        size_t a = piece.find_first_not_of(" \t");
        size_t b = piece.find_last_not_of(" \t");
        piece = (a == string::npos) ? "" : piece.substr(a, b - a + 1);
        parts.push_back(piece);
        start = pos + 1;
    }
    return parts;
}

void assign_config(array<string,3> token, config_turn& target) {

    if (token[0] == "int") {
        int val;

        try {
            val = stoi(token[2]);
        } 
        catch (invalid_argument&) {
            Log::logger(Log::Error, "Invalid integer: {}:{}", token[1], token[2]);
        }
        catch (const out_of_range&) {
            Log::logger(Log::Error, "Integer out of range: {}:{}", token[1], token[2]);
        }
        set_config(token[1], val, target);
    }

    else if (token[0] == "float") {
        float val;

        try {
            val = stof(token[2]);
        }
        catch (const invalid_argument&) {
            Log::logger(
                Log::Error,
                "Invalid float: {}:{}",
                token[1],
                token[2]
            );
            return;
        }
        catch (const out_of_range&) {
            Log::logger(
                Log::Error,
                "Float out of range: {}:{}",
                token[1],
                token[2]
            );
            return;
        }

        set_config(token[1], val, target);
    }

    else if (token[0] == "string") {
        set_config(token[1], token[2], target);
    }

    else if (token[0] == "bool") {

        bool val;

        if (token[2] == "true") {
            val = true;
        }
        else if (token[2] == "false") {
            val = false;
        }
        else {
            Log::logger(
                Log::Error,
                "Invalid boolean: {}:{}",
                token[1],
                token[2]
            );
            return;
        }

        set_config(token[1], val, target);
    }

    else if (token[0].starts_with("list")) {

        token[0].erase(std::remove(token[0].begin(), token[0].end(), ' '), token[0].end());

        size_t info_begin = token[0].find('<');
        size_t info_end   = token[0].find('>');
        if (info_begin == string::npos || info_end == string::npos || info_end < info_begin) {
            Log::logger(Log::Error, "Malformed list type: {}:{}", token[1], token[0]);
            return;
        }
        string elem_type = token[0].substr(info_begin + 1, info_end - info_begin - 1);

        string values_str = token[2];
        size_t lb = values_str.find('[');
        size_t rb = values_str.rfind(']');
        if (lb == string::npos || rb == string::npos || rb < lb) {
            Log::logger(Log::Error, "Malformed list value (missing brackets): {}:{}", token[1], token[2]);
            return;
        }
        values_str = values_str.substr(lb + 1, rb - lb - 1);

        vector<string> raw_values = split_and_trim(values_str, ',');

        if (raw_values.size() == 1 && raw_values[0].empty()) {
            raw_values.clear();
        }

        if (elem_type == "int") {
            vector<int> values;
            values.reserve(raw_values.size());
            for (const string& rv : raw_values) {
                int v;
                try {
                    v = stoi(rv);
                }
                catch (const invalid_argument&) {
                    Log::logger(Log::Error, "Invalid integer element: {}:{}", token[1], rv);
                    return;
                }
                catch (const out_of_range&) {
                    Log::logger(Log::Error, "Integer element out of range: {}:{}", token[1], rv);
                    return;
                }
                values.push_back(v);
            }
            set_config(token[1], values, target);
        }

        else if (elem_type == "float") {
            vector<float> values;
            values.reserve(raw_values.size());
            for (const string& rv : raw_values) {
                float v;
                try {
                    v = stof(rv);
                }
                catch (const invalid_argument&) {
                    Log::logger(Log::Error, "Invalid float element: {}:{}", token[1], rv);
                    return;
                }
                catch (const out_of_range&) {
                    Log::logger(Log::Error, "Float element out of range: {}:{}", token[1], rv);
                    return;
                }
                values.push_back(v);
            }
            set_config(token[1], values, target);
        }
        else if (elem_type == "bool") {
            vector<bool> values;
            values.reserve(raw_values.size());
            for (const string& rv : raw_values) {
                if (rv == "true")       values.push_back(true);
                else if (rv == "false") values.push_back(false);
                else {
                    Log::logger(Log::Error, "Invalid boolean element: {}:{}", token[1], rv);
                    return;
                }
            }
            set_config(token[1], values, target);
        }

        else if (elem_type == "string") {
            set_config(token[1], raw_values, target);
        }

        else {
            Log::logger(Log::Error, "Unsupported element type in {}: {}", token[0], elem_type);
            return;
        }
    }

    else {
        Log::logger(
            Log::Error,
            "Invalid type {}: {} = {}",
            token[0],
            token[1],
            token[2]
        );
    }
}

template <typename T>
string format_element(const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    }
    else if constexpr (std::is_same_v<T, string>) {
        return v; // string 元素不带引号
    }
    else {
        ostringstream oss;
        oss << v;
        return oss.str();
    }
}

template <typename T>
string format_list(const vector<T>& values) {
    string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        out += format_element<T>(values[i]);
        if (i + 1 < values.size()) out += ",";
    }
    out += "]";
    return out;
}

} // namespace

Config::Config(ConfigType arg_type, source_location location) {
    error_code ec;
    path parent_path = path(getenv("HOME")) / ".config" / "Tide Island";

    if (arg_type == ConfigType::Count) {
        Log::fatal(R"(Config type should not be Count. "{}": {})", location.file_name(),location.line());
    }

    type = arg_type;

    bool is_exist = exists(parent_path,ec);

    if (ec) {
        Log::logger(
            Log::Error,
            "Failed to access config directory {}: {} ({})",
            parent_path.string(), ec.message(), ec.value());
        Log::logger(Log::Error, "So use the default config file instead.");

        conf = get_default_config(arg_type);

        return;
    }

    if (!is_exist) {
        create_directories(parent_path, ec);

        if (ec) {
            Log::logger(
                Log::Error,
                "Failed to access config directory {}: {} ({})",
                parent_path.string(), ec.message(), ec.value());
            Log::logger(Log::Error, "So use the default config file instead.");

            conf = get_default_config(arg_type);
            
            return;
        }

        Log::logger(Log::Debug,"Create directory {} successfully", parent_path.string());
    }

    path file_path = get_config_path(arg_type);
    is_exist = exists(file_path, ec);

    if (ec) {
        Log::logger(
            Log::Error,
            "Failed to access config file {}: {} ({})",
            file_path.string(), ec.message(), ec.value());

        Log::logger(Log::Error, "So use the default config file instead.");

        conf = get_default_config(arg_type);
        return;
    }

    if (!is_exist) {

        ofstream file(file_path);

        if (!file) {
            Log::logger(
                Log::Debug,
                "Failed to create file {}: {}", 
                file_path.string(),
                strerror(errno)
            );
            conf = get_default_config(type);
        }
    } 

    if (file_size(get_config_path(arg_type)) == 0) {
        conf = get_default_config(type);
        write(conf);
    }
    
    else {
        conf = get_default_config(arg_type);
        read();
    }

}

void Config::read() {
    error_code ec;
    path file_path = get_config_path(type);

    ifstream file(file_path);

    if (!file) {
        Log::logger(
            Log::Debug,
            "Failed to open file {}: {}", 
            file_path.string(),
            strerror(errno)
        );
        conf = get_default_config(type);
        return;
    }

    int count{};
    string line{};

    while (getline(file, line)) {
        ++count;

        if (
            line.empty() ||
            line.starts_with("//") ||
            line.starts_with("#")
        ) {
            continue;
        }

        array<string, 3> tokens = split(line);
        if (tokens[0].empty()) {
            Log::logger(
                Log::Error, 
                R"(Invalid config on "{}":{} )",
                get_config_path(type).string(),
                count
            );
        }
        
        if (!conf.contains(tokens[1])) {
            Log::logger(
                Log::Error,
                R"(Invalid config "{}" on "{}": {})",
                tokens[1],
                get_config_path(type).string(),
                count
            );
        }
        assign_config(tokens, conf.at(tokens[1]));
    }
}

void Config::write() {
    error_code ec;
    path file_path = get_config_path(type);

    ofstream file(file_path);

    if (!file) {
        Log::logger(
            Log::Debug,
            "Failed to open file {}: {}", 
            file_path.string(),
            strerror(errno)
        );
        Log::logger(Log::Error, "So stop trying");
        return;
    }

    for (const auto& [key, value] : conf) {
        std::visit([&](const auto& val) {
            using T = decay_t<decltype(val)>;

            if constexpr (is_same_v<T, int>) {
                file << "int: " << key << " = " << val << "\n";
            }
            else if constexpr (is_same_v<T, float>) {
                file << "float: " << key << " = " << val << "\n";
            }
            else if constexpr (is_same_v<T, bool>) {
                file << "bool: " << key << " = " << (val ? "true" : "false") << "\n";
            }
            else if constexpr (is_same_v<T, string>) {
                file << "string: " << key << " = " << val << "\n";
            }
            else if constexpr (is_same_v<T, vector<int>>) {
                file << "list<int>: " << key << " = " << format_list(val) << "\n";
            }
            else if constexpr (is_same_v<T, vector<float>>) {
                file << "list<float>: " << key << " = " << format_list(val) << "\n";
            }
            else if constexpr (is_same_v<T, vector<bool>>) {
                file << "list<bool>: " << key << " = " << format_list(val) << "\n";
            }
            else if constexpr (is_same_v<T, vector<string>>) {
                file << "list<string>: " << key << " = " << format_list(val) << "\n";
            }
        }, value);
    }
}

void Config::write(config& arg_config) {
    error_code ec;
    path file_path = get_config_path(type);

    ofstream file(file_path);

    if (!file) {
        Log::logger(
            Log::Debug,
            "Failed to open file {}: {}", 
            file_path.string(),
            strerror(errno)
        );
        Log::logger(Log::Error, "So stop trying");
        return;
    }

    for (const auto& [key, value] : arg_config) {
        std::visit([&](const auto& val) {
            using T = decay_t<decltype(val)>;

            if constexpr (is_same_v<T, int>) {
                file << "int: " << key << " = " << val << "\n";
            }
            else if constexpr (is_same_v<T, float>) {
                file << "float: " << key << " = " << val << "\n";
            }
            else if constexpr (is_same_v<T, bool>) {
                file << "bool: " << key << " = " << (val ? "true" : "false") << "\n";
            }
            else if constexpr (is_same_v<T, string>) {
                file << "string: " << key << " = " << val << "\n";
            }
            else if constexpr (is_same_v<T, vector<int>>) {
                file << "list<int>: " << key << " = " << format_list(val) << "\n";
            }
            else if constexpr (is_same_v<T, vector<float>>) {
                file << "list<float>: " << key << " = " << format_list(val) << "\n";
            }
            else if constexpr (is_same_v<T, vector<bool>>) {
                file << "list<bool>: " << key << " = " << format_list(val) << "\n";
            }
            else if constexpr (is_same_v<T, vector<string>>) {
                file << "list<string>: " << key << " = " << format_list(val) << "\n";
            }
        }, value);
    }
}


variant<IslandConf> Config::to_struct(){
    if (type == ConfigType::IslandConfig) {

        IslandConf island_conf{};

        set_config("color", conf, island_conf.color);
        set_config("island_width", conf, island_conf.island_width);
        set_config("island_height", conf, island_conf.island_height);
        set_config("zone", conf, island_conf.zone);
        set_config("anchor_top", conf, island_conf.anchor_top);
        set_config("radius", conf, island_conf.radius);
        set_config("is_running", conf, island_conf.is_running);

        return island_conf;
    }

    return {};

}