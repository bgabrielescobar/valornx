// Emulador de la máquina virtual de trucos de Atmosphère (solo para pruebas en PC).
// Portado de stratosphere/dmnt/source/cheat/impl/dmnt_cheat_vm.cpp para los tipos
// 0x0, 0x1, 0x2, 0x4, 0x5, 0x9, 0xA y 0xC0, respetando sus detalles
// (por ejemplo: cargar de memoria NO limpia el registro).
#pragma once
#include <cstdint>
#include <vector>

#include "../source/core/game.hpp"

class CheatVmEmu {
public:
    // Ejecuta el programa una vez (lo que hace dmnt 12 veces por segundo).
    // Devuelve false si encontró un código que no sabe decodificar.
    bool execute(const std::vector<uint32_t>& program, vnx::IGame& mem);
    uint64_t regs[16] = {};

private:
    struct Op {
        uint32_t type = 0;
        bool beginCond = false;
        bool isElse = false, hasImm = false, incr = false;
        uint32_t width = 0, memType = 0, reg = 0, reg2 = 0, reg3 = 0;
        uint32_t cond = 0, compType = 0, ofsType = 0, math = 0, loadMode = 0;
        uint64_t rel = 0, value = 0;
    };
    const std::vector<uint32_t>* m_prog = nullptr;
    size_t m_ip = 0;
    size_t m_depth = 0;
    bool m_stoppedOnUnknown = false;

    bool decode(Op& op);
    void skipBlock(bool isIf);
    static uint64_t address(const vnx::ProcessInfo& pi, uint32_t memType, uint64_t rel);
};
