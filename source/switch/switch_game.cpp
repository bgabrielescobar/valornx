#include "switch_game.hpp"

#include <switch.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dmntcht.h"

namespace vnx {

namespace {

std::string resultHex(Result rc) {
    char b[32];
    snprintf(b, sizeof(b), "0x%X (%04d-%04d)", rc, 2000 + R_MODULE(rc), R_DESCRIPTION(rc));
    return b;
}

}  // namespace

DmntGame* DmntGame::connect(std::string& error) {
    error.clear();
    const AppletType at = appletGetAppletType();
    if (at == AppletType_Application || at == AppletType_SystemApplication) {
        error =
            "ValorNX se abrió en lugar de un juego (modo aplicación, por ejemplo manteniendo R al abrir un juego). "
            "Así no puede leer ningún juego. Ciérralo, abre tu juego normalmente, pulsa HOME y abre ValorNX desde "
            "el Álbum.";
        return nullptr;
    }
    Result rc = dmntchtInitialize();
    if (R_FAILED(rc)) {
        error = "No se pudo conectar con el servicio de trucos de Atmosphère (dmnt:cht). Comprueba que usas "
                "Atmosphère actualizado y cierra EdiZon/Breeze si los tienes abiertos. Código " +
                resultHex(rc) + ".";
        return nullptr;
    }
    bool has = false;
    dmntchtHasCheatProcess(&has);
    if (!has) {
        dmntchtForceOpenCheatProcess();
        dmntchtHasCheatProcess(&has);
    }
    if (!has) {
        dmntchtExit();
        return nullptr;
    }
    DmntCheatProcessMetadata meta;
    memset(&meta, 0, sizeof(meta));
    rc = dmntchtGetCheatProcessMetadata(&meta);
    if (R_FAILED(rc)) {
        dmntchtExit();
        error = "No se pudo leer la información del juego. Código " + resultHex(rc) + ".";
        return nullptr;
    }
    DmntGame* g = new DmntGame();
    ProcessInfo& pi = g->m_pi;
    pi.titleId = meta.title_id;
    pi.processId = meta.process_id;
    memcpy(pi.buildId, meta.main_nso_build_id, sizeof(pi.buildId));
    pi.main = {meta.main_nso_extents.base, meta.main_nso_extents.size};
    pi.heap = {meta.heap_extents.base, meta.heap_extents.size};
    pi.alias = {meta.alias_extents.base, meta.alias_extents.size};
    pi.aslr = {meta.address_space_extents.base, meta.address_space_extents.size};
    return g;
}

DmntGame::~DmntGame() { dmntchtExit(); }

bool DmntGame::regions(std::vector<MemRegion>& out) {
    out.clear();
    u64 addr = 0;
    for (int guard = 0; guard < 200000; guard++) {
        MemoryInfo mi;
        memset(&mi, 0, sizeof(mi));
        if (R_FAILED(dmntchtQueryCheatProcessMemory(&mi, addr)) || mi.size == 0) break;
        MemRegion r;
        r.base = mi.addr;
        r.size = mi.size;
        r.type = mi.type & 0xFF;
        r.perm = mi.perm;
        if (r.type != memtype::Unmapped) out.push_back(r);
        const u64 next = mi.addr + mi.size;
        if (next <= addr) break;
        addr = next;
    }
    return !out.empty();
}

bool DmntGame::read(uint64_t addr, void* buf, size_t size) {
    return R_SUCCEEDED(dmntchtReadCheatProcessMemory(addr, buf, size));
}

bool DmntGame::write(uint64_t addr, const void* buf, size_t size) {
    return R_SUCCEEDED(dmntchtWriteCheatProcessMemory(addr, buf, size));
}

bool DmntGame::freeze(uint64_t addr, int width) {
    u64 value = 0;
    return R_SUCCEEDED(dmntchtEnableFrozenAddress(addr, (u64)width, &value));
}

bool DmntGame::unfreeze(uint64_t addr) { return R_SUCCEEDED(dmntchtDisableFrozenAddress(addr)); }

bool DmntGame::frozenAddresses(std::vector<uint64_t>& out) {
    out.clear();
    u64 count = 0;
    if (R_FAILED(dmntchtGetFrozenAddressCount(&count))) return false;
    if (count == 0) return true;
    if (count > 4096) count = 4096;
    std::vector<DmntFrozenAddressEntry> entries(count);
    u64 got = 0;
    if (R_FAILED(dmntchtGetFrozenAddresses(entries.data(), count, 0, &got))) return false;
    for (u64 i = 0; i < got && i < count; i++) out.push_back(entries[i].address);
    return true;
}

bool DmntGame::addCheat(const std::string& name, const std::vector<uint32_t>& opcodes, bool enabled,
                        uint32_t* outId) {
    DmntCheatDefinition def;
    memset(&def, 0, sizeof(def));
    strncpy(def.readable_name, name.c_str(), sizeof(def.readable_name) - 1);
    if (opcodes.size() > 0x100) return false;
    def.num_opcodes = (uint32_t)opcodes.size();
    for (size_t i = 0; i < opcodes.size(); i++) def.opcodes[i] = opcodes[i];
    u32 id = 0;
    if (R_FAILED(dmntchtAddCheat(&def, enabled, &id))) return false;
    if (outId) *outId = id;
    return true;
}

bool DmntGame::removeCheat(uint32_t id) { return R_SUCCEEDED(dmntchtRemoveCheat(id)); }

bool DmntGame::toggleCheat(uint32_t id) { return R_SUCCEEDED(dmntchtToggleCheat(id)); }

bool DmntGame::listCheats(std::vector<CheatInfo>& out) {
    out.clear();
    u64 count = 0;
    if (R_FAILED(dmntchtGetCheatCount(&count))) return false;
    if (count == 0) return true;
    if (count > 0x80) count = 0x80;
    std::vector<DmntCheatEntry> entries(count);
    u64 got = 0;
    if (R_FAILED(dmntchtGetCheats(entries.data(), count, 0, &got))) return false;
    for (u64 i = 0; i < got && i < count; i++) {
        const DmntCheatEntry& e = entries[i];
        CheatInfo ci;
        ci.id = e.cheat_id;
        ci.enabled = e.enabled;
        ci.name.assign(e.definition.readable_name, strnlen(e.definition.readable_name, sizeof(e.definition.readable_name)));
        const u32 n = e.definition.num_opcodes > 0x100 ? 0x100 : e.definition.num_opcodes;
        ci.opcodes.assign(e.definition.opcodes, e.definition.opcodes + n);
        out.push_back(ci);
    }
    return true;
}

bool DmntGame::stillAttached() {
    bool has = false;
    if (R_FAILED(dmntchtHasCheatProcess(&has)) || !has) return false;
    DmntCheatProcessMetadata meta;
    if (R_FAILED(dmntchtGetCheatProcessMetadata(&meta))) return false;
    return meta.process_id == m_pi.processId;
}

}  // namespace vnx
