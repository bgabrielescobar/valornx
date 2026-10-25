// ValorNX - todas las pantallas de la interfaz.
#include <algorithm>
#include <cstdio>

#include "../core/cheats.hpp"
#include "../core/fsutil.hpp"
#include "../core/numentry.hpp"
#include "app.hpp"

namespace vnx {

namespace {

constexpr size_t kListMax = 100;   // resultados que se muestran en la lista

void cardRow(Canvas& c, int x, int y, int w, int h, bool sel, int radius = 14) {
    if (sel) {
        c.fillRoundRect(x, y, w, h, radius, theme::cardHi);
        c.strokeRoundRect(x - 3, y - 3, w + 6, h + 6, radius + 3, 3, theme::accent);
    } else {
        c.fillRoundRect(x, y, w, h, radius, theme::card);
    }
}

void iconBadge(Canvas& c, int x, int y, int size, Icon icon, Color color) {
    c.fillRoundRect(x, y, size, size, size / 4, withAlpha(color, 46));
    const int is = size * 60 / 100;
    drawIcon(c, icon, x + (size - is) / 2, y + (size - is) / 2, is, color);
}

void listClip(Canvas& c, const ListView& lv) { c.pushClip(lv.x - 5, lv.y - 5, lv.w + 10, lv.h + 10); }

ListView makeList(int x, int y, int w, int h, int rowH, int gap) {
    ListView lv;
    lv.x = x;
    lv.y = y;
    lv.w = w;
    lv.h = h;
    lv.rowH = rowH;
    lv.gap = gap;
    return lv;
}

std::function<void(Task&)> refineDone(App& app) {
    return [&app](Task& t) {
        if (t.status() == Task::Status::Done) {
            const uint64_t n = t.found();
            if (n == 0)
                app.toast("No quedó ningún resultado.", true);
            else if (n == 1)
                app.toast("¡Queda 1 resultado!");
            else
                app.toast("Quedan " + formatCount(n) + " resultados.");
        } else if (t.status() == Task::Status::Failed) {
            app.message("No se pudo continuar la búsqueda", t.error(), Icon::Warning);
        } else {
            app.toast("Cancelado.");
        }
    };
}

// ================================================================== Inicio

class HomeScreen : public Screen {
public:
    HomeScreen() { m_lv = makeList(kMargin, 112, 1184, 528, 78, 10); }

    void update(App& app, const Input& in) override {
        build(app);
        if (m_first) {
            m_first = false;
            m_lv.sel = !app.favs->items().empty() ? 4 : (app.session->active() ? 3 : 0);
        }
        if (in.down & BTN_PLUS) {
            app.exit();
            return;
        }
        const int act = m_lv.update(in, (int)m_items.size(), true);
        if (act < 0) return;
        switch (act) {
            case 0: app.beginKnownSearch(); break;
            case 1: app.beginRangeSearch(); break;
            case 2: app.beginHiddenSearch(); break;
            case 3:
                if (app.session->active())
                    app.openSearch();
                else
                    app.toast("No hay ninguna búsqueda en curso.", true);
                break;
            case 4: app.push(makeFavoritesScreen()); break;
            case 5: app.push(makeSettingsScreen()); break;
        }
    }

    void draw(App& app, Canvas& c) override {
        build(app);
        listClip(c, m_lv);
        for (int i = 0; i < (int)m_items.size(); i++) {
            const Item& it = m_items[i];
            const int y = m_lv.rowY(i);
            cardRow(c, m_lv.x, y, m_lv.w, m_lv.rowH, i == m_lv.sel);
            iconBadge(c, m_lv.x + 16, y + 11, 56, it.icon, it.enabled ? it.color : theme::faint);
            c.text(m_lv.x + 92, y + 8, it.title, 27, it.enabled ? theme::text : theme::faint);
            c.text(m_lv.x + 92, y + 45, it.sub, 20, theme::dim, Align::Left, m_lv.w - 120);
        }
        c.popClip();
    }

    void hints(App&, std::vector<Hint>& out) override {
        out.push_back({"A", "Elegir"});
        out.push_back({"+", "Salir"});
    }

private:
    struct Item {
        Icon icon;
        Color color;
        std::string title, sub;
        bool enabled;
    };
    std::vector<Item> m_items;
    ListView m_lv;
    bool m_first = true;

    void build(App& app) {
        m_items.clear();
        m_items.push_back({Icon::Search, theme::accent, "Buscar un valor que ves",
                           "Dinero, experiencia, nivel, munición… escribe el número que aparece en pantalla.", true});
        m_items.push_back({Icon::Range, theme::purple, "Buscar entre dos números",
                           "Si no sabes el número exacto: por ejemplo, entre 20 y 100.", true});
        m_items.push_back({Icon::Heart, theme::pink, "Buscar un valor oculto",
                           "Barras de vida o energía sin número: se busca si sube o baja.", true});
        const SessionState& st = app.session->state();
        std::string sub = "No hay ninguna búsqueda en curso.";
        if (st.cur.stage == Stage::Snapshot)
            sub = "Memoria guardada: haz que el valor cambie en el juego y elige qué pasó.";
        else if (st.cur.stage == Stage::Candidates)
            sub = "Paso " + std::to_string(st.cur.steps) + "   ·   " + formatCount(st.cur.count) +
                  (st.cur.count == 1 ? " resultado" : " resultados");
        m_items.push_back({Icon::Play, theme::green, "Continuar la búsqueda", sub, app.session->active()});
        const size_t n = app.favs->items().size();
        const size_t mults = app.activeMultipliers();
        std::string favSub = n == 0 ? "Guarda aquí lo que encuentres para cambiarlo rápido la próxima vez."
                                    : (n == 1 ? "1 valor guardado para este juego"
                                              : formatCount(n) + " valores guardados para este juego");
        if (n > 0)
            favSub += mults == 0 ? "." : (mults == 1 ? "   ·   1 multiplicador activo." : "   ·   " + formatCount(mults) + " multiplicadores activos.");
        m_items.push_back({Icon::Star, theme::gold, "Mis valores guardados", favSub, true});
        m_items.push_back({Icon::Sliders, theme::dim, "Ajustes y ayuda", "Cómo se usa, zona de búsqueda y más.", true});
    }
};

// ================================================================== Sin juego

class NoGameScreen : public Screen {
public:
    void update(App& app, const Input& in) override {
        if (in.down & BTN_PLUS) {
            app.exit();
            return;
        }
        if ((in.down & BTN_A) || (in.tap && hitRect(in.tapX, in.tapY, kScreenW / 2 - 150, 574, 300, 56))) {
            app.connect();
            return;
        }
        if (in.down & BTN_Y) app.push(makeHelpScreen());
    }

    void draw(App& app, Canvas& c) override {
        drawIcon(c, Icon::Game, kScreenW / 2 - 50, 118, 100, theme::faint);
        c.text(kScreenW / 2, 232, "No hay ningún juego abierto", 36, theme::text, Align::Center);
        c.textBlock(250, 296,
                    "1.   Abre el juego que quieres modificar.\n"
                    "2.   Pulsa el botón HOME.\n"
                    "3.   Abre el Álbum para entrar al menú homebrew y elige ValorNX.",
                    24, theme::dim, 800, 12);
        if (!app.gameError.empty()) {
            const std::vector<std::string> lines = c.wrap(app.gameError, 20, 700);
            const int h = 28 + (int)lines.size() * 28;
            const int bx = 240, by = 452;
            c.fillRoundRect(bx, by, 800, h, 14, withAlpha(theme::red, 36));
            drawIcon(c, Icon::Warning, bx + 22, by + 16, 28, theme::red);
            int y = by + 14;
            for (const std::string& l : lines) {
                c.text(bx + 70, y, l, 20, theme::text);
                y += 28;
            }
        }
        c.fillRoundRect(kScreenW / 2 - 150, 574, 300, 56, 28, theme::accent);
        const int bw = App::hintWidth(c, {"A", "Reintentar"});
        const int x = kScreenW / 2 - bw / 2;
        c.fillCircle(x + 14, 574 + 28, 14, theme::bar);
        c.text(x + 14, 574 + 16, "A", 19, theme::accent, Align::Center);
        c.text(x + 36, 574 + 14, "Reintentar", 22, theme::bar);
    }

    void hints(App&, std::vector<Hint>& out) override {
        out.push_back({"A", "Reintentar"});
        out.push_back({"Y", "Cómo se usa"});
        out.push_back({"+", "Salir"});
    }
};

// ================================================================== Teclado numérico

class KeypadScreen : public Screen {
public:
    using OkFn = std::function<void(const std::string&, int)>;
    using RangeFn = std::function<void(const std::string&, const std::string&, int)>;

