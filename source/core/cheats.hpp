// ValorNX - exportar valores como trucos de Atmosphère
// (sdmc:/atmosphere/contents/<TitleID>/cheats/<BuildID>.txt).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "game.hpp"
#include "value.hpp"

namespace vnx {

// Código tipo 0 de Atmosphère: "escribir valor fijo en memoria".
std::vector<uint32_t> cheatStoreStatic(RegionKind region, uint64_t offset, VType type, uint64_t raw);
std::string cheatSafeName(const std::string& name);
std::string cheatBlock(const std::string& name, const std::vector<uint32_t>& ops);
std::string cheatFilePath(const std::string& sdRoot, const ProcessInfo& pi);
// Sustituye el truco con ese nombre o lo añade al final, sin tocar el resto.
std::string mergeCheatFile(const std::string& existing, const std::string& name, const std::string& block);
bool writeCheat(const std::string& sdRoot, const ProcessInfo& pi, const std::string& name,
                const std::vector<uint32_t>& ops, std::string& pathOut);
// Quita el truco con ese nombre (si existe), sin tocar el resto.
std::string removeCheatBlock(const std::string& existing, const std::string& name);
// ¿Está ese truco guardado en el archivo de trucos del juego?
bool cheatFileHas(const std::string& sdRoot, const ProcessInfo& pi, const std::string& name);
bool removeCheatFromFile(const std::string& sdRoot, const ProcessInfo& pi, const std::string& name);

}  // namespace vnx
