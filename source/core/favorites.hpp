// ValorNX - valores guardados por juego ("Mis valores").
#pragma once
#include <string>
#include <vector>

#include "game.hpp"
#include "value.hpp"

namespace vnx {

struct Favorite {
    std::string name;
    VType type = VType::S32;
    RegionKind region = RegionKind::Heap;
    uint64_t offset = 0;      // relativo a la zona, así sirve aunque cambie la dirección base
    std::string buildId;      // versión del juego en la que se guardó
    bool hasValue = false;    // último valor que puso el usuario
    uint64_t value = 0;
};

class FavoriteStore {
public:
    explicit FavoriteStore(const std::string& dir);
    bool load(uint64_t titleId);
    bool save() const;
    // Vuelve a leer el archivo si lo cambió otro programa (la app o el overlay). true si cambió.
    bool sync();
    std::vector<Favorite>& items() { return m_items; }
    const std::vector<Favorite>& items() const { return m_items; }
    static uint64_t address(const Favorite& f, const ProcessInfo& pi) { return pi.baseOf(f.region) + f.offset; }
    std::string uniqueName(const std::string& base) const;
    int find(RegionKind region, uint64_t offset, VType type) const;   // -1 si no existe

private:
    std::string m_dir, m_path;
    std::vector<Favorite> m_items;
    mutable std::string m_text;   // contenido del archivo la última vez que se leyó o se guardó

    void parse(const std::string& text);
};

}  // namespace vnx
