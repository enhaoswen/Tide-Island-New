#pragma once

#include "struct.hpp"

#include <variant>

// todo: update notes in struct.hpp after rewrite config backend.

// ======== Tide Island Config Format ========

// type: key = val

// Ex. float: island_width = 140

// Supported type:
// int
// float
// string
// bool
// vector <> (not include vector, array)
// array <> (same)

// if need to add more types, remember to change config.cpp add_config (tmp)


class Config {
private:

config conf; 

public:

Config();
Config(ConfigType type);

std::string to_string();
std::variant<IslandConf> to_struct();

// we assume file already exist, but maybe not readable.
void read(ConfigType type);
void write(ConfigType type);

};