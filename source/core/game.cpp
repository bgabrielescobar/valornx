#include "game.hpp"

#include <cstdio>

#include "value.hpp"

namespace vnx {

const char* regionKindName(RegionKind k) {
    switch (k) {
        case RegionKind::Main: return "main";
        case RegionKind::Heap: return "heap";
        case RegionKind::Alias: return "alias";
        case RegionKind::Aslr: return "aslr";
    }
    return "?";
}

std::string ProcessInfo::titleIdHex() const { return formatHex(titleId, 16); }

std::string ProcessInfo::buildIdHex() const {
    std::string s;
    char b[4];
    for (int i = 0; i < 8; i++) {
        snprintf(b, sizeof(b), "%02X", buildId[i]);
        s += b;
    }
    return s;
}

RegionKind ProcessInfo::classify(uint64_t addr, uint64_t* offset) const {
    RegionKind k;
    uint64_t base;
    if (main.size && main.contains(addr)) {
        k = RegionKind::Main;
        base = main.base;
    } else if (heap.size && heap.contains(addr)) {
        k = RegionKind::Heap;
        base = heap.base;
    } else if (alias.size && alias.contains(addr)) {
        k = RegionKind::Alias;
        base = alias.base;
    } else {
        k = RegionKind::Aslr;
        base = addr >= aslr.base ? aslr.base : 0;
    }
    if (offset) *offset = addr - base;
    return k;
}

uint64_t ProcessInfo::baseOf(RegionKind k) const {
    switch (k) {
        case RegionKind::Main: return main.base;
        case RegionKind::Heap: return heap.base;
        case RegionKind::Alias: return alias.base;
        case RegionKind::Aslr: return aslr.base;
    }
    return 0;
}

std::string ProcessInfo::describe(uint64_t addr) const {
    uint64_t off = 0;
    RegionKind k = classify(addr, &off);
    return std::string(regionKindName(k)) + "+0x" + formatHex(off);
}

const char* searchZoneLabel(SearchZone z) {
    switch (z) {
        case SearchZone::All: return "Toda la memoria (recomendado)";
        case SearchZone::Heap: return "Solo heap (datos dinámicos)";
        case SearchZone::Main: return "Solo main (datos fijos del juego)";
    }
    return "?";
}

bool regionWanted(const MemRegion& r, SearchZone zone, const ProcessInfo& pi) {
    if (r.size == 0) return false;
    if ((r.perm & memperm::RW) != memperm::RW) return false;
    const uint32_t t = r.type & 0xFF;
    const bool dataType = t == memtype::Heap || t == memtype::CodeMutable || t == memtype::Normal ||
                          t == memtype::ModuleCodeMutable;
    if (!dataType) return false;
    switch (zone) {
        case SearchZone::All: return true;
        case SearchZone::Heap: return t == memtype::Heap;
        case SearchZone::Main: return pi.main.size != 0 && pi.main.contains(r.base);
    }
    return false;
}

}  // namespace vnx