    KeypadScreen(const KeypadSpec& spec, OkFn onOk, RangeFn onRange)
        : m_spec(spec), m_ok(std::move(onOk)), m_onRange(std::move(onRange)), m_type(spec.typeChoice) {
        m_entry[0] = NumberEntry(spec.initial);
        const char* rows[4][4] = {{"7", "8", "9", "<"}, {"4", "5", "6", "C"}, {"1", "2", "3", "-"}, {"0", "0", ".", "K"}};
        for (int r = 0; r < 4; r++) {
            for (int col = 0; col < 4; col++) {
                if (r == 3 && col == 1) continue;   // el 0 ocupa dos casillas
                Cell cell;
                cell.row = r;
                cell.col = col;
                cell.span = (r == 3 && col == 0) ? 2 : 1;
                cell.key = rows[r][col][0];
                m_cells.push_back(cell);
            }
        }
        if (spec.quickValues) {
            const char* quick[4] = {"99", "999", "9999", "99999"};
            for (int col = 0; col < 4; col++) {
                Cell cell;
                cell.row = 4;
                cell.col = col;
                cell.span = 1;
                cell.key = 'Q';
                cell.quick = quick[col];
                m_cells.push_back(cell);
            }
        }
        m_rows = spec.quickValues ? 5 : 4;
        m_sel = cellAt(1, 1);   // el 5
        m_row = 1;
        m_col = 1;
    }

    void update(App& app, const Input& in) override {
        if (in.down & BTN_UP) move(-1, 0);
        if (in.down & BTN_DOWN) move(1, 0);
        if (in.down & BTN_LEFT) move(0, -1);
        if (in.down & BTN_RIGHT) move(0, 1);
        if (in.down & BTN_A) {
            if (press(app, m_cells[m_sel].key, m_cells[m_sel].quick)) return;
        }
        if (in.down & BTN_B) {
            NumberEntry& e = m_entry[m_field];
            if (e.empty() || e.fresh()) {
                if (m_spec.range && m_field == 1) {
                    m_field = 0;   // volver a la primera casilla
                    m_err.clear();
                } else {
                    app.pop();
                    return;
                }
            } else {
                press(app, '<', "");
            }
        }
        if (in.down & BTN_X) press(app, 'C', "");
        if (in.down & BTN_Y) press(app, '-', "");
        if (in.down & BTN_PLUS) {
            if (submit(app)) return;
        }
        if (m_spec.range && (in.down & (BTN_ZL | BTN_ZR))) {
            m_field = (in.down & BTN_ZL) ? 0 : 1;
            m_err.clear();
        }
        if (canChangeType()) {
            const int n = typeChoiceCount();
            if (in.down & BTN_L) m_type = (m_type + n - 1) % n;
            if (in.down & BTN_R) m_type = (m_type + 1) % n;
        }
        if (in.typed) {
            const char k = in.typed;
            if ((k >= '0' && k <= '9') || k == '.' || k == '-') {
                if (press(app, k, "")) return;
            } else if (k == '\b') {
                press(app, '<', "");
            } else if (k == '\t' && m_spec.range) {
                m_field = 1 - m_field;
            } else if (k == '\n') {
                if (submit(app)) return;
            }
        }
        if (in.tap) {
            for (int i = 0; i < (int)m_cells.size(); i++) {
                int x, y, w, h;
                cellRect(m_cells[i], x, y, w, h);
                if (hitRect(in.tapX, in.tapY, x, y, w, h)) {
                    m_sel = i;
                    m_row = m_cells[i].row;
                    m_col = m_cells[i].col;
                    if (press(app, m_cells[i].key, m_cells[i].quick)) return;
                }
            }
            if (m_spec.range) {
                for (int f = 0; f < 2; f++) {
                    int x, y, w, h;
                    fieldRect(f, x, y, w, h);
                    if (hitRect(in.tapX, in.tapY, x, y, w, h)) {
                        m_field = f;
                        m_err.clear();
                    }
                }
            }
            if (canChangeType() && hitRect(in.tapX, in.tapY, kMargin, kTypeY, kDispW, 100)) {
                const int n = typeChoiceCount();
                m_type = in.tapX < kMargin + kDispW / 2 ? (m_type + n - 1) % n : (m_type + 1) % n;
            }
        }
    }

    void draw(App& app, Canvas& c) override {
        c.text(kMargin, 106, m_spec.title, 34, theme::text, Align::Left, 600);
        if (!m_spec.subtitle.empty()) c.textBlock(kMargin, 154, m_spec.subtitle, 21, theme::dim, kDispW, 4);

        // Pantallita (o las dos casillas "Desde" y "Hasta")
        const int dx = kMargin;
        for (int f = 0; f < (m_spec.range ? 2 : 1); f++) {
            int x, y, w, h;
            fieldRect(f, x, y, w, h);
            const bool active = f == m_field;
            const NumberEntry& e = m_entry[f];
            c.fillRoundRect(x, y, w, h, 16, theme::bar);
            const Color border = !active ? theme::line : (m_err.empty() ? theme::accent : theme::red);
            c.strokeRoundRect(x, y, w, h, 16, active ? 3 : 2, border);
            int size = 60, top = y + 16;
            if (m_spec.range) {
                c.text(x + 18, y + 10, f == 0 ? "Desde" : "Hasta", 19, active ? theme::accent : theme::dim);
                size = 48;
                top = y + 40;
            }
            const std::string shown = e.empty() ? "0" : e.text();
            const std::string fit = c.ellipsize(shown, size, w - 60);
            const int tw = c.measure(fit, size);
            if (e.fresh() && !e.empty())
                c.fillRoundRect(x + w - 30 - tw - 8, top + 4, tw + 16, size + 10, 8, withAlpha(theme::accent, 70));
            c.text(x + w - 30, top, fit, size, e.empty() ? theme::faint : theme::text, Align::Right);
            if (active && !e.fresh() && (app.now / 500) % 2 == 0)
                c.fillRect(x + w - 24, top + 8, 3, size, theme::accent);
        }

        const int y = kDispY + kDispH + 12;
        if (!m_err.empty()) {
            drawIcon(c, Icon::Warning, dx, y + 2, 22, theme::red);
            c.textBlock(dx + 32, y, m_err, 20, theme::red, kDispW - 32, 2);
        } else if (!m_spec.infoLine.empty()) {
            c.text(dx, y, m_spec.infoLine, 22, theme::dim, Align::Left, kDispW);
        } else if (m_spec.range) {
            c.text(dx, y, "Da igual el orden: se busca todo lo que haya entre los dos.", 20, theme::faint,
                   Align::Left, kDispW);
        }

        // Tipo de valor
        if (m_spec.search && !m_spec.lockType) {
            c.fillRoundRect(dx, kTypeY, kDispW, 100, 16, theme::card);
            c.text(dx + kDispW / 2, kTypeY + 12, "Tipo de valor", 19, theme::dim, Align::Center);
            App::buttonBadge(c, dx + 20, kTypeY + 52, "L");
            App::buttonBadge(c, dx + kDispW - 48, kTypeY + 52, "R");
            c.text(dx + kDispW / 2, kTypeY + 46, typeChoice(m_type).label, 28, theme::text, Align::Center, kDispW - 150);
            c.textBlock(dx, kTypeY + 112, typeChoice(m_type).help, 20, theme::dim, kDispW, 4);
        } else {
            const std::string label = m_spec.search ? typeMaskLabel(m_spec.lockedMask) : vtypeLabel(m_spec.editType);
            c.fillRoundRect(dx, kTypeY, kDispW, 76, 16, theme::card);
            c.text(dx + 24, kTypeY + 10, "Tipo de valor", 19, theme::dim);
            c.text(dx + 24, kTypeY + 36, label, 25, theme::text, Align::Left, kDispW - 48);
            if (m_spec.search)
                c.textBlock(dx, kTypeY + 90, "El tipo no cambia mientras continúas esta búsqueda.", 19, theme::faint,
                            kDispW, 4);
        }

        // Teclas
        for (int i = 0; i < (int)m_cells.size(); i++) {
            const Cell& cell = m_cells[i];
            int x, cy, w, h;
            cellRect(cell, x, cy, w, h);
            const bool sel = i == m_sel;
            const bool ok = cell.key == 'K';
            const bool disabled = cell.key == '.' && !decimalsAllowed();
            Color bgc = ok ? theme::accent : (cell.key == 'Q' ? theme::bar : theme::card);
            if (sel && !ok) bgc = theme::cardHi;
            c.fillRoundRect(x, cy, w, h, 14, bgc);
            if (sel) c.strokeRoundRect(x - 3, cy - 3, w + 6, h + 6, 17, 3, ok ? theme::text : theme::accent);
            const Color fg = ok ? theme::bar : (disabled ? theme::faint : theme::text);
            const int cx = x + w / 2;
            switch (cell.key) {
                case '<': drawIcon(c, Icon::Backspace, cx - 20, cy + h / 2 - 20, 40, fg); break;
                case 'C': c.text(cx, cy + h / 2 - 16, "Limpiar", 24, fg, Align::Center); break;
                case '-': c.text(cx, cy + h / 2 - 19, "+/-", 30, fg, Align::Center); break;
                case 'K':
                    if (m_spec.range && m_field == 0)
                        c.text(cx, cy + h / 2 - 15, "Siguiente", 22, fg, Align::Center);
                    else
                        c.text(cx, cy + h / 2 - 20, "OK", 32, fg, Align::Center);
                    break;
                case 'Q': c.text(cx, cy + h / 2 - 16, cell.quick, 26, theme::gold, Align::Center); break;
                default: c.text(cx, cy + h / 2 - 23, std::string(1, cell.key), 38, fg, Align::Center); break;
            }
        }
        if (m_spec.quickValues) {
            int x, cy, w, h;
            cellRect(m_cells.back(), x, cy, w, h);
            c.text(kKeysX, cy - 26, "Valores rápidos", 18, theme::dim);
        }
    }

