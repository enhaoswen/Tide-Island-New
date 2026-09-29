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
#include <ranges>
#include <tuple>

using namespace std;
using namespace std::filesystem;

namespace {

config default_config{
    {"island_width", 140},
    {"island_height", 38},
    {"zone", 40},
    {"anchor_top", 2.0f},
    {"radius", 19.0f},
    {"color", vector<float>{0.0f, 0.0f, 0.0f, 1.0f}}
};

template <typename>
struct is_std_array : false_type {};
template <typename T, size_t N>
struct is_std_array<array<T, N>> : std::true_type {};
template <typename T>
inline constexpr bool is_std_array_v = is_std_array<decay_t<T>>::value;

template <typename T>
bool convert(const string& key, const config_turn& val, T& target) {
    if (const auto* p = get_if<T>(&val)) {
        target = *p;
        return true;
    }

    using target_t = decay_t<T>;
    if constexpr (is_std_array_v<target_t>) {
        using elem_t = typename target_t::value_type;
        constexpr auto N = std::tuple_size_v<target_t>;
        if (const auto* p = get_if<vector<elem_t>>(&val)) {
            if (p->size() != N) {
                Log::logger(Log::Error, R"@(Expected {} elements, got {}, key="{}")@", N, p->size(), key);
                return false;
            }
            ranges::copy(*p, target.begin());
            return true;
        }
    }

    return false;
}

// we assume that conf is already initialized. so when error occurred, we don't give it a default value, but just leave it.
template <typename T>
void set_config(const string& key, const config& conf, T& target) {
    auto it = conf.find(key);

    if (it == conf.end()) {
        Log::logger(Log::Error, R"@(Key not found in config, key="{}")@", key);
        if (auto dit = default_config.find(key); dit != default_config.end()){
            convert(key, dit->second, target);
        }
        return;
    }

    if (!convert(key, it->second, target)) {
        Log::logger(Log::Error, R"@(Wrong config type, key="{}")@", key);
        if (auto dit = default_config.find(key); dit != default_config.end()) {
            convert(key, dit->second, target);
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
        Log::logger(Log::Error, R"@(Wrong config type, key="{}")@", key);
    }
}

path get_config_path() {
    const char* home = getenv("HOME");

    if (home == nullptr) {
        Log::fatal("HOME is not set");
    }

    return path(home) / ".config" / "Tide Island" / "config.tide";
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
            Log::logger(Log::Error, R"@(Invalid integer, key="{}", value="{}")@", token[1], token[2]);
            return;
        }
        catch (const out_of_range&) {
            Log::logger(Log::Error, R"@(Integer out of range, key="{}", value="{}")@", token[1], token[2]);
            return;
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
                R"@(Invalid float, key="{}", value="{}")@",
                token[1],
                token[2]
            );
            return;
        }
        catch (const out_of_range&) {
            Log::logger(
                Log::Error,
                R"@(Float out of range, key="{}", value="{}")@",
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
                R"@(Invalid boolean, key="{}", value="{}")@",
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
            Log::logger(Log::Error, R"@(Malformed list type, key="{}", type="{}")@", token[1], token[0]);
            return;
        }
        string elem_type = token[0].substr(info_begin + 1, info_end - info_begin - 1);

        string values_str = token[2];
        size_t lb = values_str.find('[');
        size_t rb = values_str.rfind(']');
        if (lb == string::npos || rb == string::npos || rb < lb) {
            Log::logger(Log::Error, R"@(Malformed list value (missing brackets), key="{}", value="{}")@", token[1], token[2]);
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
                    Log::logger(Log::Error, R"@(Invalid integer element, key="{}", value="{}")@", token[1], rv);
                    return;
                }
                catch (const out_of_range&) {
                    Log::logger(Log::Error, R"@(Integer element out of range, key="{}", value="{}")@", token[1], rv);
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
                    Log::logger(Log::Error, R"@(Invalid float element, key="{}", value="{}")@", token[1], rv);
                    return;
                }
                catch (const out_of_range&) {
                    Log::logger(Log::Error, R"@(Float element out of range, key="{}", value="{}")@", token[1], rv);
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
                    Log::logger(Log::Error, R"@(Invalid boolean element, key="{}", value="{}")@", token[1], rv);
                    return;
                }
            }
            set_config(token[1], values, target);
        }

        else if (elem_type == "string") {
            set_config(token[1], raw_values, target);
        }

        else {
            Log::logger(Log::Error, R"@(Unsupported element type, type="{}", element_type="{}")@", token[0], elem_type);
            return;
        }
    }

    else {
        Log::logger(
            Log::Error,
            R"@(Invalid type, type="{}", key="{}", value="{}")@",
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
        return v;
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

Config::Config(source_location location) {
    error_code ec;
    path parent_path = path(getenv("HOME")) / ".config" / "Tide Island";

    bool is_exist = exists(parent_path,ec);

    if (ec) {
        Log::logger(
            Log::Error,
            R"@(Failed to access config directory, path="{}": {} ({}))@",
            parent_path.string(), ec.message(), ec.value());
        Log::logger(Log::Error, "So use the default config file instead.");

        conf = default_config;

        return;
    }

    if (!is_exist) {
        create_directories(parent_path, ec);

        if (ec) {
            Log::logger(
                Log::Error,
                R"@(Failed to access config directory, path="{}": {} ({}))@",
                parent_path.string(), ec.message(), ec.value());
            Log::logger(Log::Error, "So use the default config file instead.");

            conf = default_config;
            
            return;
        }

        Log::logger(Log::Debug,R"@(Create directory successfully, path="{}")@", parent_path.string());
    }

    is_exist = exists(conf_path, ec);

    if (ec) {
        Log::logger(
            Log::Error,
            R"@(Failed to access config file, path="{}": {} ({}))@",
            conf_path.string(), ec.message(), ec.value());

        Log::logger(Log::Error, "So use the default config file instead.");

        conf = default_config;
        return;
    }

    if (!is_exist) {

        ofstream file(conf_path);

        if (!file) {
            Log::logger(
                Log::Error,
                R"@(Failed to create file, path="{}": {})@",
                conf_path.string(),
                strerror(errno)
            );
            conf = default_config;
            return;
        }
    } 

    if (file_size(conf_path) == 0) {
        conf = default_config;
        write(conf);
    }
    
    else {
        conf = default_config;
        read();
    }

}

void Config::read() {
    error_code ec;

    ifstream file(conf_path);

    if (!file) {
        Log::logger(
            Log::Error,
            R"@(Failed to open file, path="{}": {})@",
            conf_path.string(),
            strerror(errno)
        );
        conf = default_config;
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
                "{}:{}: Invalid config",
                get_config_path().string(),
                count
            );
            continue;
        }
        
        if (!conf.contains(tokens[1])) {
            Log::logger(
                Log::Error,
                R"@({}:{}: Invalid config key, key="{}")@",
                get_config_path().string(),
                count,
                tokens[1]
            );
            continue;
        }
        assign_config(tokens, conf.at(tokens[1]));
    }
}

void Config::write() {
    error_code ec;

    ofstream file(conf_path);

    if (!file) {
        Log::logger(
            Log::Error,
            R"@(Failed to open file, path="{}": {})@",
            conf_path.string(),
            strerror(errno)
        );
        Log::logger(Log::Error, "So stop trying");
        return;
    }

    for (const auto& [key, value] : conf) {
        visit([&](const auto& val) {
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

    ofstream file(conf_path);

    if (!file) {
        Log::logger(
            Log::Error,
            R"@(Failed to open file, path="{}": {})@",
            conf_path.string(),
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

IslandConf Config::to_struct(){

    IslandConf island_conf{};

    set_config("color", conf, island_conf.color);
    set_config("island_width", conf, island_conf.island_width);
    set_config("island_height", conf, island_conf.island_height);
    set_config("zone", conf, island_conf.zone);
    set_config("anchor_top", conf, island_conf.anchor_top);
    set_config("radius", conf, island_conf.radius);

    return island_conf;


}
