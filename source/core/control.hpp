// ValorNX - lo que Atmosphère aplica al juego: valores congelados y multiplicadores.
//
// Lo usan la app y el overlay, así los dos hacen exactamente lo mismo.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "favorites.hpp"
#include "game.hpp"
#include "multiplier.hpp"

namespace vnx {

// Multiplicador cargado en Atmosphère (creado por ValorNX, ahora o en otra sesión).
struct ActiveMultiplier {
    uint32_t id = 0;
    bool enabled = false;
    std::string name;
    MultiplierSpec spec;
};

// Resultado de una acción, con el texto para el usuario.
struct OpResult {
    bool ok = true;
    std::string title;   // si no está vacío, conviene mostrarlo como mensaje (explicación larga)
    std::string text;
};

class GameControl {
public:
    // g puede ser nullptr (sin juego). sdRoot: raíz de la microSD, para los archivos de trucos.
    void attach(IGame* g, const std::string& sdRoot);
    IGame* game() const { return m_game; }

    void refresh();
    void refreshFrozen();
    void refreshCheats();

    bool readValue(uint64_t addr, VType t, uint64_t& raw);
    // Escribe un valor (si está congelado, se vuelve a congelar con el valor nuevo).
    bool setValue(uint64_t addr, VType t, uint64_t raw);

    bool isFrozen(uint64_t addr) const { return m_frozen.count(addr) > 0; }
    size_t frozenCount() const { return m_frozen.size(); }
    OpResult toggleFreeze(uint64_t addr, VType t);

    const std::vector<ActiveMultiplier>& multipliers() const { return m_mults; }
    const ActiveMultiplier* multiplierFor(const Favorite& f) const;   // el activo si hay varios
    size_t activeMultipliers() const;
    // Crea (o sustituye) el multiplicador de un valor guardado. Con keepSaved, si el que se
    // sustituye estaba guardado como truco en la microSD, el nuevo también se guarda.
    OpResult activateMultiplier(const Favorite& f, uint32_t factor, bool saveFile, bool keepSaved = false);
    OpResult toggleMultiplier(const Favorite& f);   // pausar / reanudar
    OpResult removeMultiplier(const Favorite& f);

private:
    IGame* m_game = nullptr;
    std::string m_sdRoot;
    std::set<uint64_t> m_frozen;
    std::vector<ActiveMultiplier> m_mults;

    uint64_t slotAddr(const MultiplierSpec& s) const;
};

}  // namespace vnx