    void hints(App&, std::vector<Hint>& out) override {
        const NumberEntry& e = m_entry[m_field];
        const bool back = e.empty() || e.fresh();
        out.push_back({"A", "Pulsar"});
        out.push_back({"B", !back ? "Borrar" : (m_spec.range && m_field == 1 ? "Desde" : "Volver")});
        out.push_back({"X", "Limpiar"});
        if (m_spec.range) out.push_back({"ZL/ZR", "Casilla"});
        if (canChangeType()) out.push_back({"L/R", "Tipo"});
        out.push_back({"+", m_spec.range && m_field == 0 ? "Siguiente" : "Aceptar"});
    }

private:
    struct Cell {
        int row = 0, col = 0, span = 1;
        char key = 0;
        std::string quick;
    };
    static constexpr int kDispW = 590;
    static constexpr int kDispY = 216, kDispH = 108;
    static constexpr int kTypeY = 392;
    static constexpr int kKeysX = 684, kKeysY = 200, kCellW = 132, kCellH = 72, kGap = 12;

    KeypadSpec m_spec;
    OkFn m_ok;
    RangeFn m_onRange;
    NumberEntry m_entry[2];
    int m_field = 0;
    std::string m_err;
    int m_type = 0;
    std::vector<Cell> m_cells;
    int m_rows = 4, m_sel = 0, m_row = 0, m_col = 0;

    void fieldRect(int f, int& x, int& y, int& w, int& h) const {
        y = kDispY;
        h = kDispH;
        if (!m_spec.range) {
            x = kMargin;
            w = kDispW;
            return;
        }
        w = (kDispW - 20) / 2;
        x = kMargin + f * (w + 20);
    }

    void cellRect(const Cell& cell, int& x, int& y, int& w, int& h) const {
        const int top = m_rows == 5 ? kKeysY : kKeysY + 16;
        const int extra = cell.row == 4 ? 34 : 0;   // hueco para el título "Valores rápidos"
        x = kKeysX + cell.col * (kCellW + kGap);
        y = top + cell.row * (kCellH + kGap) + extra;
        w = cell.span * kCellW + (cell.span - 1) * kGap;
        h = kCellH;
    }

    int cellAt(int row, int col) const {
        for (int i = 0; i < (int)m_cells.size(); i++) {
            const Cell& c = m_cells[i];
            if (c.row == row && col >= c.col && col < c.col + c.span) return i;
        }
        return 0;
    }

    void move(int dr, int dc) {
        if (dr) {
            const int nr = m_row + dr;
            if (nr < 0 || nr >= m_rows) return;
            m_row = nr;
            m_sel = cellAt(m_row, m_col);
        }
        if (dc) {
            const Cell& cur = m_cells[m_sel];
            const int nc = dc < 0 ? cur.col - 1 : cur.col + cur.span;
            if (nc < 0 || nc >= 4) return;
            m_sel = cellAt(m_row, nc);
            m_col = m_cells[m_sel].col;
        }
    }

    bool canChangeType() const { return m_spec.search && !m_spec.lockType; }

    TypeMask mask() const { return m_spec.lockType ? m_spec.lockedMask : typeChoice(m_type).mask; }

    bool decimalsAllowed() const {
        if (!m_spec.search) return vtypeIsFloat(m_spec.editType);
        return maskHasFloat(mask());
    }

    // Devuelve true si la pantalla se cerró.
    bool press(App& app, char key, const std::string& quick) {
        m_err.clear();
        if (key == 'K') return submit(app);
        if (key == 'Q')
            m_entry[m_field].set(quick);
        else
            m_err = m_entry[m_field].press(key, decimalsAllowed());
        return false;
    }

    // Comprueba un número de búsqueda: "" si vale para alguno de los tipos elegidos.
    std::string searchError(const Number& n) const {
        if (!n.valid) return "Ese número no es válido.";
        uint64_t raw = 0;
        for (int i = 0; i < kVTypeCount; i++)
            if ((mask() & typeBit((VType)i)) && encodeNumber((VType)i, n, raw)) return "";
        return n.isDecimal ? "Este tipo no admite decimales: elige \"Decimal\" o \"Automático\"."
                           : "El número es demasiado grande para este tipo de valor.";
    }

    bool submit(App& app) {
        if (m_spec.range) return submitRange(app);
        const std::string text = m_entry[0].text();
        if (text.empty() || text == "-") {
            m_err = "Escribe un número.";
            return false;
        }
        const Number n = parseNumber(text);
        m_err = m_spec.search ? searchError(n) : numberErrorForType(m_spec.editType, n);
        if (!m_err.empty()) return false;
        OkFn cb = m_ok;
        const int tc = m_type;
        app.pop();
        if (cb) cb(text, tc);
        return true;
    }

    bool submitRange(App& app) {
        std::string lo = m_entry[0].text(), hi = m_entry[1].text();
        const bool emptyLo = lo.empty() || lo == "-", emptyHi = hi.empty() || hi == "-";
        if (m_field == 0 && !emptyLo && emptyHi) {
            m_field = 1;   // primero "Desde", después "Hasta"
            return false;
        }
        if (emptyLo || emptyHi) {
            m_field = emptyLo ? 0 : 1;
            m_err = "Escribe los dos números.";
            return false;
        }
        Number a = parseNumber(lo), b = parseNumber(hi);
        for (int f = 0; f < 2; f++) {
            const std::string e = searchError(f == 0 ? a : b);
            if (!e.empty()) {
                m_field = f;
                m_err = e;
                return false;
            }
        }
        const bool swapped = (!a.isDecimal && !b.isDecimal) ? b.i < a.i : b.d < a.d;
        if (swapped) {
            std::swap(lo, hi);
            std::swap(a, b);
        }
        if (m_spec.rejectZero && a.d <= 0.0 && b.d >= 0.0) {
            m_err = "El rango no puede incluir el 0: aparece muchísimas veces en la memoria. Empieza en 1.";
            return false;
        }
        RangeFn cb = m_onRange;
        const int tc = m_type;
        app.pop();
        if (cb) cb(lo, hi, tc);
        return true;
    }
};

// ================================================================== Búsqueda en curso

class SearchScreen : public Screen {
public:
    SearchScreen() { m_lv = makeList(600, 112, 632, 520, 58, 8); }

    void update(App& app, const Input& in) override {
        if (!app.session->active()) {
            app.pop();
            return;
        }
        build(app);
        if (in.down & BTN_B) {
            app.pop();
            return;
        }
        const int act = m_lv.update(in, (int)m_opts.size());
        if (act >= 0) run(app, m_opts[act].action);
    }

    void draw(App& app, Canvas& c) override {
        if (!app.session->active()) return;
        build(app);
        const SessionState& st = app.session->state();
        const int x = kMargin, w = 520;
        c.text(x, 104,
               st.hidden ? "BÚSQUEDA DE VALOR OCULTO" : (st.range ? "BÚSQUEDA ENTRE DOS NÚMEROS" : "BÚSQUEDA EN CURSO"),
               18, theme::dim);
        c.text(x, 130, "Tipo: " + typeMaskLabel(st.types) + "   ·   Paso " + std::to_string(st.cur.steps), 21,
               theme::dim, Align::Left, w);
        if (st.cur.stage == Stage::Snapshot) {
            c.text(x, 166, formatBytes(st.cur.count), 64, theme::accent);
            c.text(x, 246, "de memoria guardada", 24, theme::dim);
        } else {
            const uint64_t n = st.cur.count;
            c.text(x, 160, formatCount(n), 76, n == 0 ? theme::red : theme::accent, Align::Left, w);
            c.text(x, 250, n == 1 ? "resultado" : "resultados", 24, theme::dim);
        }
        if (!st.cur.lastInput.empty())
            c.text(x, 286, "Último paso: " + st.cur.lastInput, 20, theme::faint, Align::Left, w);

        const int gy = 322, gh = 310;
        c.fillRoundRect(x, gy, w, gh, 16, theme::card);
        drawIcon(c, Icon::Info, x + 20, gy + 20, 28, theme::accent);
        c.textBlock(x + 62, gy + 18, guidance(st), 21, theme::text, w - 84, 6);
        c.fillRect(x + 20, gy + gh - 70, w - 40, 1, theme::line);
        c.textBlock(x + 20, gy + gh - 58, "Consejo: pulsa HOME para ir al juego. La búsqueda se guarda sola.", 19,
                    theme::dim, w - 40, 2);

        listClip(c, m_lv);
        for (int i = 0; i < (int)m_opts.size(); i++) {
            if (!m_lv.rowVisible(i)) continue;
            const Opt& o = m_opts[i];
            const int y = m_lv.rowY(i);
            cardRow(c, m_lv.x, y, m_lv.w, m_lv.rowH, i == m_lv.sel);
            iconBadge(c, m_lv.x + 12, y + 9, 40, o.icon, o.color);
            c.text(m_lv.x + 68, y + 13, o.label, 25, theme::text, Align::Left, m_lv.w - 90);
        }
        c.popClip();
        m_lv.drawScrollbar(c, (int)m_opts.size());
    }

