// ValorNX - nombre (y el icono en JPEG) de un juego instalado, a través del servicio ns.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vnx {

// iconJpeg puede ser nullptr (el overlay solo necesita el nombre).
bool loadGameName(uint64_t titleId, std::string& name, std::vector<uint8_t>* iconJpeg);

}  // namespace vnx
