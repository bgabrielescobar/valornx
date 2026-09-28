#include "ui_kit.hpp"

#include <algorithm>

namespace vnx {
namespace ovl {

// ------------------------------------------------------------------ lista

void Scroller::clamp(int count) {
    const int maxS = std::max(0, count * rowH - h);
    scroll = std::max(0, std::min(scroll, maxS));
}

void Scroller::ensureVisible(int count) {
    const int top = sel * rowH;
    if (top < scroll) scroll = top;
    if (top + rowH > scroll + h) scroll = top + rowH - h;
    clamp(count);
}

int Scroller::handle(const PanelInput& in, int count) {
    if (count <= 0) return -1;
    sel = std::max(0, std::min(sel, count - 1));
    if (in.down & BTN_UP) sel = sel > 0 ? sel - 1 : count - 1;
    if (in.down & BTN_DOWN) sel = sel < count - 1 ? sel + 1 : 0;
    if (in.down & (BTN_UP | BTN_DOWN)) ensureVisible(count);
    if (in.dragY) {
        scroll -= in.dragY;
        clamp(count);
    }
    if (in.tap && in.tapY >= y && in.tapY < y + h) {
        for (int i = 0; i < count; i++) {
            if (in.tapY >= rowY(i) && in.tapY < rowY(i) + rowH) {
                sel = i;
                ensureVisible(count);
                return i;
            }
        }
    }
    return -1;
}

void Scroller::drawScrollbar(Painter& p, int count) const {
    const int content = count * rowH;
    if (content <= h) return;
    const int barH = std::max(30, h * h / content);
    const int barY = y + (int)((int64_t)(h - barH) * scroll / (content - h));
    p.rect(441, y, 3, h, col::line);
    p.rect(441, barY, 3, barH, col::dim);
}

const char* shortTypeHelp(int choice) {
    static const char* kHelp[] = {
        "Enteros y decimales a la vez (recomendado).",
        "Lo más común: dinero, experiencia, puntos.",
        "Vida o energía en juegos de acción.",
        "Números pequeños, hasta 65.535.",
        "Hasta 255. Da muchísimos resultados.",
        "Cantidades enormes (más de 2.000 millones).",
        "Poco común: algunos juegos, para el dinero.",
    };
    const int n = (int)(sizeof(kHelp) / sizeof(kHelp[0]));
    return choice >= 0 && choice < n ? kHelp[choice] : "";
}

void drawEmpty(Painter& p, Icon ic, Color icColor, const std::string& title, const std::string& body,
               const std::string& extra, Color extraColor) {
    icon(p, ic, Panel::kW / 2 - 32, 170, 64, icColor);
    int y = 256;
    for (const std::string& l : wrap(p, title, 23, 390)) {
        text(p, Panel::kW / 2, y, l, 23, col::text, Align::Center);
        y += 30;
    }
    y += 12;
    for (const std::string& l : wrap(p, body, 18, 390)) {
        text(p, Panel::kW / 2, y, l, 18, col::dim, Align::Center);
        y += 25;
    }
    if (!extra.empty()) {
        y += 14;
        for (const std::string& l : wrap(p, extra, 17, 390)) {
            text(p, Panel::kW / 2, y, l, 17, extraColor, Align::Center);
            y += 23;
        }
    }
}

// ------------------------------------------------------------------ menú

MenuPage::MenuPage(const std::string& title, const std::string& sub, std::vector<MenuItemO> items,
                   std::function<void(Panel&, int)> onPick)
    : m_title(title), m_sub(sub), m_items(std::move(items)), m_onPick(std::move(onPick)) {
    for (const MenuItemO& it : m_items)
        if (!it.sub.empty()) m_hasSubs = true;
}

bool MenuPage::input(Panel& panel, const PanelInput& in) {
    if (in.down & BTN_B) {
        panel.pop();
        return true;
    }
    if (!m_laidOut) return true;   // todavía no se ha dibujado
    const int tapped = m_list.handle(in, (int)m_items.size());
    const int pick = tapped >= 0 ? tapped : ((in.down & BTN_A) ? m_list.sel : -1);
    if (pick >= 0) {
        std::function<void(Panel&, int)> cb = m_onPick;
        panel.pop();
        if (cb) cb(panel, pick);
    }
    return true;
}

void MenuPage::draw(Panel&, Painter& p) {
    layout(p);
    text(p, 20, Panel::kContentY + 2, m_title, 25, col::text, Align::Left, 408);
    int y = Panel::kContentY + 38;
    for (const std::string& l : m_subLines) {
        p.text(20, y, l, 17, col::dim);
        y += 23;
    }
    p.pushClip(0, m_list.y, Panel::kW, m_list.h);
    for (int i = 0; i < (int)m_items.size(); i++) {
        if (!m_list.visible(i)) continue;
        const MenuItemO& it = m_items[i];
        const int ry = m_list.rowY(i);
        const int rh = m_list.rowH - 6;
        if (i == m_list.sel) {
            roundRect(p, 12, ry, 424, rh, 10, col::selBg);
            roundRectOutline(p, 12, ry, 424, rh, 10, 2, col::accent);
        } else {
            roundRect(p, 12, ry, 424, rh, 10, col::keyBg);
        }
        icon(p, it.icon, 28, ry + (rh - 26) / 2, 26, it.color);
        if (it.sub.empty()) {
            text(p, 68, ry + (rh - 26) / 2, it.label, 21, col::text, Align::Left, 354);
        } else {
            text(p, 68, ry + 8, it.label, 21, col::text, Align::Left, 354);
            text(p, 68, ry + 38, it.sub, 16, col::dim, Align::Left, 354);
        }
    }
    p.popClip();
    m_list.drawScrollbar(p, (int)m_items.size());
}

void MenuPage::hints(Panel&, std::vector<Hint>& out) {
    out.push_back({"B", "Volver"});
    out.push_back({"A", "Elegir"});
}

void MenuPage::layout(Painter& p) {
    if (m_laidOut) return;
    m_laidOut = true;
    if (!m_sub.empty()) {
        m_subLines = wrap(p, m_sub, 17, 408);
        if (m_subLines.size() > 4) m_subLines.resize(4);
    }
    m_list.y = Panel::kContentY + 38 + (int)m_subLines.size() * 23 + 14;
    m_list.h = Panel::kContentBottom - m_list.y;
    m_list.rowH = m_hasSubs ? 74 : 60;
    m_list.ensureVisible((int)m_items.size());
}

void confirm(Panel& panel, const std::string& title, const std::string& text, const std::string& yes,
             std::function<void(Panel&)> onYes) {
    std::vector<MenuItemO> items = {{Icon::Check, col::accent, yes, ""}, {Icon::Close, col::dim, "Cancelar", ""}};
    panel.push(new MenuPage(title, text, items, [onYes](Panel& pn, int k) {
        if (k == 0 && onYes) onYes(pn);
    }));
}

// ------------------------------------------------------------------ mensaje

bool MessagePage::input(Panel& panel, const PanelInput& in) {
    if ((in.down & (BTN_A | BTN_B)) || in.tap) panel.pop();
    return true;
}

void MessagePage::draw(Panel&, Painter& p) {
    icon(p, Icon::Warning, 20, Panel::kContentY + 4, 28, col::red);
    int y = Panel::kContentY + 2;
    for (const std::string& l : wrap(p, m_title, 23, 364)) {
        p.text(60, y, l, 23, col::text);
        y += 30;
    }
    y += 14;
    for (const std::string& l : wrap(p, m_text, 18, 404)) {
        p.text(22, y, l, 18, col::dim);
        y += 25;
    }
}

// ------------------------------------------------------------------ teclado

KeypadPage::KeypadPage(const KeypadSpec& spec, KeypadDone done)
    : m_spec(spec), m_done(std::move(done)), m_type(spec.typeChoice) {
    if (m_type < 0 || m_type >= typeChoiceCount()) m_type = 0;
    m_entry[0] = NumberEntry(spec.initial);
    const char* rows[4][4] = {{"7", "8", "9", "<"}, {"4", "5", "6", "C"}, {"1", "2", "3", "-"}, {"0", "0", ".", "K"}};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (r == 3 && c == 1) continue;   // el 0 ocupa dos casillas
            m_cells.push_back({r, c, (r == 3 && c == 0) ? 2 : 1, rows[r][c][0], ""});
        }
    }
    if (spec.quick) {
        const char* quick[4] = {"99", "999", "9999", "99999"};
        for (int c = 0; c < 4; c++) m_cells.push_back({4, c, 1, 'Q', quick[c]});
    } else if (typeSelectable()) {
        m_cells.push_back({4, 0, 4, 'T', ""});   // selector del tipo de valor
    }
    m_rows = (spec.quick || typeSelectable()) ? 5 : 4;
    m_sel = cellAt(1, 1);   // el 5
}

