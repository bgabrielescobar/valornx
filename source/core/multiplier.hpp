// ValorNX - multiplicador de ganancias.
//
// Genera un truco para la máquina virtual de trucos de Atmosphère (dmnt), que se
// ejecuta 12 veces por segundo mientras juegas, aunque ValorNX esté cerrado:
//
//     actual = [valor]
//     si (anterior != 0 y actual > anterior)
//         actual = actual + (actual - anterior) * (factor - 1)   (con tope)
//         [valor] = actual
//     anterior = actual
//
// "anterior" se guarda en 8 bytes libres al final de la .bss del ejecutable del juego,
// porque los registros de la máquina virtual se borran en cada ejecución.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "game.hpp"
#include "value.hpp"

namespace vnx {

struct MultiplierSpec {
    RegionKind region = RegionKind::Heap;
    uint64_t offset = 0;       // dirección del valor, relativa a su zona
    VType type = VType::S32;
    uint32_t factor = 2;
    RegionKind slotRegion = RegionKind::Main;   // dónde se guarda el valor anterior (main o aslr)
    uint64_t slotOffset = 0;
};

constexpr uint32_t kMinFactor = 2, kMaxFactor = 1000;

// Solo enteros con los bytes en orden normal: la máquina virtual no sabe sumar decimales en
// versiones viejas, ni dar la vuelta a los bytes de los tipos BE.
bool multiplierSupported(VType t);
// Por qué no se puede multiplicar un valor de tipo t (si !multiplierSupported(t)).
void multiplierUnsupported(VType t, const std::string& valueName, std::string& title, std::string& body);
uint64_t multiplierCap(VType t);
std::vector<uint32_t> buildMultiplierOps(const MultiplierSpec& s);
// Reconoce un truco generado por buildMultiplierOps (aunque lo cargara Atmosphère desde la microSD).
bool parseMultiplierOps(const std::vector<uint32_t>& ops, MultiplierSpec& out);
std::string multiplierCheatName(const std::string& valueName, uint32_t factor);

// Hueco libre tras la .bss de un módulo del juego (según su cabecera MOD0).
// Primero se busca en el ejecutable principal; si no hay sitio, en los demás módulos.
struct ScratchArea {
    RegionKind region = RegionKind::Main;   // main, o aslr si está en otro módulo
    uint64_t slotBase = 0;                  // relativo a esa zona
    uint32_t slots = 0;                     // huecos de 8 bytes disponibles
};
bool findScratch(IGame& g, ScratchArea& out, std::string& error);

}  // namespace vnx
