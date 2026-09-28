#include "control.hpp"

#include "cheats.hpp"

namespace vnx {

namespace {

bool sameValue(const MultiplierSpec& s, const Favorite& f) {
    return s.region == f.region && s.offset == f.offset && s.type == f.type;
}

OpResult failure(const std::string& title, const std::string& text) {
    OpResult r;
    r.ok = false;
    r.title = title;
    r.text = text;
    return r;
}

OpResult notice(bool ok, const std::string& text) {
    OpResult r;
    r.ok = ok;
    r.text = text;
    return r;
}

}  // namespace

void GameControl::attach(IGame* g, const std::string& sdRoot) {
    m_game = g;
    m_sdRoot = sdRoot;
    m_frozen.clear();
    m_mults.clear();
    refresh();
}

void GameControl::refresh() {
    refreshFrozen();
    refreshCheats();
}

void GameControl::refreshFrozen() {
    m_frozen.clear();
    std::vector<uint64_t> v;
    if (m_game && m_game->frozenAddresses(v)) m_frozen.insert(v.begin(), v.end());
}

void GameControl::refreshCheats() {
    m_mults.clear();
    std::vector<CheatInfo> list;
    if (!m_game || !m_game->listCheats(list)) return;
    for (const CheatInfo& c : list) {
        ActiveMultiplier m;
        if (!parseMultiplierOps(c.opcodes, m.spec)) continue;
        m.id = c.id;
        m.enabled = c.enabled;
        m.name = c.name;
        m_mults.push_back(m);
    }
}

bool GameControl::readValue(uint64_t addr, VType t, uint64_t& raw) {
    uint8_t b[8] = {};
    if (!m_game || !m_game->read(addr, b, (size_t)vtypeSize(t))) return false;
    raw = loadRaw(b, t);
    return true;
}

bool GameControl::setValue(uint64_t addr, VType t, uint64_t raw) {
    if (!m_game) return false;
    uint8_t b[8] = {};
    storeRaw(b, t, raw);
    // Si está congelado hay que soltarlo, escribir y volver a congelarlo con el valor nuevo.
    const bool frozen = isFrozen(addr);
    if (frozen) m_game->unfreeze(addr);
    const bool ok = m_game->write(addr, b, (size_t)vtypeSize(t));
    if (frozen) m_game->freeze(addr, vtypeSize(t));
    refreshFrozen();
    return ok;
}

OpResult GameControl::toggleFreeze(uint64_t addr, VType t) {
    if (!m_game) return notice(false, "No hay ningún juego abierto.");
    OpResult r;
    if (isFrozen(addr)) {
        r = m_game->unfreeze(addr) ? notice(true, "Valor descongelado.") : notice(false, "No se pudo descongelar.");
    } else {
        r = m_game->freeze(addr, vtypeSize(t)) ? notice(true, "Congelado: el juego ya no podrá cambiar este valor.")
                                               : notice(false, "No se pudo congelar el valor.");
    }
    refreshFrozen();
    return r;
}

const ActiveMultiplier* GameControl::multiplierFor(const Favorite& f) const {
    const ActiveMultiplier* found = nullptr;
    for (const ActiveMultiplier& m : m_mults) {
        if (!sameValue(m.spec, f)) continue;
        if (m.enabled) return &m;
        if (!found) found = &m;
    }
    return found;
}

size_t GameControl::activeMultipliers() const {
    size_t n = 0;
    for (const ActiveMultiplier& m : m_mults)
        if (m.enabled) n++;
    return n;
}

uint64_t GameControl::slotAddr(const MultiplierSpec& s) const {
    return m_game ? m_game->info().baseOf(s.slotRegion) + s.slotOffset : 0;
}

OpResult GameControl::activateMultiplier(const Favorite& f, uint32_t factor, bool saveFile, bool keepSaved) {
    const char* kTitle = "No se pudo activar el multiplicador";
    if (!m_game) return notice(false, "No hay ningún juego abierto.");
    if (!multiplierSupported(f.type))
        return failure("Solo con números enteros",
                       "El multiplicador funciona con valores enteros (dinero, experiencia, puntos…). \"" + f.name +
                           "\" es un número decimal, así que no se puede multiplicar.");
    ScratchArea area;
    std::string err;
    if (!findScratch(*m_game, area, err)) return failure(kTitle, err);
    const ProcessInfo& pi = m_game->info();
    refreshCheats();
    // Si este valor ya tenía multiplicador, se sustituye y se reutiliza su hueco.
    std::set<uint64_t> used;   // direcciones absolutas de los huecos ocupados
    std::vector<ActiveMultiplier> old;
    for (const ActiveMultiplier& m : m_mults) {
        if (sameValue(m.spec, f))
            old.push_back(m);
        else
            used.insert(slotAddr(m.spec));
    }
    const uint64_t areaBase = pi.baseOf(area.region) + area.slotBase;
    const uint64_t areaEnd = areaBase + 8ull * area.slots;
    uint64_t slot = UINT64_MAX;
    for (const ActiveMultiplier& m : old) {
        const uint64_t a = slotAddr(m.spec);
        if (slot == UINT64_MAX && !used.count(a) && a >= areaBase && a < areaEnd) slot = a;
        if (keepSaved && cheatFileHas(m_sdRoot, pi, m.name)) saveFile = true;
        m_game->removeCheat(m.id);
        removeCheatFromFile(m_sdRoot, pi, m.name);
    }
    for (uint32_t i = 0; i < area.slots && slot == UINT64_MAX; i++) {
        const uint64_t a = areaBase + 8ull * i;
        uint64_t current = 0;
        if (used.count(a) || !m_game->read(a, &current, 8) || current != 0) continue;
        slot = a;
    }
    if (slot == UINT64_MAX) {
        refreshCheats();
        return failure(kTitle, "No quedan huecos libres para más multiplicadores en este juego. Quita alguno que no uses.");
    }
    const uint64_t zero = 0;
    m_game->write(slot, &zero, 8);   // empieza de cero: la primera vez solo recuerda el valor
    MultiplierSpec spec;
    spec.region = f.region;
    spec.offset = f.offset;
    spec.type = f.type;
    spec.factor = factor;
    spec.slotRegion = area.region;
    spec.slotOffset = slot - pi.baseOf(area.region);
    const std::vector<uint32_t> ops = buildMultiplierOps(spec);
    const std::string name = cheatSafeName(multiplierCheatName(f.name, factor));
    if (!m_game->addCheat(name, ops, true)) {
        refreshCheats();
        return failure(kTitle, "Atmosphère no aceptó el truco.");
    }
    bool fileOk = true;
    if (saveFile) {
        std::string path;
        fileOk = writeCheat(m_sdRoot, pi, name, ops, path);
    }
    refreshCheats();
    if (!fileOk) return notice(false, "Activado, pero no se pudo guardar el truco en la microSD.");
    return notice(true, "Multiplicador x" + std::to_string(factor) + " activado para " + f.name + ".");
}

OpResult GameControl::toggleMultiplier(const Favorite& f) {
    if (!m_game) return notice(false, "No hay ningún juego abierto.");
    refreshCheats();
    bool any = false, anyEnabled = false;
    for (const ActiveMultiplier& m : m_mults) {
        if (!sameValue(m.spec, f)) continue;
        any = true;
        anyEnabled = anyEnabled || m.enabled;
    }
    if (!any) return notice(false, "Este valor no tiene multiplicador.");
    const bool enable = !anyEnabled;
    for (const ActiveMultiplier& m : m_mults) {
        if (!sameValue(m.spec, f) || m.enabled == enable) continue;
        if (enable) {
            // Al reanudar se olvida el valor viejo: lo ganado en pausa no se multiplica.
            const uint64_t zero = 0;
            m_game->write(slotAddr(m.spec), &zero, 8);
        }
        m_game->toggleCheat(m.id);
    }
    refreshCheats();
    return notice(true, enable ? "Multiplicador reanudado." : "Multiplicador en pausa.");
}

OpResult GameControl::removeMultiplier(const Favorite& f) {
    if (!m_game) return notice(false, "No hay ningún juego abierto.");
    refreshCheats();
    std::vector<uint64_t> freed;
    for (const ActiveMultiplier& m : m_mults) {
        if (!sameValue(m.spec, f)) continue;
        m_game->removeCheat(m.id);
        removeCheatFromFile(m_sdRoot, m_game->info(), m.name);
        freed.push_back(slotAddr(m.spec));
    }
    refreshCheats();
    // El hueco vuelve a cero para que otro multiplicador lo pueda usar (si nadie más lo usa).
    for (uint64_t a : freed) {
        bool inUse = false;
        for (const ActiveMultiplier& m : m_mults) inUse = inUse || slotAddr(m.spec) == a;
        const uint64_t zero = 0;
        if (!inUse) m_game->write(a, &zero, 8);
    }
    return freed.empty() ? notice(false, "Este valor no tenía multiplicador.") : notice(true, "Multiplicador quitado.");
}

}  // namespace vnx