TypeMask KeypadPage::mask() const { return m_spec.lockedMask ? m_spec.lockedMask : typeChoice(m_type).mask; }

bool KeypadPage::decimalsAllowed() const {
    if (!m_spec.search) return vtypeIsFloat(m_spec.editType);
    return (mask() & (typeBit(VType::F32) | typeBit(VType::F64))) != 0;
}

bool KeypadPage::input(Panel& panel, const PanelInput& in) {
    if (in.down & BTN_UP) move(-1, 0);
    if (in.down & BTN_DOWN) move(1, 0);
    const bool onType = m_cells[m_sel].key == 'T';
    if (in.down & BTN_LEFT) {
        if (onType)
            changeType(-1);
        else
            move(0, -1);
    }
    if (in.down & BTN_RIGHT) {
        if (onType)
            changeType(1);
        else
            move(0, 1);
    }
    if ((in.down & BTN_A) && press(panel, m_cells[m_sel].key, m_cells[m_sel].quick)) return true;
    if (in.down & BTN_B) {
        NumberEntry& e = m_entry[m_field];
        if (e.empty() || e.fresh()) {
            if (m_spec.range && m_field == 1) {
                switchField(0);   // volver a la primera casilla
            } else {
                panel.pop();
                return true;
            }
        } else {
            press(panel, '<', "");
        }
    }
    if (in.down & BTN_X) press(panel, 'C', "");
    if (in.down & BTN_Y) press(panel, '-', "");
    if ((in.down & BTN_PLUS) && submit(panel)) return true;
    if (m_spec.range && (in.down & (BTN_ZL | BTN_ZR))) switchField((in.down & BTN_ZL) ? 0 : 1);
    if (in.typed) {
        const char k = in.typed;
        if ((k >= '0' && k <= '9') || k == '.' || k == '-') {
            if (press(panel, k, "")) return true;
        } else if (k == '\b') {
            press(panel, '<', "");
        } else if (k == '\t' && m_spec.range) {
            switchField(1 - m_field);
        } else if (k == '\n') {
            if (submit(panel)) return true;
        }
    }
    if (in.tap) {
        for (int i = 0; i < (int)m_cells.size(); i++) {
            int x, y, w, h;
            cellRect(m_cells[i], x, y, w, h);
            if (!hitRect(in.tapX, in.tapY, x, y, w, h)) continue;
            m_sel = i;
            m_row = m_cells[i].row;
            m_col = m_cells[i].col;
            if (m_cells[i].key == 'T') {
                changeType(in.tapX < x + w / 2 ? -1 : 1);
                return true;
            }
            if (press(panel, m_cells[i].key, m_cells[i].quick)) return true;
        }
        if (m_spec.range) {
            for (int f = 0; f < 2; f++) {
                int x, y, w, h;
                fieldRect(f, x, y, w, h);
                if (hitRect(in.tapX, in.tapY, x, y, w, h)) switchField(f);
            }
        }
    }
    return true;
}

