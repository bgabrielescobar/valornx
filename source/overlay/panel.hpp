// ValorNX (overlay) - panel rápido encima del juego.
//
// Dos pestañas: "Mis valores" (cambiar, congelar y multiplicar lo guardado) y "Buscar"
// (la misma búsqueda de la app, que sigue guardada en la microSD). No depende de libtesla:
// la parte de la Switch solo le pasa los botones y un Painter, y en PC se prueba igual.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/config.hpp"
#include "../core/control.hpp"
#include "../core/favorites.hpp"
#include "../core/search.hpp"
#include "../ui/input.hpp"
#include "painter.hpp"

namespace vnx {
namespace ovl {

// Lo que el panel necesita de la plataforma.
struct PanelEnv {
    std::string sdRoot;                                     // "sdmc:/" en la Switch
    std::function<IGame*(std::string& error)> connect;      // nullptr si no hay juego
    std::function<void()> disconnect;                       // suelta el juego (cierra dmnt:cht)
    std::function<std::string(uint64_t titleId)> gameName;  // "" si no se sabe
};

// Entrada ya traducida a los botones de la app (ui/input.hpp).
struct PanelInput {
    uint32_t down = 0, held = 0;
    bool tap = false;
    int tapX = 0, tapY = 0;
    int dragY = 0;
    char typed = 0;   // teclado (solo en el simulador de PC)
};

class Page;

class Panel {
public:
    static constexpr int kW = 448, kH = 720;
    static constexpr int kContentY = 96, kContentBottom = 640, kFooterY = 647;
    enum Tab { TabValues = 0, TabSearch = 1 };

    explicit Panel(PanelEnv env);
    ~Panel();

    // Al mostrarse el overlay: conecta con el juego y vuelve a leer "Mis valores" y la búsqueda.
    void show(uint64_t now);
    // Al ocultarse: cancela una búsqueda a medias y suelta el juego (Atmosphère solo admite
    // dos conexiones a la vez).
    void hide();
    // Devuelve false si la B no se usó (en la página principal): el overlay debe cerrarse.
    bool input(const PanelInput& in, uint64_t now);
    void update(uint64_t now);
    void draw(Painter& p);
    // true mientras busca (barra de progreso).
    bool busy() const;

    // ---- para las páginas
    IGame* game() const { return m_game; }
    GameControl& ctl() { return m_ctl; }
    FavoriteStore& favStore() { return m_favs; }
    std::vector<Favorite>& favs() { return m_favs.items(); }
    int findFav(RegionKind region, uint64_t offset, VType type) const { return m_favs.find(region, offset, type); }
    uint64_t addressOf(const Favorite& f) const;
    bool liveValue(size_t i, uint64_t& raw) const;
    void readValuesNow();
    SearchSession* session() { return m_session.get(); }
    Config& cfg() { return m_cfg; }
    void saveConfig();
    int tab() const { return m_tab; }
    void setTab(int t) { m_tab = t; }
    // Búsqueda con barra de progreso. 'done' se llama al terminar, fallar o cancelarse.
    void runTask(std::unique_ptr<Task> task, std::function<void(Panel&, Task&)> done);
    // Una búsqueda se canceló porque se ocultó el overlay (se avisa al volver a abrirlo).
    void noteInterrupted() { m_interrupted = true; }
    uint64_t now() const { return m_now; }
    const std::string& gameTitle() const { return m_gameTitle; }
    const std::string& error() const { return m_error; }
    void toast(const std::string& msg, bool error = false);
    void report(const OpResult& r);
    void message(const std::string& title, const std::string& text);
    void push(Page* p);
    void pop();
    void popToRoot();

private:
    PanelEnv m_env;
    IGame* m_game = nullptr;
    std::string m_error, m_gameTitle;
    uint64_t m_tid = 0, m_pid = 0;
    std::map<uint64_t, std::string> m_names;
    FavoriteStore m_favs;
    GameControl m_ctl;
    std::unique_ptr<SearchSession> m_session;
    Config m_cfg;
    int m_tab = TabValues;
    bool m_interrupted = false;
    std::vector<uint64_t> m_live;
    std::vector<uint8_t> m_liveOk;
    uint64_t m_now = 0, m_lastRead = 0, m_lastRefresh = 0;
    std::string m_toast;
    bool m_toastErr = false;
    uint64_t m_toastUntil = 0;
    std::vector<std::unique_ptr<Page>> m_pages, m_trash;
    uint64_t m_repeatSince = 0, m_lastRepeat = 0;
    bool m_repeating = false;

    std::string dataDir() const;
    void resetPages();
    void drawHeader(Painter& p);
    void drawFooter(Painter& p);
    void drawToast(Painter& p);
};

}  // namespace ovl
}  // namespace vnx
