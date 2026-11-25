#include "config.hpp"

#include "utils/color_json.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

using namespace std;
using namespace std::filesystem;

namespace {

using Entries = vector<pair<string, glz::raw_json>>;

void report(const char* what, const string& path, const string& error, source_location l) {
    Log::logger(Log::Error, "{}:{}: Failed to {} config.\n\tpath={}, error={}",
        l.file_name(), l.line(), what, path, error);
}

}

namespace Config {
    path get_path() {
        const char* const home = getenv("HOME");
        if (home == nullptr)
            Log::fatal("HOME is not set");

        return path(home) / ".config" / "Tide Island" / "config.json";
    }

    bool exists() {
        error_code ec;
        return filesystem::exists(get_path(), ec);
    }

    template <typename T>
    bool read(T& t, source_location l) {
        const string config_path = get_path().string();

        if (const auto error = glz::read_file_json<glz::opts{.error_on_unknown_keys = false}>(t, config_path, string{})) {
            report("load", config_path, glz::format_error(error), l);
            return false;
        }
        return true;
    }

    template <typename T>
    bool write(const T& t, source_location l) {
        const path   config_path = get_path();
        const string path_str    = config_path.string();

        Entries entries;
        if (exists()) {
            if (const auto error = glz::read_file_json(entries, path_str, string{})) {
                report("load", path_str, glz::format_error(error), l);
                return false;
            }
        }

        string  json;
        Entries own;
        if (const auto error = glz::write_json(t, json)) {
            report("serialize", path_str, glz::format_error(error), l);
            return false;
        }
        (void)glz::read_json(own, json);

        for (auto& [key, value] : own) {
            if (auto it = ranges::find(entries, key, &pair<string, glz::raw_json>::first); it != entries.end())
                it->second = std::move(value);
            else
                entries.emplace_back(key, std::move(value));
        }

        error_code ec;
        create_directories(config_path.parent_path(), ec);
        if (ec) {
            report("create directory for", path_str, ec.message(), l);
            return false;
        }

        if (const auto error = glz::write_file_json<glz::opts{.prettify = true}>(entries, path_str, string{})) {
            report("save", path_str, glz::format_error(error), l);
            return false;
        }
        return true;
    }

    template bool read<IslandConfig>(IslandConfig&, source_location);
    template bool write<IslandConfig>(const IslandConfig&, source_location);

} // namespace Config