void KeypadPage::draw(Panel& panel, Painter& p) {
    const int top = Panel::kContentY;
    text(p, 20, top + 2, m_spec.title, 24, col::text, Align::Left, 408);
    if (!m_spec.info.empty()) text(p, 20, top + 34, m_spec.info, 17, col::dim, Align::Left, 408);
    // Pantallita (o las dos casillas "Desde" y "Hasta")
    for (int f = 0; f < (m_spec.range ? 2 : 1); f++) {
        int x, y, w, h;
        fieldRect(f, x, y, w, h);
        const bool active = f == m_field;
        const NumberEntry& e = m_entry[f];
        roundRect(p, x, y, w, h, 12, col::display);
        roundRectOutline(p, x, y, w, h, 12, 2, !active ? col::line : (m_err.empty() ? col::accent : col::red));
        int size = 38, ty = y + 10;
        if (m_spec.range) {
            p.text(x + 12, y + 5, f == 0 ? "Desde" : "Hasta", 15, active ? col::accent : col::dim);
            size = 30;
            ty = y + 25;
        }
        const int right = x + w - 20;
        const std::string shown = e.empty() ? "0" : e.text();
        const std::string fit = ellipsize(p, shown, size, w - (m_spec.range ? 70 : 48));
        const int tw = p.measure(fit, size);
        if (e.fresh() && !e.empty()) roundRect(p, right - tw - 6, ty - 1, tw + 12, size + 8, 6, withAlpha(col::accent, 90));
        p.text(right - tw, ty, fit, size, e.empty() ? col::faint : col::text);
        if (active && !e.fresh() && (panel.now() / 500) % 2 == 0) p.rect(right + 4, ty + 3, 2, size, col::accent);
    }
    const int iy = kDispY + kDispH + 5;
    if (!m_err.empty()) {
        std::vector<std::string> lines = wrap(p, m_err, 16, 404);
        if (lines.size() > 2) lines.resize(2);
        int y = iy;
        for (const std::string& l : lines) {
            p.text(22, y, l, 16, col::red);
            y += 21;
        }
    } else if (m_spec.range) {
        p.text(22, iy, "Da igual el orden de los dos números.", 16, col::faint);
    }
    // Teclas
    for (int i = 0; i < (int)m_cells.size(); i++) {
        const Cell& c = m_cells[i];
        int x, y, w, h;
        cellRect(c, x, y, w, h);
        const bool sel = i == m_sel, ok = c.key == 'K';
        const bool disabled = c.key == '.' && !decimalsAllowed();
        roundRect(p, x, y, w, h, 10, ok ? col::accent : (sel ? col::keySel : col::keyBg));
        if (sel) roundRectOutline(p, x - 2, y - 2, w + 4, h + 4, 12, 2, ok ? col::text : col::accent);
        const Color fg = ok ? col::dark : (disabled ? col::faint : col::text);
        const int cx = x + w / 2;
        switch (c.key) {
            case '<': icon(p, Icon::Backspace, cx - 15, y + h / 2 - 15, 30, fg); break;
            case 'C': text(p, cx, y + h / 2 - 11, "Limpiar", 17, fg, Align::Center); break;
            case '-': text(p, cx, y + h / 2 - 14, "+/-", 23, fg, Align::Center); break;
            case 'K':
                if (m_spec.range && m_field == 0)
                    text(p, cx, y + h / 2 - 11, "Siguiente", 17, fg, Align::Center);
                else
                    text(p, cx, y + h / 2 - 15, "OK", 25, fg, Align::Center);
                break;
            case 'Q': text(p, cx, y + h / 2 - 12, c.quick, 20, col::gold, Align::Center); break;
            case 'T':
                icon(p, Icon::Left, x + 12, y + h / 2 - 9, 18, sel ? col::accent : col::dim);
                icon(p, Icon::Right, x + w - 30, y + h / 2 - 9, 18, sel ? col::accent : col::dim);
                text(p, cx, y + h / 2 - 12, typeChoice(m_type).label, 20, col::text, Align::Center, w - 80);
                break;
            default: text(p, cx, y + h / 2 - 17, std::string(1, c.key), 30, fg, Align::Center); break;
        }
    }
    if (m_rows == 5) {
        int x, y, w, h;
        cellRect(m_cells.back(), x, y, w, h);
        p.text(20, y - 23, m_spec.quick ? "Valores rápidos" : "Tipo de valor", 16, col::dim);
        if (!m_spec.quick) text(p, Panel::kW / 2, y + h + 8, shortTypeHelp(m_type), 15, col::faint, Align::Center, 408);
    }
}

