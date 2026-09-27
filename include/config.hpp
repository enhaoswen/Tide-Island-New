#pragma once

#include "struct.hpp"

#include <variant>
#include <source_location>

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
        std::array<float, 4>
>;

using config = std::unordered_map<std::string, config_turn>;

class Config {
private:

config conf; 
ConfigType type;

public:

Config(ConfigType type, std::source_location location = std::source_location::current());

IslandConf to_struct();

// we assume file already exist, but maybe not readable.
void read();
void write();
void write(config& arg_config);

};