    void hints(App&, std::vector<Hint>& out) override {
        out.push_back({"A", "Elegir"});
        out.push_back({"B", "Volver"});
    }

private:
    enum Action { A_VIEW, A_EQUAL, A_RANGE, A_INC, A_DEC, A_CHANGED, A_UNCHANGED, A_UNDO, A_RESET };
    struct Opt {
        Icon icon;
        Color color;
        std::string label;
        Action action;
    };
    std::vector<Opt> m_opts;
    ListView m_lv;
    uint32_t m_seenGen = 0xFFFFFFFFu;
    Stage m_seenStage = Stage::Empty;

    void build(App& app) {
        const SessionState& st = app.session->state();
        const uint64_t n = st.cur.count;
        m_opts.clear();
        // Si se empezó sin saber el número (oculto o entre dos números), lo normal es seguir
        // con "aumentó / disminuyó"; si se sabía, con el número nuevo.
        const bool unknown = st.hidden || st.range;
        const Opt equal{Icon::Search, theme::accent,
                        unknown ? "Ahora veo el número: escribirlo" : "Buscar el nuevo valor", A_EQUAL};
        const Opt between{Icon::Range, theme::purple, "Está entre dos números", A_RANGE};
        if (st.cur.stage == Stage::Snapshot) {
            m_opts.push_back({Icon::Down, theme::pink, "Disminuyó", A_DEC});
            m_opts.push_back({Icon::Up, theme::green, "Aumentó", A_INC});
            m_opts.push_back({Icon::NotEqual, theme::purple, "Cambió (no sé si subió o bajó)", A_CHANGED});
            m_opts.push_back(equal);
            m_opts.push_back(between);
        } else if (n > 0) {
            const bool few = n <= kListMax;
            if (few) m_opts.push_back({Icon::List, theme::gold, "Ver resultados (" + formatCount(n) + ")", A_VIEW});
            if (!unknown) m_opts.push_back(equal);
            m_opts.push_back({Icon::Down, theme::pink, "Disminuyó", A_DEC});
            m_opts.push_back({Icon::Up, theme::green, "Aumentó", A_INC});
            m_opts.push_back({Icon::NotEqual, theme::purple, "Cambió", A_CHANGED});
            m_opts.push_back({Icon::Equal, theme::dim, "No cambió", A_UNCHANGED});
            m_opts.push_back(between);
            if (unknown) m_opts.push_back(equal);
            if (!few) m_opts.push_back({Icon::List, theme::gold, "Ver los primeros 100 resultados", A_VIEW});
        }
        if (app.session->canUndo()) m_opts.push_back({Icon::Undo, theme::dim, "Deshacer el último paso", A_UNDO});
        m_opts.push_back({Icon::Close, theme::red, "Empezar de nuevo", A_RESET});
        if (st.cur.gen != m_seenGen || st.cur.stage != m_seenStage) {
            m_seenGen = st.cur.gen;
            m_seenStage = st.cur.stage;
            m_lv.sel = 0;
            m_lv.scroll = 0;
        }
    }

    static std::string guidance(const SessionState& st) {
        const uint64_t n = st.cur.count;
        if (st.cur.stage == Stage::Snapshot)
            return "Ahora vuelve al juego (botón HOME) y haz que el valor cambie: recibe daño, gasta energía… "
                   "Después vuelve aquí y elige qué le pasó al valor.";
        if (n == 0)
            return "No quedó ningún resultado. Puede que el valor sea de otro tipo o que el número no fuera "
                   "exacto. Deshaz el último paso o empieza de nuevo.";
        if (n == 1) return "¡Lo encontraste! Entra en \"Ver resultados\" para cambiarlo, congelarlo o guardarlo.";
        if (n <= kListMax)
            return "Quedan pocos resultados. Míralos: si hay varios, cambia uno y comprueba en el juego cuál es "
                   "el bueno (o cámbialos todos a la vez).";
        if (st.hidden)
            return "Vuelve al juego, haz que el valor cambie otra vez (o no lo toques) y elige qué pasó para "
                   "descartar resultados.";
        if (st.range)
            return "Todavía hay muchos resultados. Vuelve al juego, haz que el valor cambie (recibe daño, gasta, "
                   "gana…) y elige si aumentó o disminuyó. También puedes escribir un rango más pequeño.";
        return "Todavía hay muchos resultados. Vuelve al juego, haz que el número cambie (gasta, gana, recibe "
               "daño…) y busca el nuevo valor.";
    }

    void run(App& app, Action a) {
        App* ap = &app;
        switch (a) {
            case A_VIEW: app.push(makeResultsScreen()); break;
            case A_EQUAL: {
                KeypadSpec k;
                k.title = "¿Qué número ves ahora?";
                k.subtitle = "Escribe el valor que muestra el juego en este momento.";
                k.search = true;
                k.lockType = true;
                k.lockedMask = app.session->state().types;
                app.keypad(k, [ap](const std::string& text, int) {
                    const Number n = parseNumber(text);
                    ap->runTask(ap->session->refine(*ap->game, Relation::Equal, &n, text), refineDone(*ap));
                });
                break;
            }
            case A_RANGE: {
                KeypadSpec k;
                k.title = "¿Entre qué números está ahora?";
                k.subtitle = "Escribe un mínimo y un máximo para el valor que tiene ahora en el juego.";
                k.search = true;
                k.lockType = true;
                k.lockedMask = app.session->state().types;
                app.rangeKeypad(k, [ap](const std::string& lo, const std::string& hi, int) {
                    const Number a = parseNumber(lo), b = parseNumber(hi);
                    ap->runTask(ap->session->refine(*ap->game, Relation::Between, &a, "entre " + lo + " y " + hi, &b),
                                refineDone(*ap));
                });
                break;
            }
            case A_INC:
            case A_DEC:
            case A_CHANGED:
            case A_UNCHANGED: {
                const Relation rel = a == A_INC       ? Relation::Increased
                                     : a == A_DEC     ? Relation::Decreased
                                     : a == A_CHANGED ? Relation::Changed
                                                      : Relation::Unchanged;
                app.runTask(app.session->refine(*app.game, rel, nullptr, relationLabel(rel)), refineDone(app));
                break;
            }
            case A_UNDO:
                if (app.session->undo()) app.toast("Se deshizo el último paso.");
                break;
            case A_RESET:
                app.confirm("¿Empezar de nuevo?", "Se borrará esta búsqueda.", "Sí, borrarla", [ap]() {
                    ap->session->reset();
                    ap->pop();
                    ap->toast("Búsqueda borrada.");
                });
                break;
        }
    }
};

// ================================================================== Resultados

class ResultsScreen : public Screen {
public:
    ResultsScreen() { m_lv = makeList(kMargin, 176, 1184, 458, 52, 6); }

    void update(App& app, const Input& in) override {
        const SessionState& st = app.session->state();
        if (st.cur.stage != Stage::Candidates) {
            app.pop();
            return;
        }
        if (st.cur.gen != m_gen) load(app);
        if (in.down & BTN_B) {
            app.pop();
            return;
        }
        const int n = (int)m_c.size();
        const int act = m_lv.update(in, n);
        if (n > 0) {
            if (act >= 0) {
                options(app, act);
            } else if (in.down & BTN_Y) {
                const Candidate& cd = m_c[m_lv.sel];
                app.toggleFreeze(candAddr(cd), candType(cd));
            } else if (in.down & BTN_X) {
                editAll(app);
            }
        }
        refresh(app);
    }

    void draw(App& app, Canvas& c) override {
        const uint64_t total = app.session->state().cur.count;
        c.text(kMargin, 104, "Resultados", 30, theme::text);
        const std::string right = total > m_c.size()
                                      ? "Mostrando " + formatCount(m_c.size()) + " de " + formatCount(total)
                                      : formatCount(total) + (total == 1 ? " resultado" : " resultados");
        c.text(kScreenW - kMargin, 112, right, 20, theme::dim, Align::Right);
        const int x = m_lv.x, w = m_lv.w;
        c.text(x + 18, 148, "#", 18, theme::faint);
        c.text(x + 80, 148, "Dirección", 18, theme::faint);
        c.text(x + 450, 148, "Tipo", 18, theme::faint);
        c.text(x + w - 250, 148, "Valor actual", 18, theme::faint, Align::Right);
        c.text(x + w - 24, 148, "Antes", 18, theme::faint, Align::Right);
        if (m_c.empty()) {
            c.text(kScreenW / 2, 330, "No hay resultados que mostrar.", 26, theme::dim, Align::Center);
            return;
        }
        listClip(c, m_lv);
        for (int i = 0; i < (int)m_c.size(); i++) {
            if (!m_lv.rowVisible(i)) continue;
            const Candidate& cd = m_c[i];
            const uint64_t addr = candAddr(cd);
            const VType t = candType(cd);
            const int y = m_lv.rowY(i);
            cardRow(c, x, y, w, m_lv.rowH, i == m_lv.sel, 12);
            c.text(x + 18, y + 13, std::to_string(i + 1), 20, theme::faint);
            c.text(x + 80, y + 11, app.game->info().describe(addr), 24, theme::text, Align::Left, 360);
            c.text(x + 450, y + 13, vtypeShort(t), 20, theme::dim);
            if (app.isFrozen(addr)) {
                drawIcon(c, Icon::Snowflake, x + 590, y + 14, 24, theme::ice);
                c.text(x + 622, y + 15, "Congelado", 18, theme::ice);
            }
            c.text(x + w - 250, y + 8, m_ok[i] ? formatRaw(t, m_live[i]) : "?", 28, theme::gold, Align::Right, 230);
            c.text(x + w - 24, y + 13, formatRaw(t, cd.value), 20, theme::dim, Align::Right, 200);
        }
        c.popClip();
        m_lv.drawScrollbar(c, (int)m_c.size());
    }

