// ValorNX - aplicación: navegación entre pantallas, diálogos y acciones comunes.
#pragma once
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../core/config.hpp"
#include "../core/favorites.hpp"
#include "../core/control.hpp"
#include "../core/search.hpp"
#include "gfx.hpp"
#include "icons.hpp"
#include "input.hpp"
#include "platform.hpp"

namespace vnx {

constexpr const char* kAppName = "ValorNX";
constexpr const char* kAppVersion = "1.4.0";

namespace theme {
constexpr Color bg = rgba(0x1B, 0x1D, 0x23);
constexpr Color bar = rgba(0x13, 0x15, 0x1A);
constexpr Color card = rgba(0x27, 0x2B, 0x34);
constexpr Color cardHi = rgba(0x32, 0x38, 0x46);
constexpr Color line = rgba(0x3A, 0x40, 0x4E);
constexpr Color text = rgba(0xF2, 0xF3, 0xF7);
constexpr Color dim = rgba(0xA0, 0xA7, 0xB5);
constexpr Color faint = rgba(0x6B, 0x72, 0x82);
constexpr Color accent = rgba(0x00, 0xC3, 0xE3);
constexpr Color gold = rgba(0xFF, 0xC9, 0x40);
constexpr Color green = rgba(0x4C, 0xD9, 0x64);
constexpr Color red = rgba(0xFF, 0x5A, 0x5F);
constexpr Color pink = rgba(0xFF, 0x6B, 0x9A);
constexpr Color purple = rgba(0xA3, 0x7B, 0xFF);
constexpr Color ice = rgba(0x8A, 0xDB, 0xFF);
}  // namespace theme

constexpr int kScreenW = 1280, kScreenH = 720;
constexpr int kHeaderH = 88, kFooterY = 656;
constexpr int kBodyY = 104, kBodyBottom = 640;
constexpr int kMargin = 48;

struct Hint {
    std::string btn;
    std::string label;
};

class App;

class Screen {
public:
    virtual ~Screen() = default;
    virtual void update(App& app, const Input& in) = 0;
    virtual void draw(App& app, Canvas& c) = 0;
    virtual void hints(App& app, std::vector<Hint>& out) {}
    virtual bool modal() const { return false; }
};

struct MenuItem {
    std::string label;
    std::string sub;
    bool hasIcon = false;
    Icon icon = Icon::Check;
    Color color = theme::text;
};
MenuItem menuItem(const std::string& label, const std::string& sub = "");
MenuItem menuItem(Icon icon, Color color, const std::string& label, const std::string& sub = "");

struct KeypadSpec {
    std::string title, subtitle;
    std::string initial;
    bool search = true;            // true: buscar (se elige el tipo); false: escribir un valor
    int typeChoice = 0;            // búsqueda
    bool lockType = false;         // búsqueda con el tipo ya fijado (refinar)
    TypeMask lockedMask = 0;
    VType editType = VType::S32;   // escribir un valor
    std::string infoLine;
    bool quickValues = false;
    // Búsqueda "entre dos números": dos casillas, "Desde" y "Hasta".
    bool range = false;
    bool rejectZero = false;       // el 0 no puede quedar dentro (en la memoria hay muchísimos)
};

// Lista desplazable con selección (botones y pantalla táctil).
struct ListView {
    int x = 0, y = 0, w = 0, h = 0, rowH = 60, gap = 8;
    int sel = 0;
    int scroll = 0;
    // Devuelve el índice activado (A o toque) o -1.
    int update(const Input& in, int count, bool wrap = false);
    void ensureVisible(int count);
    void clampScroll(int count);
    int rowY(int i) const { return y + i * (rowH + gap) - scroll; }
    bool rowVisible(int i) const { return rowY(i) + rowH > y && rowY(i) < y + h; }
    void drawScrollbar(Canvas& c, int count) const;
};

class App {
public:
    App(Platform& platform, Font& font);
    ~App();

    void start();
    void frame(const Input& in, Canvas& c);
    bool wantsExit() const { return m_exit; }

    Platform& platform;
    Font& font;
    IGame* game = nullptr;
    std::string gameError;
    std::string dataDir;
    std::unique_ptr<SearchSession> session;
    std::unique_ptr<FavoriteStore> favs;
    Config cfg;
    uint64_t now = 0;

    // Navegación (las pantallas quitadas se destruyen en el siguiente frame).
    void push(Screen* s);
    void pop();
    void popAll();

