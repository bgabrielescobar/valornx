// ValorNX - ajustes de la app.
#pragma once
#include <string>

#include "game.hpp"

namespace vnx {

struct Config {
    SearchZone zone = SearchZone::All;
    int typeChoice = 0;   // último tipo elegido al buscar
};

bool loadConfig(const std::string& path, Config& cfg);
bool saveConfig(const std::string& path, const Config& cfg);

}  // namespace vnx