void KeypadPage::hints(Panel&, std::vector<Hint>& out) {
    const NumberEntry& e = m_entry[m_field];
    const bool back = e.empty() || e.fresh();
    out.push_back({"B", !back ? "Borrar" : (m_spec.range && m_field == 1 ? "Desde" : "Volver")});
    out.push_back({"A", "Pulsar"});
    out.push_back({"+", m_spec.range && m_field == 0 ? "Siguiente" : "Aceptar"});
}

void KeypadPage::cellRect(const Cell& c, int& x, int& y, int& w, int& h) const {
    x = kGridX + c.col * (kCellW + kGap);
    y = kGridY + c.row * (kCellH + kGap) + (c.row == 4 ? 30 : 0);
    w = c.span * kCellW + (c.span - 1) * kGap;
    h = kCellH;
}

void KeypadPage::fieldRect(int f, int& x, int& y, int& w, int& h) const {
    y = kDispY;
    h = kDispH;
    if (!m_spec.range) {
        x = 20;
        w = 408;
        return;
    }
    w = 200;
    x = 20 + f * (w + 8);
}

int KeypadPage::cellAt(int row, int col) const {
    for (int i = 0; i < (int)m_cells.size(); i++) {
        const Cell& c = m_cells[i];
        if (c.row == row && col >= c.col && col < c.col + c.span) return i;
    }
    return 0;
}