    // Diálogos
    void toast(const std::string& msg, bool error = false);
    void message(const std::string& title, const std::string& text, Icon icon = Icon::Info);
    void confirm(const std::string& title, const std::string& text, const std::string& yes,
                 std::function<void()> onYes);
    void menu(const std::string& title, const std::string& sub, const std::vector<MenuItem>& items,
              std::function<void(int)> onPick, int initial = 0);
    void keypad(const KeypadSpec& spec, std::function<void(const std::string&, int)> onOk);
    // Teclado con dos casillas (spec.range): devuelve "desde", "hasta" y el tipo elegido.
    void rangeKeypad(const KeypadSpec& spec,
                     std::function<void(const std::string&, const std::string&, int)> onOk);

    // Trabajo largo con barra de progreso
    void runTask(std::unique_ptr<Task> task, std::function<void(Task&)> done);
    bool busy() const { return (bool)m_task; }

    // Juego (congelados y multiplicadores: GameControl, igual que en el overlay)
    GameControl ctl;
    void connect();
    void refreshFrozen() { ctl.refreshFrozen(); }
    size_t frozenCount() const { return ctl.frozenCount(); }
    bool isFrozen(uint64_t addr) const { return ctl.isFrozen(addr); }
    bool readValue(uint64_t addr, VType t, uint64_t& raw) { return ctl.readValue(addr, t, raw); }
    bool setValue(uint64_t addr, VType t, uint64_t raw) { return ctl.setValue(addr, t, raw); }
    void toggleFreeze(uint64_t addr, VType t);
    void saveConfig();
    void exit() { m_exit = true; }
    // Muestra el resultado de una acción (aviso corto o mensaje si trae título).
    void report(const OpResult& r);

    // Acciones compartidas
    void beginKnownSearch();
    void beginRangeSearch();
    void beginHiddenSearch();
    void openSearch();
    // Guarda en "Mis valores" (pide el nombre). 'after' recibe el valor guardado (o el que ya existía).
    void saveFavorite(uint64_t addr, VType t, std::function<void(const Favorite&)> after = nullptr);
    void pickName(const std::string& title, std::function<void(const std::string&)> onName);
    void editValue(const std::string& title, uint64_t addr, VType t, std::function<void(uint64_t)> after);

    // Multiplicador de ganancias
    void refreshCheats() { ctl.refreshCheats(); }
    const ActiveMultiplier* multiplierFor(const Favorite& f) const { return ctl.multiplierFor(f); }
    size_t activeMultipliers() const { return ctl.activeMultipliers(); }
    void openMultiplier(const Favorite& f);
    void removeMultiplier(const Favorite& f, bool quiet = false);

    // Dibujo común
    void drawHeader(Canvas& c);
    int drawFooter(Canvas& c, const std::vector<Hint>& hints);   // devuelve dónde empiezan las pistas
    static int buttonBadge(Canvas& c, int x, int y, const std::string& btn);   // devuelve el ancho
    static int hintWidth(Canvas& c, const Hint& h);

private:
    std::vector<std::unique_ptr<Screen>> m_stack;
    std::vector<std::unique_ptr<Screen>> m_trash;
    std::unique_ptr<Task> m_task;
    std::function<void(Task&)> m_taskDone;
    uint64_t m_taskStart = 0;
    std::string m_toast;
    bool m_toastErr = false;
    uint64_t m_toastUntil = 0;
    bool m_exit = false;
    uint64_t m_lastCheck = 0;

    void pickFactor(const Favorite& f, std::function<void(uint32_t)> onFactor);
    void confirmMultiplier(const Favorite& f, uint32_t factor);
    void drawTask(Canvas& c);
    void drawToast(Canvas& c, int maxRight);
};

// Pantallas (screens.cpp)
Screen* makeHomeScreen();
Screen* makeNoGameScreen();
Screen* makeSearchScreen();
Screen* makeResultsScreen();
Screen* makeFavoritesScreen();
Screen* makeSettingsScreen();
Screen* makeHelpScreen();
Screen* makeKeypadScreen(const KeypadSpec& spec, std::function<void(const std::string&, int)> onOk);
Screen* makeRangeKeypadScreen(const KeypadSpec& spec,
                              std::function<void(const std::string&, const std::string&, int)> onOk);
Screen* makeMenuScreen(const std::string& title, const std::string& sub, const std::vector<MenuItem>& items,
                       std::function<void(int)> onPick, int initial);
Screen* makeMessageScreen(const std::string& title, const std::string& text, Icon icon);

}  // namespace vnx
