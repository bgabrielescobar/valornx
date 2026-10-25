#include "mock_game.hpp"

#include <cstring>

using namespace vnx;

uint32_t MockGame::rnd() {
    m_rng = m_rng * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(m_rng >> 33);
}

void MockGame::addSeg(uint64_t base, uint64_t size, uint32_t type, uint32_t perm, bool fill) {
    Seg s;
    s.r.base = base;
    s.r.size = size;
    s.r.type = type;
    s.r.perm = perm;
    s.data.assign(size, 0);
    if (fill) {
        for (uint64_t off = 0; off + 4 <= size; off += 4) {
            uint32_t kind = rnd() % 100;
            uint32_t v = 0;
            if (kind < 50) {
                v = 0;
            } else if (kind < 70) {
                v = rnd() % 2000;
            } else if (kind < 85) {
                float f = (float)(rnd() % 100000) / 100.0f;
                memcpy(&v, &f, 4);
            } else {
                v = rnd();
            }
            memcpy(&s.data[off], &v, 4);
        }
    }
    m_segs.push_back(std::move(s));
}

MockGame::MockGame(uint64_t heapSize) {
    m_pi.titleId = 0x0100ABCD12340000ull;
    m_pi.processId = 0x51;
    for (int i = 0; i < 0x20; i++) m_pi.buildId[i] = (uint8_t)(0xA0 + i);
    m_pi.gameName = "Aventura de Prueba";
    m_pi.aslr = {0x0000000008000000ull, 0x0000007FF8000000ull};
    m_pi.main = {0x0000000080004000ull, 0x0000000000C00000ull};
    m_pi.heap = {0x0000001000000000ull, 0x0000000180000000ull};
    m_pi.alias = {0x0000000A00000000ull, 0x0000001000000000ull};

    const uint64_t mainBase = m_pi.main.base;
    addSeg(mainBase, 0x800000, memtype::CodeStatic, memperm::R | memperm::X, true);            // .text
    addSeg(mainBase + 0x800000, 0x200000, memtype::CodeStatic, memperm::R, true);             // .rodata
    addSeg(mainBase + 0xA00000, 0x100000, memtype::CodeMutable, memperm::RW, true);           // .data
    addSeg(mainBase + 0xB00000, 0x80000, memtype::CodeMutable, memperm::RW, false);           // .bss
    addSeg(0x0000000900000000ull, 0x10000, memtype::Io, memperm::RW, false);                  // E/S
    addSeg(0x0000000A00000000ull, 0x800000, memtype::Normal, memperm::RW, true);              // alias
    addSeg(m_pi.heap.base, heapSize, memtype::Heap, memperm::RW, true);                       // heap
    addSeg(m_pi.heap.base + heapSize + 0x100000, 0x100000, memtype::Heap, memperm::RW, true); // heap 2
    addSeg(m_pi.heap.base + heapSize + 0x300000, 0x40000, memtype::SharedMem, memperm::RW, true);
    addSeg(m_pi.aslr.base + 0x1100000000ull, 0x10000, memtype::Normal, memperm::RW, false);   // muy lejos (ASLR)
    const uint64_t sdkBase = 0x0000002000000000ull;                                            // otro módulo
    addSeg(sdkBase, 0x100000, memtype::CodeStatic, memperm::R | memperm::X, true);             // sdk .text
    addSeg(sdkBase + 0x100000, 0x20000, memtype::CodeMutable, memperm::RW, false);             // sdk .data/.bss

    // Heap 2 tiene una página que no se puede leer.
    Seg& h2 = m_segs[7];
    h2.badPageStart = h2.r.base + 0x3000;
    h2.badPageEnd = h2.r.base + 0x4000;

    const uint64_t heap = m_pi.heap.base;
    moneyAddr = heap + 0x123450;
    moneyCopyAddr = heap + 0x200010;
    hpAddr = heap + 0x2340;
    expAddr = mainBase + 0xA00100;
    levelAddr = heap + 0x8000;
    itemsAddr = heap + 0x9001;
    rupeesAddr = heap + 0x3C4C0C;
    magicAddr = heap + 0x3C4C20;
    ioValueAddr = 0x0000000900000000ull + 0x100;
    farAddr = m_pi.aslr.base + 0x1100000000ull + 0x40;
    badPageValueAddr = h2.badPageStart + 0x10;
    goodPageValueAddr = h2.r.base + 0x5010;

    // Cabecera MOD0 del ejecutable principal: en base+4 va su posición; la .bss acaba antes
    // del final de su página, como en un juego real.
    const uint64_t bssBase = mainBase + 0xB00000;
    bssEnd = bssBase + 0x7F7F4;
    const uint32_t mod0Rel = 0x80;
    write(mainBase + 4, &mod0Rel, 4);
    const uint64_t mod0 = mainBase + mod0Rel;
    const uint32_t mod0Hdr[7] = {0x30444F4Du, 0x1000u, (uint32_t)(bssBase - mod0), (uint32_t)(bssEnd - mod0), 0, 0,
                                 (uint32_t)(bssBase + 0x100 - mod0)};
    write(mod0, mod0Hdr, sizeof(mod0Hdr));
    // La .bss en uso tiene datos hasta el final (el hueco que queda detrás está libre).
    const uint32_t used = 0xDEADBEEFu;
    write(bssEnd - 4, &used, 4);
    // Lo mismo para el segundo módulo.
    sdkBssEnd = sdkBase + 0x100000 + 0x1E008;
    const uint32_t sdkMod0Rel = 0x40;
    write(sdkBase + 4, &sdkMod0Rel, 4);
    const uint64_t sdkMod0 = sdkBase + sdkMod0Rel;
    const uint32_t sdkHdr[7] = {0x30444F4Du, 0x800u, (uint32_t)(sdkBase + 0x110000 - sdkMod0),
                                (uint32_t)(sdkBssEnd - sdkMod0), 0, 0, 0};
    write(sdkMod0, sdkHdr, sizeof(sdkHdr));
    write(sdkBssEnd - 4, &used, 4);

    setS32(moneyAddr, 1500);
    setS32(moneyCopyAddr, 1500);
    setF32(hpAddr, 87.5f);
    setS32(expAddr, 12345);
    setS32(farAddr, 1000);
    uint16_t lvl = 12;
    write(levelAddr, &lvl, 2);
    uint8_t items = 5;
    write(itemsAddr, &items, 1);
    setU16BE(rupeesAddr, 150);
    setF32BE(magicAddr, 32.0f);
    // Valores trampa: en E/S (no se debe leer) y en la página ilegible.
    int32_t special = 424242;
    for (Seg& s : m_segs)
        if (ioValueAddr >= s.r.base && ioValueAddr < s.r.end()) memcpy(&s.data[ioValueAddr - s.r.base], &special, 4);
    memcpy(&h2.data[badPageValueAddr - h2.r.base], &special, 4);
    memcpy(&h2.data[goodPageValueAddr - h2.r.base], &special, 4);
}