    void hints(App&, std::vector<Hint>& out) override {
        out.push_back({"A", "Opciones"});
        out.push_back({"Y", "Congelar"});
        out.push_back({"X", "Cambiar todos"});
        out.push_back({"B", "Volver"});
    }

private:
    std::vector<Candidate> m_c;
    std::vector<uint64_t> m_live;
    std::vector<uint8_t> m_ok;
    uint32_t m_gen = 0xFFFFFFFFu;
    uint64_t m_lastRead = 0;
    ListView m_lv;

    void load(App& app) {
        app.session->loadCandidates(m_c, kListMax);
        m_live.assign(m_c.size(), 0);
        m_ok.assign(m_c.size(), 0);
        m_gen = app.session->state().cur.gen;
        m_lastRead = 0;
        m_lv.clampScroll((int)m_c.size());
        refresh(app);
    }

    void refresh(App& app) {
        if (m_lastRead != 0 && app.now - m_lastRead < 300) return;
        m_lastRead = app.now ? app.now : 1;
        for (int i = 0; i < (int)m_c.size(); i++) {
            if (!m_lv.rowVisible(i)) continue;
            uint64_t raw = 0;
            m_ok[i] = app.readValue(candAddr(m_c[i]), candType(m_c[i]), raw) ? 1 : 0;
            m_live[i] = raw;
        }
    }

    void options(App& app, int i) {
        const Candidate cd = m_c[i];
        const uint64_t addr = candAddr(cd);
        const VType t = candType(cd);
        const bool frozen = app.isFrozen(addr);
        const std::string title = app.game->info().describe(addr);
        const std::string sub =
            std::string(vtypeLabel(t)) + "   ·   Valor actual: " + (m_ok[i] ? formatRaw(t, m_live[i]) : "?");
        enum Act { EDIT, FREEZE, SAVE, MULTIPLY, REMOVE };
        std::vector<MenuItem> items = {
            menuItem(Icon::Pencil, theme::accent, "Cambiar valor"),
            menuItem(Icon::Snowflake, theme::ice, frozen ? "Descongelar" : "Congelar",
                     frozen ? "El juego podrá volver a cambiarlo." : "El juego no podrá cambiarlo (vida infinita…)."),
            menuItem(Icon::Star, theme::gold, "Guardar en Mis valores", "Para cambiarlo rápido la próxima vez.")};
        std::vector<Act> acts = {EDIT, FREEZE, SAVE};
        if (multiplierSupported(t)) {
            items.push_back(menuItem(Icon::Up, theme::gold, "Multiplicar lo que ganes (x2, x3…)",
                                     "Se guarda en Mis valores y cada subida se multiplica."));
            acts.push_back(MULTIPLY);
        }
        items.push_back(menuItem(Icon::Trash, theme::red, "Quitar de la lista"));
        acts.push_back(REMOVE);
        App* ap = &app;
        ResultsScreen* self = this;
        app.menu(title, sub, items, [ap, self, addr, t, cd, acts](int k) {
            if (k < 0 || k >= (int)acts.size()) return;
            switch (acts[k]) {
                case EDIT: ap->editValue("Cambiar valor", addr, t, [self](uint64_t) { self->m_lastRead = 0; }); break;
                case FREEZE: ap->toggleFreeze(addr, t); break;
                case SAVE: ap->saveFavorite(addr, t); break;
                case MULTIPLY: ap->saveFavorite(addr, t, [ap](const Favorite& f) { ap->openMultiplier(f); }); break;
                case REMOVE:
                    if (ap->session->removeCandidates({cd}))
                        ap->toast("Quitado de la lista.");
                    else
                        ap->toast("No se pudo quitar.", true);
                    break;
            }
        });
    }

    void editAll(App& app) {
        bool anyFloat = false, allFloat = true;
        for (const Candidate& cd : m_c) {
            const bool f = vtypeIsFloat(candType(cd));
            anyFloat = anyFloat || f;
            allFloat = allFloat && f;
        }
        KeypadSpec k;
        k.search = false;
        k.editType = anyFloat ? VType::F32 : candType(m_c[0]);
        if (allFloat) k.editType = candType(m_c[0]);
        k.title = m_c.size() == 1 ? "Cambiar el resultado" : "Cambiar los " + std::to_string(m_c.size()) + " resultados";
        k.subtitle = "Se escribirá el mismo número en todos. Útil cuando no sabes cuál es el bueno.";
        k.quickValues = true;
        const std::vector<Candidate> list = m_c;
        App* ap = &app;
        ResultsScreen* self = this;
        app.keypad(k, [ap, self, list](const std::string& text, int) {
            const Number n = parseNumber(text);
            int ok = 0, fail = 0;
            for (const Candidate& cd : list) {
                uint64_t raw = 0;
                if (encodeNumber(candType(cd), n, raw) && ap->setValue(candAddr(cd), candType(cd), raw))
                    ok++;
                else
                    fail++;
            }
            self->m_lastRead = 0;
            if (fail == 0)
                ap->toast(ok == 1 ? "Listo: se cambió el valor." : "Listo: se cambiaron " + std::to_string(ok) + " valores.");
            else
                ap->toast("Se cambiaron " + std::to_string(ok) + " de " + std::to_string(ok + fail) + " valores.",
                          ok == 0);
        });
    }
};

// ================================================================== Mis valores

class FavoritesScreen : public Screen {
public:
    FavoritesScreen() { m_lv = makeList(kMargin, 156, 1184, 478, 78, 8); }

    void update(App& app, const Input& in) override {
        std::vector<Favorite>& items = app.favs->items();
        const int n = (int)items.size();
        if ((int)m_live.size() != n) {
            m_live.assign(n, 0);
            m_ok.assign(n, 0);
            m_lastRead = 0;
        }
        if (in.down & BTN_B) {
            app.pop();
            return;
        }
        const int act = m_lv.update(in, n);
        if (n > 0) {
            const int i = m_lv.sel;
            if (act >= 0) {
                edit(app, act);
            } else if (in.down & BTN_Y) {
                app.toggleFreeze(FavoriteStore::address(items[i], app.game->info()), items[i].type);
            } else if (in.down & BTN_X) {
                more(app, i);
            }
        }
        refresh(app);
    }

    void draw(App& app, Canvas& c) override {
        const std::vector<Favorite>& items = app.favs->items();
        c.text(kMargin, 104, "Mis valores guardados", 30, theme::text);
        if (items.empty()) {
            drawIcon(c, Icon::Star, kScreenW / 2 - 45, 200, 90, theme::faint);
            c.text(kScreenW / 2, 320, "Todavía no has guardado ningún valor", 28, theme::text, Align::Center);
            c.textBlock(290, 372,
                        "Cuando encuentres uno con la búsqueda, ábrelo en \"Ver resultados\" y elige \"Guardar en "
                        "Mis valores\". Así lo tendrás aquí para cambiarlo en un momento.",
                        22, theme::dim, 700, 6);
            return;
        }
        c.text(kScreenW - kMargin, 112, items.size() == 1 ? "1 valor" : formatCount(items.size()) + " valores", 20,
               theme::dim, Align::Right);
        const ProcessInfo& pi = app.game->info();
        const std::string build = pi.buildIdHex();
        listClip(c, m_lv);
        for (int i = 0; i < (int)items.size(); i++) {
            if (!m_lv.rowVisible(i)) continue;
            const Favorite& f = items[i];
            const uint64_t addr = FavoriteStore::address(f, pi);
            const int y = m_lv.rowY(i);
            const int x = m_lv.x, w = m_lv.w;
            cardRow(c, x, y, w, m_lv.rowH, i == m_lv.sel);
            c.text(x + 24, y + 9, f.name, 28, theme::text, Align::Left, 640);
            std::string sub = std::string(vtypeShort(f.type)) + "   ·   " + pi.describe(addr);
            c.text(x + 24, y + 47, sub, 19, theme::dim, Align::Left, 560);
            if (!f.buildId.empty() && f.buildId != build) {
                const int sw = c.measure(sub, 19);
                c.text(x + 24 + sw + 16, y + 47, "Guardado en otra versión del juego", 19, theme::red);
            }
            const std::string val = i < (int)m_ok.size() && m_ok[i] ? formatRaw(f.type, m_live[i]) : "?";
            const int vw = c.text(x + w - 26, y + 17, val, 36, theme::gold, Align::Right, 300);
            int badgeRight = x + w - 26 - vw - 18;
            if (app.isFrozen(addr)) {
                drawIcon(c, Icon::Snowflake, badgeRight - 28, y + 23, 28, theme::ice);
                badgeRight -= 28 + 14;
            }
            if (const ActiveMultiplier* m = app.multiplierFor(f)) {
                // Etiqueta "x2" (dorada si está activo, gris si está en pausa)
                const std::string label = "x" + std::to_string(m->spec.factor) + (m->enabled ? "" : " en pausa");
                const int lw = c.measure(label, 21) + 24;
                const Color col = m->enabled ? theme::gold : theme::faint;
                c.fillRoundRect(badgeRight - lw, y + 21, lw, 34, 17, withAlpha(col, 48));
                c.text(badgeRight - lw / 2, y + 24, label, 21, col, Align::Center);
            }
        }
        c.popClip();
        m_lv.drawScrollbar(c, (int)items.size());
    }

