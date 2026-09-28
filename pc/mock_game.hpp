// Juego simulado para probar ValorNX en PC (no se compila para la Switch).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../source/core/game.hpp"
#include "cheat_vm.hpp"

class MockGame : public vnx::IGame {
public:
    explicit MockGame(uint64_t heapSize = 64ull << 20);

    const vnx::ProcessInfo& info() const override { return m_pi; }
    bool regions(std::vector<vnx::MemRegion>& out) override;
    bool read(uint64_t addr, void* buf, size_t size) override;
    bool write(uint64_t addr, const void* buf, size_t size) override;
    bool freeze(uint64_t addr, int width) override;
    bool unfreeze(uint64_t addr) override;
    bool frozenAddresses(std::vector<uint64_t>& out) override;
    bool addCheat(const std::string& name, const std::vector<uint32_t>& opcodes, bool enabled,
                  uint32_t* outId = nullptr) override;
    bool removeCheat(uint32_t id) override;
    bool toggleCheat(uint32_t id) override;
    bool listCheats(std::vector<vnx::CheatInfo>& out) override;
    bool stillAttached() override { return true; }

    // Un "tick" de Atmosphère: ejecuta los trucos activos y aplica los valores congelados.
    void tick();
    bool lastVmOk = true;
    // Cambia valores aleatorios, como haría un juego real (temporizadores, animaciones...).
    void noise(int count);

    int32_t getS32(uint64_t addr);
    void setS32(uint64_t addr, int32_t v);
    float getF32(uint64_t addr);
    void setF32(uint64_t addr, float v);

    // Variables del "juego"
    uint64_t moneyAddr = 0, moneyCopyAddr = 0, hpAddr = 0, expAddr = 0, levelAddr = 0, itemsAddr = 0;
    uint64_t ioValueAddr = 0, badPageValueAddr = 0, goodPageValueAddr = 0;
    uint64_t farAddr = 0;      // valor muy lejos en la zona ASLR (más de 64 GB)
    uint64_t bssEnd = 0;       // fin de la .bss del ejecutable (según MOD0)
    uint64_t sdkBssEnd = 0;    // lo mismo para un segundo módulo (tipo "sdk")
    int64_t readCalls = 0;

    void spendMoney(int32_t n);
    void takeDamage(float x);
    void gainExp(int32_t n);
    void setPid(uint64_t pid) { m_pi.processId = pid; }

private:
    struct Seg {
        vnx::MemRegion r;
        std::vector<uint8_t> data;
        uint64_t badPageStart = 0, badPageEnd = 0;   // rango que falla al leer
    };
    std::vector<Seg> m_segs;
    vnx::ProcessInfo m_pi;
    std::map<uint64_t, std::pair<uint64_t, int>> m_frozen;
    std::vector<vnx::CheatInfo> m_cheats;
    uint32_t m_nextCheatId = 1;
    CheatVmEmu m_vm;
    uint64_t m_rng = 0x1234567887654321ull;

    uint32_t rnd();
    Seg* segFor(uint64_t addr, size_t size);
    void addSeg(uint64_t base, uint64_t size, uint32_t type, uint32_t perm, bool fill);
};