MockGame::Seg* MockGame::segFor(uint64_t addr, size_t size) {
    for (Seg& s : m_segs)
        if (addr >= s.r.base && addr + size <= s.r.end()) return &s;
    return nullptr;
}

bool MockGame::regions(std::vector<MemRegion>& out) {
    out.clear();
    uint64_t last = 0;
    for (const Seg& s : m_segs) {
        if (s.r.base > last) {
            MemRegion gap;
            gap.base = last;
            gap.size = s.r.base - last;
            gap.type = memtype::Unmapped;
            out.push_back(gap);
        }
        out.push_back(s.r);
        last = s.r.end();
    }
    return true;
}

bool MockGame::read(uint64_t addr, void* buf, size_t size) {
    readCalls++;
    Seg* s = segFor(addr, size);
    if (!s) return false;
    if (s->badPageEnd > s->badPageStart && addr < s->badPageEnd && addr + size > s->badPageStart) return false;
    memcpy(buf, &s->data[addr - s->r.base], size);
    return true;
}

bool MockGame::write(uint64_t addr, const void* buf, size_t size) {
    Seg* s = segFor(addr, size);
    if (!s) return false;
    memcpy(&s->data[addr - s->r.base], buf, size);
    return true;
}

bool MockGame::freeze(uint64_t addr, int width) {
    uint64_t v = 0;
    if (!read(addr, &v, (size_t)width)) return false;
    m_frozen[addr] = {v, width};
    return true;
}

bool MockGame::unfreeze(uint64_t addr) { return m_frozen.erase(addr) > 0; }

bool MockGame::frozenAddresses(std::vector<uint64_t>& out) {
    out.clear();
    for (auto& kv : m_frozen) out.push_back(kv.first);
    return true;
}

