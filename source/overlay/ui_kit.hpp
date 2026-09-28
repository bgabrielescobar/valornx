// ValorNX (overlay) - piezas comunes de las páginas: colores, listas, menús, mensajes
// y el teclado numérico (para cambiar un valor o para buscar).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../core/numentry.hpp"
#include "../core/value.hpp"
#include "panel.hpp"

namespace vnx {
namespace ovl {

// Colores pensados para la capa de 4 bits por canal de libtesla.
namespace col {
constexpr Color bg = rgba(0, 0, 0, 221);
constexpr Color edge = rgba(34, 34, 34);
constexpr Color text = rgba(255, 255, 255);
constexpr Color dim = rgba(170, 170, 170);
constexpr Color faint = rgba(119, 119, 119);
constexpr Color line = rgba(255, 255, 255, 40);
constexpr Color accent = rgba(0, 204, 238);
constexpr Color gold = rgba(255, 204, 68);
constexpr Color ice = rgba(136, 221, 255);
constexpr Color red = rgba(255, 85, 85);
constexpr Color green = rgba(85, 221, 102);
constexpr Color pink = rgba(255, 119, 170);
constexpr Color purple = rgba(170, 136, 255);
constexpr Color selBg = rgba(255, 255, 255, 34);
constexpr Color keyBg = rgba(255, 255, 255, 26);
constexpr Color keySel = rgba(255, 255, 255, 68);
constexpr Color dark = rgba(17, 17, 17);
constexpr Color display = rgba(0, 0, 0, 187);
}  // namespace col

struct Hint {
    std::string btn, label;
};

class Page {
public:
    virtual ~Page() = default;
    // true si usó la entrada. La página principal devuelve false con la B (cerrar el overlay).
    virtual bool input(Panel& panel, const PanelInput& in) = 0;
    virtual void update(Panel&) {}
    virtual void draw(Panel& panel, Painter& p) = 0;
    virtual void hints(Panel& panel, std::vector<Hint>& out) = 0;
    // El overlay se oculta: hay que dejar de usar el juego (por ejemplo, cancelar una búsqueda).
    virtual void abort(Panel&) {}
    // true mientras hace un trabajo largo (búsqueda): el panel no lee nada más mientras tanto.
    virtual bool working() const { return false; }
};

constexpr uint32_t kDirs = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT;

// Lista vertical con selección, desplazamiento y toques.
struct Scroller {
    int y = 0, h = 0, rowH = 60;
    int sel = 0, scroll = 0;

    int rowY(int i) const { return y + i * rowH - scroll; }
    bool visible(int i) const { return rowY(i) + rowH > y && rowY(i) < y + h; }
    void clamp(int count);
    void ensureVisible(int count);
    // Mueve la selección con arriba/abajo y el dedo; devuelve el índice tocado (o -1).
    int handle(const PanelInput& in, int count);
    void drawScrollbar(Painter& p, int count) const;
};

// Explicación corta de cada tipo de valor (mismo orden que typeChoice()).
const char* shortTypeHelp(int choice);

// Pantalla vacía con icono, título y explicación (sin juego, sin valores...).
void drawEmpty(Painter& p, Icon ic, Color icColor, const std::string& title, const std::string& body,
               const std::string& extra = "", Color extraColor = col::red);

// ------------------------------------------------------------------ menú

struct MenuItemO {
    Icon icon;
    Color color;
    std::string label, sub;
};

class MenuPage : public Page {
public:
    MenuPage(const std::string& title, const std::string& sub, std::vector<MenuItemO> items,
             std::function<void(Panel&, int)> onPick);
    bool input(Panel& panel, const PanelInput& in) override;
    void draw(Panel& panel, Painter& p) override;
    void hints(Panel& panel, std::vector<Hint>& out) override;

private:
    std::string m_title, m_sub;
    std::vector<MenuItemO> m_items;
    std::function<void(Panel&, int)> m_onPick;
    std::vector<std::string> m_subLines;
    bool m_hasSubs = false, m_laidOut = false;
    Scroller m_list;

    void layout(Painter& p);
};

// "¿Seguro?": sí / cancelar.
void confirm(Panel& panel, const std::string& title, const std::string& text, const std::string& yes,
             std::function<void(Panel&)> onYes);

// ------------------------------------------------------------------ mensaje

class MessagePage : public Page {
public:
    MessagePage(const std::string& title, const std::string& text) : m_title(title), m_text(text) {}
    bool input(Panel& panel, const PanelInput& in) override;
    void draw(Panel& panel, Painter& p) override;
    void hints(Panel&, std::vector<Hint>& out) override { out.push_back({"A", "Aceptar"}); }

private:
    std::string m_title, m_text;
};

// ------------------------------------------------------------------ teclado

struct KeypadSpec {
    std::string title;             // "Cambiar: Dinero", "¿Qué número ves?"...
    std::string info;              // línea gris debajo del título
    std::string initial;
    bool search = false;           // números para buscar (valen si sirven para alguno de los tipos)
    TypeMask lockedMask = 0;       // buscar con los tipos ya fijados (continuar); 0 = se eligen abajo
    int typeChoice = 0;            // buscar: tipo elegido al abrir
    VType editType = VType::S32;   // cambiar un valor: su tipo
    bool quick = false;            // fila de valores rápidos (99, 999...)
    bool range = false;            // dos casillas: "Desde" y "Hasta"
    bool rejectZero = false;       // el rango no puede incluir el 0
};

// Recibe el número (o "desde" y "hasta" si es un rango, ya ordenados) y el tipo elegido.
// La página ya se ha quitado cuando se llama.
using KeypadDone = std::function<void(Panel&, const std::string&, const std::string&, int)>;

class KeypadPage : public Page {
public:
    KeypadPage(const KeypadSpec& spec, KeypadDone done);
    bool input(Panel& panel, const PanelInput& in) override;
    void draw(Panel& panel, Painter& p) override;
    void hints(Panel& panel, std::vector<Hint>& out) override;

private:
    struct Cell {
        int row, col, span;
        char key;              // '0'-'9', '.', '<' borrar, 'C' limpiar, '-' signo, 'K' OK, 'Q' rápido, 'T' tipo
        std::string quick;
    };
    KeypadSpec m_spec;
    KeypadDone m_done;
    NumberEntry m_entry[2];
    int m_field = 0;
    int m_type = 0;
    std::string m_err;
    std::vector<Cell> m_cells;
    int m_rows = 4, m_sel = 0, m_row = 1, m_col = 1;

    static constexpr int kGridX = 20, kGridY = 272, kCellW = 96, kCellH = 54, kGap = 8;
    static constexpr int kDispY = Panel::kContentY + 62, kDispH = 62;

    bool typeSelectable() const { return m_spec.search && m_spec.lockedMask == 0; }
    TypeMask mask() const;
    bool decimalsAllowed() const;
    void cellRect(const Cell& c, int& x, int& y, int& w, int& h) const;
    void fieldRect(int f, int& x, int& y, int& w, int& h) const;
    int cellAt(int row, int col) const;
    void move(int dr, int dc);
    void changeType(int delta);
    void switchField(int f);
    // Devuelven true si la página se cerró.
    bool press(Panel& panel, char key, const std::string& quick);
    bool submit(Panel& panel);
    bool submitRange(Panel& panel);
    std::string searchError(const Number& n) const;
};

}  // namespace ovl
}  // namespace vnx
