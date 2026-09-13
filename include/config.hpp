#pragma once

#include "struct.hpp"

#include <unordered_map>
#include <variant>

class TConfig {

public:

    std::unordered_map<
        std::string, 
        std::variant<
            int, 
            float, 
            std::string, 
            bool, 
            std::vector<float>, 
            std::vector<int>, 
            std::vector<std::string>,
            std::array<float, 4>
    >> config;

    TConfig();
    std::string to_string();


};

namespace Config {

void init();

// void* is the placeholder for other config types. we will add them later.
std::variant<IslandConf,void*> read(ConfigType type);
std::variant<IslandConf,void*> get_config(ConfigType type);

void write(
    ConfigType type,

    std::unordered_map<
        std::string, 
        std::variant<int, float, std::string, bool>
    >);

}