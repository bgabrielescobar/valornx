// ValorNX - interfaz con el juego en ejecución (memoria, congelar, trucos).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vnx {

// Mismos valores que MemoryType / Permission de Horizon (libnx).
namespace memtype {
constexpr uint32_t Unmapped = 0x00;
constexpr uint32_t Io = 0x01;
constexpr uint32_t Normal = 0x02;
constexpr uint32_t CodeStatic = 0x03;
constexpr uint32_t CodeMutable = 0x04;
constexpr uint32_t Heap = 0x05;
constexpr uint32_t SharedMem = 0x06;
constexpr uint32_t ModuleCodeStatic = 0x08;
constexpr uint32_t ModuleCodeMutable = 0x09;
}  // namespace memtype
namespace memperm {
constexpr uint32_t R = 1, W = 2, X = 4, RW = 3;
}

struct MemRegion {
    uint64_t base = 0, size = 0;
    uint32_t type = 0, perm = 0;
    uint64_t end() const { return base + size; }
};

struct Extent {
    uint64_t base = 0, size = 0;
    bool contains(uint64_t a) const { return a >= base && a - base < size; }
};

// Zonas de memoria tal y como las entiende el sistema de trucos de Atmosphère.
enum class RegionKind : uint8_t { Main = 0, Heap = 1, Alias = 2, Aslr = 3 };
const char* regionKindName(RegionKind k);

struct ProcessInfo {
    uint64_t titleId = 0;
    uint64_t processId = 0;
    uint8_t buildId[0x20] = {};
    Extent main, heap, alias, aslr;
    std::string gameName;
    std::vector<uint32_t> icon;   // RGBA8, iconSize x iconSize (puede estar vacío)
    int iconSize = 0;

    std::string titleIdHex() const;
    std::string buildIdHex() const;   // primeros 8 bytes (como en los trucos)
    RegionKind classify(uint64_t addr, uint64_t* offset) const;
    uint64_t baseOf(RegionKind k) const;
    std::string describe(uint64_t addr) const;   // "heap+0x1234"
};

// Truco cargado ahora mismo en Atmosphère (los de la microSD y los añadidos en vivo).
struct CheatInfo {
    uint32_t id = 0;
    bool enabled = false;
    std::string name;
    std::vector<uint32_t> opcodes;
};

class IGame {
public:
    virtual ~IGame() = default;
    virtual const ProcessInfo& info() const = 0;
    virtual bool regions(std::vector<MemRegion>& out) = 0;
    virtual bool read(uint64_t addr, void* buf, size_t size) = 0;
    virtual bool write(uint64_t addr, const void* buf, size_t size) = 0;
    // Congelar: Atmosphère reescribe el valor 12 veces por segundo, aunque la app se cierre.
    virtual bool freeze(uint64_t addr, int width) = 0;
    virtual bool unfreeze(uint64_t addr) = 0;
    virtual bool frozenAddresses(std::vector<uint64_t>& out) = 0;
    // Trucos: se ejecutan 12 veces por segundo mientras el juego está abierto.
    virtual bool addCheat(const std::string& name, const std::vector<uint32_t>& opcodes, bool enabled,
                          uint32_t* outId = nullptr) = 0;
    virtual bool removeCheat(uint32_t id) = 0;
    virtual bool toggleCheat(uint32_t id) = 0;
    virtual bool listCheats(std::vector<CheatInfo>& out) = 0;
    virtual bool stillAttached() = 0;
};

enum class SearchZone : uint8_t { All = 0, Heap = 1, Main = 2 };
const char* searchZoneLabel(SearchZone z);
bool regionWanted(const MemRegion& r, SearchZone zone, const ProcessInfo& pi);

}  // namespace vnx