    void hints(App& app, std::vector<Hint>& out) override {
        if (!app.favs->items().empty()) {
            out.push_back({"A", "Cambiar valor"});
            out.push_back({"Y", "Congelar"});
            out.push_back({"X", "Más opciones"});
        }
        out.push_back({"B", "Volver"});
    }

private:
    ListView m_lv;
    std::vector<uint64_t> m_live;
    std::vector<uint8_t> m_ok;
    uint64_t m_lastRead = 0;

    struct Key {
        RegionKind region;
        uint64_t offset;
        VType type;
    };
    static Key keyOf(const Favorite& f) { return Key{f.region, f.offset, f.type}; }
    static int indexOf(App& app, const Key& k) { return app.favs->find(k.region, k.offset, k.type); }

    void refresh(App& app) {
        if (m_lastRead != 0 && app.now - m_lastRead < 300) return;
        m_lastRead = app.now ? app.now : 1;
        const std::vector<Favorite>& items = app.favs->items();
        for (int i = 0; i < (int)items.size() && i < (int)m_live.size(); i++) {
            if (!m_lv.rowVisible(i)) continue;
            uint64_t raw = 0;
            m_ok[i] = app.readValue(FavoriteStore::address(items[i], app.game->info()), items[i].type, raw) ? 1 : 0;
            m_live[i] = raw;
        }
    }

    void edit(App& app, int i) {
        const Favorite f = app.favs->items()[i];
        const Key key = keyOf(f);
        App* ap = &app;
        FavoritesScreen* self = this;
        app.editValue("Cambiar: " + f.name, FavoriteStore::address(f, app.game->info()), f.type,
                      [ap, self, key](uint64_t raw) {
                          ap->favs->sync();   // por si se guardó algo desde el overlay
                          const int idx = indexOf(*ap, key);
                          if (idx >= 0) {
                              ap->favs->items()[idx].hasValue = true;
                              ap->favs->items()[idx].value = raw;
                              ap->favs->save();
                          }
                          self->m_lastRead = 0;
                      });
    }

    void more(App& app, int i) {
        const Favorite f = app.favs->items()[i];
        const Key key = keyOf(f);
        const uint64_t addr = FavoriteStore::address(f, app.game->info());
        App* ap = &app;
        enum Act { MULTIPLY, RENAME, CHEAT, ERASE };
        std::vector<MenuItem> items;
        std::vector<Act> acts;
        if (multiplierSupported(f.type)) {
            const ActiveMultiplier* m = app.multiplierFor(f);
            if (m)
                items.push_back(menuItem(Icon::Up, theme::gold,
                                         "Multiplicador x" + std::to_string(m->spec.factor) +
                                             (m->enabled ? " (activo)" : " (en pausa)"),
                                         "Cambiarlo, pausarlo o quitarlo."));
            else
                items.push_back(menuItem(Icon::Up, theme::gold, "Multiplicar lo que ganes (x2, x3…)",
                                         "Cada vez que suba, la subida se multiplica."));
            acts.push_back(MULTIPLY);
        }
        items.push_back(menuItem(Icon::Pencil, theme::accent, "Cambiar el nombre"));
        acts.push_back(RENAME);
        items.push_back(menuItem(Icon::Bolt, theme::gold, "Crear truco de Atmosphère",
                                 "Para activarlo con EdiZon/Breeze o al abrir el juego."));
        acts.push_back(CHEAT);
        items.push_back(menuItem(Icon::Trash, theme::red, "Borrar de Mis valores"));
        acts.push_back(ERASE);
        app.menu(f.name, std::string(vtypeLabel(f.type)) + "   ·   " + app.game->info().describe(addr), items,
                 [ap, key, f, addr, acts](int idx) {
                     if (idx < 0 || idx >= (int)acts.size()) return;
                     const Act k = acts[idx];
                     if (k == MULTIPLY) {
                         ap->openMultiplier(f);
                     } else if (k == RENAME) {
                         ap->pickName("Nuevo nombre para \"" + f.name + "\"", [ap, key](const std::string& name) {
                             ap->favs->sync();
                             const int idx = indexOf(*ap, key);
                             if (idx < 0) return;
                             if (ap->favs->items()[idx].name == name) return;
                             ap->favs->items()[idx].name = ap->favs->uniqueName(name);
                             ap->favs->save();
                             ap->toast("Nombre cambiado.");
                         });
                     } else if (k == CHEAT) {
                         createCheat(*ap, f, addr);
                     } else if (k == ERASE) {
                         const bool hasMult = ap->multiplierFor(f) != nullptr;
                         ap->confirm("¿Borrar \"" + f.name + "\"?",
                                     hasMult ? "Se quita de esta lista y se quita también su multiplicador."
                                             : "Solo se quita de esta lista; el juego no cambia.",
                                     "Sí, borrar", [ap, key, addr, f]() {
                                         ap->favs->sync();
                                         const int i = indexOf(*ap, key);
                                         if (i < 0) return;
                                         if (ap->isFrozen(addr)) ap->toggleFreeze(addr, key.type);
                                         if (ap->multiplierFor(f)) ap->removeMultiplier(f, true);
                                         ap->favs->items().erase(ap->favs->items().begin() + i);
                                         ap->favs->save();
                                         ap->toast("Borrado de Mis valores.");
                                     });
                     }
                 });
    }

    static void createCheat(App& app, const Favorite& f, uint64_t addr) {
        uint64_t cur = 0;
        const bool ok = app.readValue(addr, f.type, cur);
        KeypadSpec k;
        k.search = false;
        k.editType = f.type;
        k.title = "Valor fijo del truco";
        k.subtitle = "Mientras el truco esté activado, \"" + f.name + "\" tendrá siempre este valor.";
        const uint64_t initial = f.hasValue ? f.value : cur;
        if (f.hasValue || ok) k.initial = formatRaw(f.type, initial);
        if (k.initial.find_first_not_of("-0123456789.") != std::string::npos) k.initial.clear();
        k.infoLine = ok ? "Valor actual: " + formatRaw(f.type, cur) : "";
        k.quickValues = true;
        App* ap = &app;
        app.keypad(k, [ap, f](const std::string& text, int) {
            uint64_t raw = 0;
            if (!encodeNumber(f.type, parseNumber(text), raw)) return;
            const std::vector<uint32_t> ops = cheatStoreStatic(f.region, f.offset, f.type, raw);
            const std::string name = cheatSafeName(f.name + " = " + formatRaw(f.type, raw));
            std::string path;
            if (!writeCheat(ap->platform.sdRoot(), ap->game->info(), name, ops, path)) {
                ap->message("No se pudo crear el truco", "No se pudo escribir el archivo en la microSD:\n" + path,
                            Icon::Warning);
                return;
            }
            ap->game->addCheat(name, ops, false);
            ap->message("Truco creado",
                        "Se guardó el truco \"" + name + "\" en:\n" + path +
                            "\n\nYa aparece en EdiZon o Breeze (desactivado) para que lo actives cuando quieras. "
                            "También se cargará cada vez que abras el juego.\n\nSi tras reiniciar el juego no "
                            "funciona, el valor cambió de sitio: búscalo otra vez.",
                        Icon::Bolt);
        });
    }
};

// ================================================================== Ajustes

class SettingsScreen : public Screen {
public:
    SettingsScreen() { m_lv = makeList(kMargin, 156, 1184, 478, 84, 12); }

    void update(App& app, const Input& in) override {
        if (in.down & BTN_B) {
            app.pop();
            return;
        }
        const int act = m_lv.update(in, 4, true);
        App* ap = &app;
        switch (act) {
            case 0:
                app.cfg.zone = (SearchZone)(((int)app.cfg.zone + 1) % 3);
                app.saveConfig();
                app.toast(std::string("Zona de búsqueda: ") + searchZoneLabel(app.cfg.zone));
                break;
            case 1: app.push(makeHelpScreen()); break;
            case 2:
                if (!app.session->active()) {
                    app.toast("No hay ninguna búsqueda en curso.");
                    break;
                }
                app.confirm("¿Borrar la búsqueda en curso?", "Se borrarán sus resultados de la microSD.", "Sí, borrarla",
                            [ap]() {
                                ap->session->reset();
                                ap->toast("Búsqueda borrada.");
                            });
                break;
            case 3:
                app.message(std::string(kAppName) + " " + kAppVersion,
                            "Editor de valores para juegos de Nintendo Switch con Atmosphère.\n\n"
                            "Lee y escribe la memoria del juego abierto a través del servicio de trucos de "
                            "Atmosphère (dmnt:cht), el mismo que usan EdiZon y Breeze.\n\n"
                            "Úsalo solo sin conexión y haz copia de seguridad de tus partidas antes de cambiar "
                            "valores.",
                            Icon::Info);
                break;
            default: break;
        }
    }