void KeypadPage::move(int dr, int dc) {
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

void KeypadPage::changeType(int delta) {
    const int n = typeChoiceCount();
    m_type = ((m_type + delta) % n + n) % n;
    m_err.clear();
}

void KeypadPage::switchField(int f) {
    m_field = f;
    m_err.clear();
}

bool KeypadPage::press(Panel& panel, char key, const std::string& quick) {
    m_err.clear();
    if (key == 'K') return submit(panel);
    if (key == 'T') {
        changeType(1);
        return false;
    }
    if (key == 'Q')
        m_entry[m_field].set(quick);
    else
        m_err = m_entry[m_field].press(key, decimalsAllowed());
    return false;
}

std::string KeypadPage::searchError(const Number& n) const {
    if (!n.valid) return "Ese número no es válido.";
    uint64_t raw = 0;
    for (int i = 0; i < kVTypeCount; i++)
        if ((mask() & typeBit((VType)i)) && encodeNumber((VType)i, n, raw)) return "";
    return n.isDecimal ? "Este tipo no admite decimales: elige \"Decimal\" o \"Automático\"."
                       : "El número es demasiado grande para este tipo de valor.";
}

bool KeypadPage::submit(Panel& panel) {
    if (m_spec.range) return submitRange(panel);
    const std::string t = m_entry[0].text();
    if (t.empty() || t == "-") {
        m_err = "Escribe un número.";
        return false;
    }
    const Number n = parseNumber(t);
    m_err = m_spec.search ? searchError(n) : numberErrorForType(m_spec.editType, n);
    if (!m_err.empty()) return false;
    KeypadDone done = m_done;
    const int tc = m_type;
    panel.pop();
    if (done) done(panel, t, "", tc);
    return true;
}

bool KeypadPage::submitRange(Panel& panel) {
    std::string lo = m_entry[0].text(), hi = m_entry[1].text();
    const bool emptyLo = lo.empty() || lo == "-", emptyHi = hi.empty() || hi == "-";
    if (m_field == 0 && !emptyLo && emptyHi) {
        switchField(1);   // primero "Desde", después "Hasta"
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
    KeypadDone done = m_done;
    const int tc = m_type;
    panel.pop();
    if (done) done(panel, lo, hi, tc);
    return true;
}

}  // namespace ovl
}  // namespace vnx
