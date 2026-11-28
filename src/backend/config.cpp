#include "config.hpp"

#include <algorithm>
#include <ranges>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

#include "utils/color_json.hpp"
#include "utils/log.hpp"

using namespace std;

namespace {

using Entries = vector<pair<string, glz::raw_json>>;

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

template<typename T>
bool read(T& t, source_location location) {
	const string config_path{get_path().string()};

	if(const auto error{glz::read_file_json<glz::opts{.error_on_unknown_keys = false}>(t, config_path, string{})}) {
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
		if(const auto error{glz::read_file_json(entries, path_str, string{})}) {
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