    void draw(App& app, Canvas& c) override {
        c.text(kMargin, 104, "Ajustes y ayuda", 30, theme::text);
        const SessionState& st = app.session->state();
        std::string search = "No hay ninguna búsqueda en curso.";
        if (st.cur.stage == Stage::Snapshot)
            search = "Hay una memoria guardada de " + formatBytes(st.cur.count) + ".";
        else if (st.cur.stage == Stage::Candidates)
            search = "Paso " + std::to_string(st.cur.steps) + " con " + formatCount(st.cur.count) +
                     (st.cur.count == 1 ? " resultado." : " resultados.");
        struct Row {
            Icon icon;
            Color color;
            std::string title, sub;
        };
        const Row rows[4] = {
            {Icon::Search, theme::accent, "Zona de búsqueda",
             std::string(searchZoneLabel(app.cfg.zone)) + "   (pulsa A para cambiar)"},
            {Icon::Info, theme::green, "Cómo se usa", "Guía rápida paso a paso."},
            {Icon::Trash, theme::red, "Borrar la búsqueda en curso", search},
            {Icon::Game, theme::dim, std::string("Acerca de ") + kAppName, std::string("Versión ") + kAppVersion},
        };
        listClip(c, m_lv);
        for (int i = 0; i < 4; i++) {
            const int y = m_lv.rowY(i);
            cardRow(c, m_lv.x, y, m_lv.w, m_lv.rowH, i == m_lv.sel);
            iconBadge(c, m_lv.x + 16, y + 14, 56, rows[i].icon, rows[i].color);
            c.text(m_lv.x + 94, y + 12, rows[i].title, 27, theme::text);
            c.text(m_lv.x + 94, y + 49, rows[i].sub, 20, theme::dim, Align::Left, m_lv.w - 120);
        }
        c.popClip();
    }

    void hints(App&, std::vector<Hint>& out) override {
        out.push_back({"A", "Elegir"});
        out.push_back({"B", "Volver"});
    }

private:
    ListView m_lv;
};

// ================================================================== Ayuda

class HelpScreen : public Screen {
public:
    void update(App& app, const Input& in) override {
        if (in.down & (BTN_B | BTN_A)) {
            app.pop();
            return;
        }
        if (in.down & BTN_DOWN) m_scroll += 60;
        if (in.down & BTN_UP) m_scroll -= 60;
        if (in.down & BTN_R) m_scroll += 400;
        if (in.down & BTN_L) m_scroll -= 400;
        if (in.touching && in.dragY) m_scroll -= in.dragY;
        m_scroll = std::max(0, std::min(m_scroll, std::max(0, m_contentH - kViewH)));
    }

    void draw(App& app, Canvas& c) override {
        struct Sec {
            const char* title;
            const char* body;
        };
        static const Sec kSecs[] = {
            {"Antes de empezar",
             "• Haz una copia de tu partida (por ejemplo con JKSV) antes de cambiar valores.\n"
             "• No lo uses en juegos en línea: te pueden banear.\n"
             "• ValorNX necesita Atmosphère y se abre desde el Álbum mientras el juego está abierto."},
            {"Buscar un valor que ves (dinero, EXP, nivel…)",
             "1. En el juego, fíjate en el número. Por ejemplo: tienes 1500 monedas.\n"
             "2. Pulsa HOME, abre el Álbum y lanza ValorNX.\n"
             "3. Elige \"Buscar un valor que ves\" y escribe 1500.\n"
             "4. Vuelve al juego (HOME) y haz que el número cambie: compra algo, gana experiencia…\n"
             "5. Abre ValorNX otra vez, elige \"Continuar la búsqueda\" y después \"Buscar el nuevo valor\", y "
             "escribe el número nuevo.\n"
             "6. Repite hasta que queden pocos resultados. Ábrelos y cámbialos."},
            {"Buscar entre dos números (si no ves el número exacto)",
             "1. Elige \"Buscar entre dos números\" y escribe un mínimo y un máximo. Por ejemplo, si la vida está "
             "entre 20 y 100.\n"
             "2. Vuelve al juego y haz que el valor cambie.\n"
             "3. En \"Continuar la búsqueda\" elige \"Aumentó\" o \"Disminuyó\", o escribe un rango más pequeño "
             "con \"Está entre dos números\".\n"
             "• El rango no puede incluir el 0 (en la memoria hay muchísimos ceros).\n"
             "• Cuanto más pequeño sea el rango, menos resultados. Si sabes si es entero o decimal, elige ese tipo "
             "en vez de Automático."},
            {"Buscar un valor oculto (barras de vida o energía)",
             "1. Elige \"Buscar un valor oculto\": se guarda cómo está la memoria ahora.\n"
             "2. Vuelve al juego y haz que la barra cambie (por ejemplo, recibe daño).\n"
             "3. En ValorNX elige \"Disminuyó\" (o \"Aumentó\").\n"
             "4. Si la barra no cambia, elige \"No cambió\". Repite hasta tener pocos resultados."},
            {"Cambiar, congelar y guardar",
             "• Cambiar valor: pon el número que quieras.\n"
             "• Congelar: el juego ya no puede cambiar ese valor (vida o munición infinitas). Sigue congelado "
             "aunque cierres ValorNX, hasta que cierres el juego.\n"
             "• Guardar en Mis valores: la próxima vez lo tendrás a mano sin buscar.\n"
             "• Crear truco: se guarda en la carpeta de trucos de Atmosphère para activarlo con EdiZon o Breeze."},
            {"Multiplicar lo que ganes (x2, x3…)",
             "1. Busca el valor (por ejemplo, tu experiencia) y guárdalo en Mis valores.\n"
             "2. En Mis valores pulsa X y elige \"Multiplicar lo que ganes\" (también está en el menú de cada "
             "resultado).\n"
             "3. Elige x2, x3, x5, x10 u otro número y actívalo cuando ya estés jugando tu partida.\n"
             "• Cada vez que el valor suba, la subida se multiplica. Si baja (al gastar), no se toca.\n"
             "• Sigue funcionando aunque cierres ValorNX, hasta que cierres el juego. Puedes pausarlo o quitarlo "
             "desde Mis valores, o con EdiZon/Breeze.\n"
             "• Solo funciona con números enteros. Si en tu juego la experiencia vuelve a 0 al subir de nivel, la "
             "ganancia que te hace subir de nivel no se multiplica.\n"
             "• Si el número se descontrola, seguramente multiplicaste una copia que el juego usa para animar el "
             "contador: quita el multiplicador y prueba con otro resultado."},
            {"Overlay: busca y cambia sin salir del juego",
             "• Necesita Ultrahand o Tesla Menu (con nx-ovlloader). Copia ValorNX.ovl a sd:/switch/.overlays/.\n"
             "• Mientras juegas, abre el menú de overlays con su combinación y elige ValorNX. Ultrahand trae ZL + ZR + "
             "abajo y Tesla L + abajo + R3, pero muchos packs la cambian (por ejemplo a ZL + ZR + L + R). Con "
             "Ultrahand, en modo portátil, también se abre deslizando el dedo desde el borde izquierdo de la pantalla.\n"
             "• Tiene dos pestañas; se cambian con la cruceta (izquierda y derecha):\n"
             "   – Mis valores: A para cambiar un valor, Y para congelarlo y X para el multiplicador.\n"
             "   – Buscar: las mismas búsquedas que esta app. Entre un paso y otro, cierra el overlay, juega para que "
             "el valor cambie y vuelve a abrirlo.\n"
             "• La búsqueda es la misma en la app y en el overlay: puedes empezarla en uno y seguirla en el otro.\n"
             "• Mientras busca, no cierres el overlay: si lo cierras, esa búsqueda se cancela.\n"
             "• El overlay no puede abrir esta app (la consola no deja que un overlay abra el Álbum): ábrela desde "
             "el Álbum como siempre."},
            {"¿Mis valores siguen ahí si cierro el juego?",
             "• Tus valores guardados no se borran: siguen en Mis valores (y en el overlay).\n"
             "• Los de la zona \"main\" suelen estar en el mismo sitio cada vez que abres el juego. Los de \"heap\" "
             "a veces cambian de sitio al reiniciarlo.\n"
             "• Al abrir el juego otra vez, mira en Mis valores si el número coincide con el del juego. Si ves un "
             "número raro, ese valor cambió de sitio: búscalo otra vez y guárdalo de nuevo."},
            {"Si algo no funciona",
             "• Si quedan varios resultados, cambia uno cada vez y mira cuál afecta al juego, o usa \"Cambiar "
             "todos\".\n"
             "• Algunos juegos guardan el valor como decimal: prueba el tipo \"Decimal\".\n"
             "• Si no encuentras nada, usa el tipo \"Automático\" y comprueba que escribes el número exacto.\n"
             "• Hay juegos que muestran una copia del valor: si cambias uno y en pantalla no se actualiza, gasta o "
             "gana algo para que se refresque."},
            {"Botones",
             "A: aceptar   ·   B: volver   ·   X / Y: opciones de cada pantalla   ·   +: salir\n"
             "También puedes usar la pantalla táctil y arrastrar las listas con el dedo."},
        };
        // El texto se parte en líneas una sola vez; después solo se dibuja.
        if (m_lines.empty()) {
            int y = 0;
            for (const Sec& s : kSecs) {
                m_lines.push_back({s.title, 26, theme::accent, 0, y});
                y += 40;
                for (const std::string& l : c.wrap(s.body, 22, 1150)) {
                    m_lines.push_back({l, 22, theme::text, 8, y});
                    y += c.font().lineHeight(22) + 8;
                }
                y += 22;
            }
            m_contentH = y;
        }
        c.text(kMargin, 104, "Cómo se usa", 30, theme::text);
        c.pushClip(kMargin, kViewY, 1184, kViewH);
        for (const Line& l : m_lines) {
            const int y = kViewY - m_scroll + l.y;
            if (y + 40 < kViewY || y > kViewY + kViewH) continue;
            c.text(kMargin + l.indent, y, l.text, l.size, l.color);
        }
        c.popClip();
        if (m_contentH > kViewH) {
            const int barH = std::max(40, kViewH * kViewH / m_contentH);
            const int barY = kViewY + (kViewH - barH) * m_scroll / std::max(1, m_contentH - kViewH);
            c.fillRoundRect(1244, kViewY, 6, kViewH, 3, theme::card);
            c.fillRoundRect(1244, barY, 6, barH, 3, theme::faint);
        }
    }

