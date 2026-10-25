#include "multiplier.hpp"

#include <cstring>

namespace vnx {

namespace {

constexpr uint32_t kMod0Magic = 0x30444F4D;   // "MOD0"
constexpr uint64_t kScratchMargin = 32;        // bytes que se dejan sin tocar tras la .bss
constexpr uint32_t kMaxSlots = 16;

// Instrucciones de la máquina virtual de trucos (docs/features/cheats.md de Atmosphère).
// Solo se usan tipos antiguos (0x5, 0x9, 0xA, 0xC0, 0x2) para funcionar en cualquier versión.
// Registros: r0 = valor actual, r1 = valor anterior, r2 = ganancia, r3 = dirección auxiliar.

void emitImm64(std::vector<uint32_t>& o, uint64_t v) {
    o.push_back((uint32_t)(v >> 32));
    o.push_back((uint32_t)(v & 0xFFFFFFFFu));
}

// rX = imm (con AND 0 + OR imm, así el orden de las dos palabras está garantizado)
void emitSetReg(std::vector<uint32_t>& o, uint32_t reg, uint64_t value) {
    o.push_back(0x98500100u | (reg << 16) | (reg << 12));   // 9 8 5(and) R S 1(imm)
    emitImm64(o, 0);
    o.push_back(0x98600100u | (reg << 16) | (reg << 12));   // 9 8 6(or) R S 1(imm)
    emitImm64(o, value);
}

// [base(zona) + offset] = r0, escribiendo 'width' bytes
void emitStoreR0(std::vector<uint32_t>& o, uint32_t region, uint64_t offset, uint32_t width) {
    if (offset < (1ull << 36)) {
        // ATSRIOxa aaaaaaaa, O = 4 (zona + dirección relativa)
        o.push_back(0xA0000000u | (width << 24) | (0u << 20) | (0u << 16) | (4u << 8) | (region << 4) |
                    (uint32_t)((offset >> 32) & 0xF));
        o.push_back((uint32_t)(offset & 0xFFFFFFFFu));
    } else {
        // r3 = offset; O = 3 (zona + registro r3)
        emitSetReg(o, 3, offset);
        o.push_back(0xA0000000u | (width << 24) | (0u << 20) | (3u << 16) | (3u << 8) | (region << 4));
    }
}

bool typeFromWidth(uint32_t w, VType& t) {
    switch (w) {
        case 1: t = VType::U8; return true;
        case 2: t = VType::U16; return true;
        case 4: t = VType::S32; return true;
        case 8: t = VType::S64; return true;
    }
    return false;
}

}  // namespace

bool multiplierSupported(VType t) { return !vtypeIsFloat(t) && !vtypeSwapped(t); }

void multiplierUnsupported(VType t, const std::string& valueName, std::string& title, std::string& body) {
    if (vtypeIsFloat(t)) {
        title = "Solo con números enteros";
        body = "El multiplicador funciona con valores enteros (dinero, experiencia, puntos…). \"" + valueName +
               "\" es un número decimal, así que no se puede multiplicar.";
    } else {
        title = "No disponible en ports";
        body = "El multiplicador no funciona con valores de ports de Wii U o GameCube (guardan los números al "
               "revés). \"" + valueName + "\" sí se puede cambiar y congelar.";
    }
}

uint64_t multiplierCap(VType t) {
    switch (t) {
        case VType::U8: return 0xFFull;
        case VType::U16: return 0xFFFFull;
        case VType::S32: return 0x7FFFFFFFull;
        case VType::S64: return 0x7FFFFFFFFFFFFFFFull;
        default: return 0x7FFFFFFFull;
    }
}

std::vector<uint32_t> buildMultiplierOps(const MultiplierSpec& s) {
    const uint32_t w = (uint32_t)vtypeSize(s.type);
    const uint32_t m = (uint32_t)s.region;
    const uint64_t cap = multiplierCap(s.type);
    std::vector<uint32_t> o;
    // r0 = 0 (los registros pueden venir con datos de otros trucos) y r0 = [valor]
    emitSetReg(o, 0, 0);
    o.push_back(0x50000000u | (w << 24) | (m << 20) | (0u << 16) | (uint32_t)((s.offset >> 32) & 0xFF));
    o.push_back((uint32_t)(s.offset & 0xFFFFFFFFu));
    // r1 = [zona del hueco + hueco] (8 bytes, no hace falta limpiarlo)
    const uint32_t ms = (uint32_t)s.slotRegion;
    o.push_back(0x58010000u | (ms << 20) | (uint32_t)((s.slotOffset >> 32) & 0xFF));
    o.push_back((uint32_t)(s.slotOffset & 0xFFFFFFFFu));
    // si (r1 != 0)
    o.push_back(0xC0861400u);   // C0 T=8 c=6(!=) S=1 X=4(valor fijo)
    emitImm64(o, 0);
    //   si (r0 > r1)
    o.push_back(0xC0810510u);   // C0 T=8 c=1(>) S=0 X=5(otro registro) r=1
    //     r2 = r0 - r1
    o.push_back(0x98120010u);
    //     r2 = r2 * (factor - 1)
    o.push_back(0x98222100u);
    emitImm64(o, (uint64_t)s.factor - 1);
    //     r0 = r0 + r2
    o.push_back(0x98000020u);
    //     si (r0 > tope) r0 = tope
    o.push_back(0xC0810400u);   // C0 T=8 c=1(>) S=0 X=4(valor fijo)
    emitImm64(o, cap);
    emitSetReg(o, 0, cap);
    o.push_back(0x20000000u);
    //     [valor] = r0
    emitStoreR0(o, m, s.offset, w);
    o.push_back(0x20000000u);
    o.push_back(0x20000000u);
    // [zona del hueco + hueco] = r0
    emitStoreR0(o, ms, s.slotOffset, 8);
    return o;
}

bool parseMultiplierOps(const std::vector<uint32_t>& ops, MultiplierSpec& out) {
    // Se leen los parámetros de sus posiciones fijas y se vuelve a generar el truco:
    // solo se acepta si coincide exactamente.
    if (ops.size() < 30) return false;
    const uint32_t load = ops[6];
    if ((load >> 28) != 0x5 || ((load >> 8) & 0xFF) != 0 || ((load >> 16) & 0xF) != 0) return false;
    MultiplierSpec s;
    if (!typeFromWidth((load >> 24) & 0xF, s.type)) return false;
    const uint32_t region = (load >> 20) & 0xF;
    if (region > 3) return false;
    s.region = (RegionKind)region;
    s.offset = ((uint64_t)(load & 0xFF) << 32) | ops[7];
    if ((ops[8] & 0xFF0FFF00u) != 0x58010000u) return false;
    const uint32_t slotRegion = (ops[8] >> 20) & 0xF;
    if (slotRegion != (uint32_t)RegionKind::Main && slotRegion != (uint32_t)RegionKind::Aslr) return false;
    s.slotRegion = (RegionKind)slotRegion;
    s.slotOffset = ((uint64_t)(ops[8] & 0xFF) << 32) | ops[9];
    const uint64_t factorMinus1 = ((uint64_t)ops[16] << 32) | ops[17];
    if (factorMinus1 + 1 < kMinFactor || factorMinus1 + 1 > kMaxFactor) return false;
    s.factor = (uint32_t)(factorMinus1 + 1);
    if (buildMultiplierOps(s) != ops) return false;
    out = s;
    return true;
}

std::string multiplierCheatName(const std::string& valueName, uint32_t factor) {
    return "x" + std::to_string(factor) + " " + valueName + " (ValorNX)";
}

namespace {

// Hueco tras la .bss del módulo que empieza en 'moduleBase' (en base+4 está la posición de su MOD0).
bool scratchInModule(IGame& g, const std::vector<MemRegion>& regs, uint64_t moduleBase, uint64_t maxSpan,
                     uint64_t& slotAbs, uint32_t& slots, std::string& error) {
    uint32_t mod0Rel = 0;
    uint32_t hdr[7] = {};
    if (!g.read(moduleBase + 4, &mod0Rel, 4) || mod0Rel == 0 || mod0Rel >= maxSpan ||
        !g.read(moduleBase + mod0Rel, hdr, sizeof(hdr)) || hdr[0] != kMod0Magic) {
        error = "No se pudo leer la cabecera del ejecutable del juego (MOD0).";
        return false;
    }
    const uint64_t mod0 = moduleBase + mod0Rel;
    const uint64_t bssStart = mod0 + (uint64_t)(int64_t)(int32_t)hdr[2];
    const uint64_t bssEnd = mod0 + (uint64_t)(int64_t)(int32_t)hdr[3];
    if (bssEnd <= bssStart || bssEnd <= moduleBase || bssEnd - moduleBase > maxSpan) {
        error = "La cabecera del ejecutable del juego tiene datos extraños.";
        return false;
    }
    const MemRegion* rw = nullptr;
    for (const MemRegion& r : regs)
        if (r.base <= bssEnd - 1 && bssEnd - 1 < r.end()) rw = &r;
    if (!rw || (rw->perm & memperm::RW) != memperm::RW) {
        error = "No se encontró la memoria de datos del ejecutable del juego.";
        return false;
    }
    const uint64_t start = (bssEnd + 15) & ~15ull;
    const uint64_t first = start + kScratchMargin;
    if (first + 8 > rw->end()) {
        error = "Este juego no tiene memoria libre para guardar el valor anterior.";
        return false;
    }
    // La zona justo tras la .bss debe estar a cero: si no, alguien la usa.
    uint8_t margin[kScratchMargin];
    if (!g.read(start, margin, sizeof(margin))) {
        error = "No se pudo leer la memoria libre del juego.";
        return false;
    }
    for (uint8_t b : margin) {
        if (b != 0) {
            error = "La memoria libre del juego parece estar en uso (quizá por otro truco).";
            return false;
        }
    }
    uint64_t n = (rw->end() - first) / 8;
    if (n > kMaxSlots) n = kMaxSlots;
    slotAbs = first;
    slots = (uint32_t)n;
    return true;
}

}  // namespace

bool findScratch(IGame& g, ScratchArea& out, std::string& error) {
    const ProcessInfo& pi = g.info();
    std::vector<MemRegion> regs;
    g.regions(regs);
    uint64_t abs = 0;
    uint32_t slots = 0;
    if (pi.main.size && scratchInModule(g, regs, pi.main.base, pi.main.size, abs, slots, error)) {
        out.region = RegionKind::Main;
        out.slotBase = abs - pi.main.base;
        out.slots = slots;
        return true;
    }
    // Si el ejecutable principal no tiene sitio, se prueba con los demás módulos (sdk, subsdk...).
    // Cada uno empieza en una zona de código (lectura + ejecución).
    const std::string mainError = error;
    for (const MemRegion& r : regs) {
        if ((r.type & 0xFF) != memtype::CodeStatic || (r.perm & memperm::X) == 0) continue;
        if (pi.main.size && pi.main.contains(r.base)) continue;
        if (r.base < pi.aslr.base) continue;
        std::string ignored;
        if (scratchInModule(g, regs, r.base, 0x40000000ull, abs, slots, ignored)) {
            out.region = RegionKind::Aslr;
            out.slotBase = abs - pi.aslr.base;
            out.slots = slots;
            return true;
        }
    }
    error = mainError;
    return false;
}

}  // namespace vnx
