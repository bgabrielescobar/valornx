// ValorNX - acceso al juego en la Switch a través de dmnt:cht (Atmosphère).
#pragma once
#include <string>

#include "../core/game.hpp"

namespace vnx {

class DmntGame : public IGame {
public:
    // Conecta con el juego abierto. nullptr si no hay (error vacío = simplemente no hay juego).
    static DmntGame* connect(std::string& error);
    ~DmntGame() override;

    const ProcessInfo& info() const override { return m_pi; }
    // Para añadir el nombre y el icono del juego después de conectar.
    ProcessInfo& editableInfo() { return m_pi; }
    bool regions(std::vector<MemRegion>& out) override;
    bool read(uint64_t addr, void* buf, size_t size) override;
    bool write(uint64_t addr, const void* buf, size_t size) override;
    bool freeze(uint64_t addr, int width) override;
    bool unfreeze(uint64_t addr) override;
    bool frozenAddresses(std::vector<uint64_t>& out) override;
    bool addCheat(const std::string& name, const std::vector<uint32_t>& opcodes, bool enabled,
                  uint32_t* outId = nullptr) override;
    bool removeCheat(uint32_t id) override;
    bool toggleCheat(uint32_t id) override;
    bool listCheats(std::vector<CheatInfo>& out) override;
    bool stillAttached() override;

private:
    DmntGame() = default;
    ProcessInfo m_pi;
};

}  // namespace vnx