    void hints(App&, std::vector<Hint>& out) override {
        out.push_back({"L/R", "Página"});
        out.push_back({"B", "Volver"});
    }

private:
    struct Line {
        std::string text;
        int size;
        Color color;
        int indent, y;
    };
    static constexpr int kViewY = 152, kViewH = 488;
    std::vector<Line> m_lines;
    int m_scroll = 0;
    int m_contentH = 0;
};

// ================================================================== Menú emergente

class MenuScreen : public Screen {
public:
    MenuScreen(const std::string& title, const std::string& sub, const std::vector<MenuItem>& items,
               std::function<void(int)> onPick, int initial)
        : m_title(title), m_sub(sub), m_items(items), m_onPick(std::move(onPick)) {
        m_lv.sel = initial;
        for (const MenuItem& it : m_items)
            if (!it.sub.empty()) m_hasSubs = true;
    }

    bool modal() const override { return true; }

    void update(App& app, const Input& in) override {
        if (in.down & BTN_B) {
            app.pop();
            return;
        }
        if (m_boxH > 0 && in.tap && !hitRect(in.tapX, in.tapY, m_boxX, m_boxY, kBoxW, m_boxH)) {
            app.pop();
            return;
        }
        if (m_boxH == 0) return;   // aún no se ha dibujado
        const int act = m_lv.update(in, (int)m_items.size(), true);
        if (act >= 0) {
            std::function<void(int)> cb = m_onPick;
            app.pop();
            if (cb) cb(act);
        }
    }

    void draw(App& app, Canvas& c) override {
        layout(c);
        c.fillRoundRect(m_boxX, m_boxY, kBoxW, m_boxH, 22, theme::card);
        c.strokeRoundRect(m_boxX, m_boxY, kBoxW, m_boxH, 22, 1, theme::line);
        int y = m_boxY + 26;
        c.text(m_boxX + 32, y, m_title, 28, theme::text, Align::Left, kBoxW - 64);
        y += 44;
        for (const std::string& l : m_subLines) {
            c.text(m_boxX + 32, y, l, 21, theme::dim);
            y += 29;
        }
        listClip(c, m_lv);
        for (int i = 0; i < (int)m_items.size(); i++) {
            if (!m_lv.rowVisible(i)) continue;
            const MenuItem& it = m_items[i];
            const int ry = m_lv.rowY(i);
            const bool sel = i == m_lv.sel;
            c.fillRoundRect(m_lv.x, ry, m_lv.w, m_lv.rowH, 12, sel ? theme::cardHi : theme::bar);
            if (sel) c.strokeRoundRect(m_lv.x - 3, ry - 3, m_lv.w + 6, m_lv.rowH + 6, 15, 3, theme::accent);
            int tx = m_lv.x + 20;
            if (it.hasIcon) {
                drawIcon(c, it.icon, m_lv.x + 18, ry + (m_lv.rowH - 30) / 2, 30, it.color);
                tx = m_lv.x + 64;
            }
            if (it.sub.empty()) {
                c.text(tx, ry + (m_lv.rowH - 34) / 2, it.label, 24, theme::text, Align::Left, m_lv.w - (tx - m_lv.x) - 16);
            } else {
                c.text(tx, ry + 9, it.label, 24, theme::text, Align::Left, m_lv.w - (tx - m_lv.x) - 16);
                c.text(tx, ry + 42, it.sub, 18, theme::dim, Align::Left, m_lv.w - (tx - m_lv.x) - 16);
            }
        }
        c.popClip();
        m_lv.drawScrollbar(c, (int)m_items.size());
    }

    void hints(App&, std::vector<Hint>& out) override {
        out.push_back({"A", "Elegir"});
        out.push_back({"B", "Cancelar"});
    }

private:
    static constexpr int kBoxW = 700;
    std::string m_title, m_sub;
    std::vector<MenuItem> m_items;
    std::function<void(int)> m_onPick;
    std::vector<std::string> m_subLines;
    bool m_hasSubs = false;
    bool m_laidOut = false;
    ListView m_lv;
    int m_boxX = 0, m_boxY = 0, m_boxH = 0;

    void layout(Canvas& c) {
        if (m_laidOut) return;
        m_laidOut = true;
        if (!m_sub.empty()) m_subLines = c.wrap(m_sub, 21, kBoxW - 64);
        const int rowH = m_hasSubs ? 72 : 58, gap = 8;
        const int visible = std::min((int)m_items.size(), m_hasSubs ? 5 : 7);
        const int listH = visible * (rowH + gap) - gap;
        const int headH = 26 + 44 + (int)m_subLines.size() * 29 + 12;
        m_boxH = headH + listH + 28;
        m_boxX = (kScreenW - kBoxW) / 2;
        m_boxY = std::max(96, (kScreenH - 60 - m_boxH) / 2);
        m_lv.x = m_boxX + 28;
        m_lv.y = m_boxY + headH;
        m_lv.w = kBoxW - 56 - (visible < (int)m_items.size() ? 16 : 0);
        m_lv.h = listH;
        m_lv.rowH = rowH;
        m_lv.gap = gap;
        m_lv.ensureVisible((int)m_items.size());
    }
};

// ================================================================== Mensaje

class MessageScreen : public Screen {
public:
    MessageScreen(const std::string& title, const std::string& text, Icon icon)
        : m_title(title), m_text(text), m_icon(icon) {}

    bool modal() const override { return true; }

    void update(App& app, const Input& in) override {
        if ((in.down & (BTN_A | BTN_B)) || in.tap) app.pop();
    }

    void draw(App& app, Canvas& c) override {
        const int w = 800, x = (kScreenW - w) / 2;
        const std::vector<std::string> lines = c.wrap(m_text, 22, w - 80);
        const int h = 30 + 50 + (int)lines.size() * 31 + 30 + 60 + 24;
        const int y = std::max(96, (kScreenH - 60 - h) / 2);
        c.fillRoundRect(x, y, w, h, 22, theme::card);
        c.strokeRoundRect(x, y, w, h, 22, 1, theme::line);
        const Color ic = m_icon == Icon::Warning ? theme::red : (m_icon == Icon::Bolt ? theme::gold : theme::accent);
        drawIcon(c, m_icon, x + 36, y + 32, 34, ic);
        c.text(x + 86, y + 30, m_title, 29, theme::text, Align::Left, w - 120);
        int ty = y + 30 + 56;
        for (const std::string& l : lines) {
            c.text(x + 40, ty, l, 22, theme::text);
            ty += 31;
        }
        const int bw = 220, bx = x + (w - bw) / 2, by = y + h - 24 - 56;
        c.fillRoundRect(bx, by, bw, 56, 28, theme::accent);
        c.fillCircle(bx + 60, by + 28, 14, theme::bar);
        c.text(bx + 60, by + 16, "A", 19, theme::accent, Align::Center);
        c.text(bx + 84, by + 13, "Aceptar", 24, theme::bar);
    }

    void hints(App&, std::vector<Hint>& out) override { out.push_back({"A", "Aceptar"}); }

private:
    std::string m_title, m_text;
    Icon m_icon;
};

}  // namespace

Screen* makeHomeScreen() { return new HomeScreen(); }
Screen* makeNoGameScreen() { return new NoGameScreen(); }
Screen* makeSearchScreen() { return new SearchScreen(); }
Screen* makeResultsScreen() { return new ResultsScreen(); }
Screen* makeFavoritesScreen() { return new FavoritesScreen(); }
Screen* makeSettingsScreen() { return new SettingsScreen(); }
Screen* makeHelpScreen() { return new HelpScreen(); }
Screen* makeKeypadScreen(const KeypadSpec& spec, std::function<void(const std::string&, int)> onOk) {
    return new KeypadScreen(spec, std::move(onOk), nullptr);
}
Screen* makeRangeKeypadScreen(const KeypadSpec& spec,
                              std::function<void(const std::string&, const std::string&, int)> onOk) {
    KeypadSpec s = spec;
    s.range = true;
    return new KeypadScreen(s, nullptr, std::move(onOk));
}
Screen* makeMenuScreen(const std::string& title, const std::string& sub, const std::vector<MenuItem>& items,
                       std::function<void(int)> onPick, int initial) {
    return new MenuScreen(title, sub, items, std::move(onPick), initial);
}
Screen* makeMessageScreen(const std::string& title, const std::string& text, Icon icon) {
    return new MessageScreen(title, text, icon);
}

}  // namespace vnx
