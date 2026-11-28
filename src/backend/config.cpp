#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

#include "utils/color_json.hpp"
#include "utils/log.hpp"

using namespace std;

namespace {

using Entries = vector<pair<string, glz::raw_json> >;

constexpr string_view k_default_config{R"({
   "island_width": 140,
   "island_height": 38,
   "zone": 40,
   "anchor_top": 2,
   "radius": 19,
   "color": "#00000044",
   "blur": true,
   "fps": 0,
   "_fps": "0 means follow the system fps",
   "twenty_four_h": false
}
)"};

constexpr glz::opts k_read_opts{.error_on_unknown_keys = false};

Config::TideConfig config{};

void report(const char* what, const string& path, const string& error, source_location location) {
	Log::logger(Log::LogLevel::Error,
		"{}:{}: Failed to {} config.\n\tpath={}, error={}",
		location.file_name(),
		location.line(),
		what,
		path,
		error);
}

}

namespace Config {

filesystem::path get_path() {
	const char* const home{getenv("HOME")};
	if(home == nullptr)
		Log::fatal("HOME is not set");

	return filesystem::path(home) / ".config" / "Tide Island" / "config.json";
}

bool has_config() {
	error_code ec{};
	return filesystem::exists(get_path(), ec);
}

void init() {
	// Seed with the defaults so keys missing from the file keep a valid value.
	if(const auto error{glz::read<k_read_opts>(config, string{k_default_config})})
		Log::fatal("Failed to parse the default config: {}", glz::format_error(error));

	const auto is_blank{[] {
		ifstream     file{get_path()};
		const string text{istreambuf_iterator<char>{file}, istreambuf_iterator<char>{}};
		return text.find_first_not_of(" \t\r\n") == string::npos;
	}};

	if(has_config() && !is_blank()) {
		// Only write back when a key has to be added, so the file is not reformatted on every start.
		Entries file_entries{};
		Entries own{};
		string  json{};
		if(!read(config) || glz::read_file_json<k_read_opts>(file_entries, get_path().string(), string{})
			|| glz::write_json(config, json) || glz::read_json(own, json))
			return;

		const bool missing_key{ranges::any_of(own, [&](const auto& entry) {
			return ranges::find(file_entries, entry.first, &pair<string, glz::raw_json>::first) == file_entries.end();
		})};
		if(missing_key)
			write(config);
		return;
	}

	const filesystem::path config_path{get_path()};
	error_code             ec{};
	filesystem::create_directories(config_path.parent_path(), ec);
	if(ec) {
		report("create directory for", config_path.string(), ec.message(), source_location::current());
		return;
	}

	ofstream file{config_path};
	if(!(file << k_default_config))
		report("save", config_path.string(), "write failed", source_location::current());
}

const TideConfig& get_config() {
	return config;
}

template<typename T>
bool read(T& t, source_location location) {
	const string config_path{get_path().string()};

	if(const auto error{glz::read_file_json<k_read_opts>(t, config_path, string{})}) {
		report("load", config_path, glz::format_error(error), location);
		return false;
	}
	return true;
}

template<typename T>
bool write(const T& t, source_location location) {
	const filesystem::path config_path{get_path()};
	const string           path_str{config_path.string()};

	Entries entries{};
	if(has_config()) {
		if(const auto error{glz::read_file_json<k_read_opts>(entries, path_str, string{})}) {
			report("load", path_str, glz::format_error(error), location);
			return false;
		}
	}

	// A raw value keeps the whitespace around it, e.g. the newline before a closing brace.
	for(auto& value : entries | views::values) {
		auto&      text{value.str};
		const auto first{text.find_first_not_of(" \t\r\n")};
		text = first == string::npos ? string{} : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	}

	string  json{};
	Entries own{};
	if(const auto error{glz::write_json(t, json)}) {
		report("serialize", path_str, glz::format_error(error), location);
		return false;
	}
	static_cast<void>(glz::read_json(own, json));

	for(auto& [key, value] : own) {
		if(auto it{ranges::find(entries, key, &pair<string, glz::raw_json>::first)}; it != entries.end())
			it->second = std::move(value);
		else
			entries.emplace_back(key, std::move(value));
	}

	error_code ec{};
	filesystem::create_directories(config_path.parent_path(), ec);
	if(ec) {
		report("create directory for", path_str, ec.message(), location);
		return false;
	}

	if(const auto error{glz::write_file_json<glz::opts{.prettify = true}>(entries, path_str, string{})}) {
		report("save", path_str, glz::format_error(error), location);
		return false;
	}
	return true;
}

template bool read<TideConfig>(TideConfig&, source_location);

template bool write<TideConfig>(const TideConfig&, source_location);

}