bool MockGame::addCheat(const std::string& name, const std::vector<uint32_t>& opcodes, bool enabled,
                        uint32_t* outId) {
    if (opcodes.empty() || opcodes.size() > 0x100) return false;
    CheatInfo c;
    c.id = m_nextCheatId++;
    c.enabled = enabled;
    c.name = name.substr(0, 0x3F);
    c.opcodes = opcodes;
    m_cheats.push_back(c);
    if (outId) *outId = c.id;
    return true;
}

bool MockGame::removeCheat(uint32_t id) {
    for (size_t i = 0; i < m_cheats.size(); i++)
        if (m_cheats[i].id == id) {
            m_cheats.erase(m_cheats.begin() + (long)i);
            return true;
        }
    return false;
}

bool MockGame::toggleCheat(uint32_t id) {
    for (CheatInfo& c : m_cheats)
        if (c.id == id) {
            c.enabled = !c.enabled;
            return true;
        }
    return false;
}

bool MockGame::listCheats(std::vector<CheatInfo>& out) {
    out = m_cheats;
    return true;
}

void MockGame::tick() {
    // Igual que dmnt: todos los trucos activos forman un solo programa.
    std::vector<uint32_t> program;
    for (const CheatInfo& c : m_cheats)
        if (c.enabled) program.insert(program.end(), c.opcodes.begin(), c.opcodes.end());
    if (!program.empty()) lastVmOk = m_vm.execute(program, *this);
    for (auto& kv : m_frozen) write(kv.first, &kv.second.first, (size_t)kv.second.second);
}

void MockGame::noise(int count) {
    Seg& heap = m_segs[6];
    for (int i = 0; i < count; i++) {
        uint64_t off = ((uint64_t)rnd() % (heap.r.size / 4)) * 4;
        uint64_t addr = heap.r.base + off;
        if (addr >= hpAddr - 64 && addr <= hpAddr + 64) continue;
        if (addr >= moneyAddr - 64 && addr <= moneyAddr + 64) continue;
        if (addr >= moneyCopyAddr - 64 && addr <= moneyCopyAddr + 64) continue;
        if (addr >= levelAddr - 64 && addr <= itemsAddr + 64) continue;
        if (addr >= rupeesAddr - 64 && addr <= magicAddr + 64) continue;
        uint32_t v;
        memcpy(&v, &heap.data[off], 4);
        v += (rnd() & 1) ? 1 : (uint32_t)-1;
        memcpy(&heap.data[off], &v, 4);
    }
    tick();
}

int32_t MockGame::getS32(uint64_t addr) {
    int32_t v = 0;
    read(addr, &v, 4);
    return v;
}
void MockGame::setS32(uint64_t addr, int32_t v) { write(addr, &v, 4); }
float MockGame::getF32(uint64_t addr) {
    float v = 0;
    read(addr, &v, 4);
    return v;
}
void MockGame::setF32(uint64_t addr, float v) { write(addr, &v, 4); }
uint16_t MockGame::getU16BE(uint64_t addr) {
    uint8_t b[2] = {};
    read(addr, b, 2);
    return (uint16_t)((b[0] << 8) | b[1]);
}
void MockGame::setU16BE(uint64_t addr, uint16_t v) {
    const uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    write(addr, b, 2);
}
float MockGame::getF32BE(uint64_t addr) {
    uint8_t b[4] = {};
    read(addr, b, 4);
    const uint32_t u = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    float f;
    memcpy(&f, &u, 4);
    return f;
}
void MockGame::setF32BE(uint64_t addr, float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    const uint8_t b[4] = {(uint8_t)(u >> 24), (uint8_t)(u >> 16), (uint8_t)(u >> 8), (uint8_t)u};
    write(addr, b, 4);
}

void MockGame::spendMoney(int32_t n) {
    setS32(moneyAddr, getS32(moneyAddr) - n);
    setS32(moneyCopyAddr, getS32(moneyAddr));
    tick();
}

void MockGame::takeDamage(float x) {
    setF32(hpAddr, getF32(hpAddr) - x);
    tick();
}

void MockGame::gainExp(int32_t n) { setS32(expAddr, getS32(expAddr) + n); }

void MockGame::gainRupees(int n) {
    setU16BE(rupeesAddr, (uint16_t)(getU16BE(rupeesAddr) + n));
    tick();
}
