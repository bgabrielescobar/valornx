// ValorNX - utilidades de archivos (funcionan igual en la Switch y en PC).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vnx {

bool makeDirs(const std::string& path);
bool fileExists(const std::string& path);
bool removeFile(const std::string& path);
bool readFile(const std::string& path, std::string& out);
bool writeFile(const std::string& path, const std::string& data);
uint64_t freeSpace(const std::string& path);   // bytes libres (0 si no se sabe)
std::vector<std::string> listDir(const std::string& dir);
std::string joinPath(const std::string& a, const std::string& b);

// Milisegundos desde un punto fijo (lo implementa cada plataforma).
uint64_t nowMs();

}  // namespace vnx
