#pragma once

#include "utils/struct.hpp"
#include "utils/color.hpp"

#include <filesystem>
#include <variant>
#include <source_location>
#include <unordered_map>

// todo: update notes in struct.hpp after rewrite config backend.

// ======== Tide Island Config Format ========

// type: key = val
// Ex. float: island_width = 140

// for array & vector, we should write the config like this:
// list<type>: key = [v1,v2,v3]


// Supported type:
// int
// float
// string
// bool
// list<type> (not include list)


// if need to add more types, remember to change Config::Read && Config::Write

// you can both you "//" and "#" for note

using config_turn = std::variant<
        int, 
        float, 
        std::string, 
        bool, 
        std::vector<float>, 
        std::vector<int>, 
        std::vector<std::string>,
        std::vector<bool>,
        Color
>;

using config = std::unordered_map<std::string, config_turn>;

class Config {
private:

config conf; 
std::filesystem::path conf_path{};

public:

Config();

IslandConf to_struct();

// we assume file already exist, but maybe not readable.
void read();
void write();
void write(config& arg_config);

};