#include "panel.hpp"

#include <algorithm>
#include <cstdio>

#include "../core/fsutil.hpp"
#include "../core/multiplier.hpp"
#include "pages.hpp"

namespace vnx {
namespace ovl {

namespace {

std::string valueText(Panel& panel, size_t i) {
    uint64_t raw = 0;
    if (!panel.liveValue(i, raw)) return "?";
    return formatRaw(panel.favs()[i].type, raw);
}

// ------------------------------------------------------------------ opciones de un valor guardado

void openOptions(Panel& panel, size_t index) {
    const Favorite f = panel.favs()[index];
    const uint64_t addr = panel.addressOf(f);
    enum Act { EDIT, FREEZE, MULTIPLY, TOGGLE, CHANGE, REMOVE };
    std::vector<MenuItemO> items;
    std::vector<Act> acts;
    items.push_back({Icon::Pencil, col::accent, "Cambiar valor", ""});
    acts.push_back(EDIT);
    const bool frozen = panel.ctl().isFrozen(addr);
    items.push_back({Icon::Snowflake, col::ice, frozen ? "Descongelar" : "Congelar",
                     frozen ? "El juego podrá volver a cambiarlo." : "El juego ya no podrá cambiarlo."});
    acts.push_back(FREEZE);
    if (multiplierSupported(f.type)) {
        const ActiveMultiplier* m = panel.ctl().multiplierFor(f);
        if (!m) {
            items.push_back({Icon::Up, col::gold, "Multiplicar lo que ganes", "x2, x3, x5 o x10"});
            acts.push_back(MULTIPLY);
        } else {
            const std::string fx = "x" + std::to_string(m->spec.factor);
            items.push_back({m->enabled ? Icon::Pause : Icon::Play, col::gold,
                             (m->enabled ? "Pausar el " : "Reanudar el ") + fx,
                             m->enabled ? "Ahora mismo multiplica lo que ganas." : "Ahora mismo está en pausa."});
            acts.push_back(TOGGLE);
            items.push_back({Icon::Up, col::gold, "Cambiar el multiplicador", ""});
            acts.push_back(CHANGE);
            items.push_back({Icon::Trash, col::red, "Quitar el multiplicador", ""});
            acts.push_back(REMOVE);
        }
    }
    const std::string sub = std::string(vtypeLabel(f.type)) + "  ·  Valor actual: " + valueText(panel, index);
    panel.push(new MenuPage(f.name, sub, items, [f, acts, addr](Panel& pn, int k) {
        if (k < 0 || k >= (int)acts.size()) return;
        switch (acts[k]) {
            case EDIT: editValue(pn, "Cambiar: " + f.name, f.name, addr, f.type); break;
            case FREEZE: pn.report(pn.ctl().toggleFreeze(addr, f.type)); break;
            case MULTIPLY:
            case CHANGE: openFactorMenu(pn, f); break;
            case TOGGLE: pn.report(pn.ctl().toggleMultiplier(f)); break;
            case REMOVE: pn.report(pn.ctl().removeMultiplier(f)); break;
        }
        pn.readValuesNow();
    }));
}

// ------------------------------------------------------------------ pestaña "Mis valores"

class ValuesTab : public Page {
public:
    ValuesTab() {
        m_list.y = kListY;
        m_list.h = Panel::kContentBottom - kListY;
        m_list.rowH = 70;
    }

    bool input(Panel& panel, const PanelInput& in) override {
        std::vector<Favorite>& items = panel.favs();
        const int n = (int)items.size();
        if (n == 0) {
            if ((in.down & BTN_A) || in.tap) panel.setTab(Panel::TabSearch);
            return true;
        }
        const int tapped = m_list.handle(in, n);
        const int i = m_list.sel;
        if (tapped >= 0 || (in.down & BTN_A)) {
            editValue(panel, "Cambiar: " + items[i].name, items[i].name, panel.addressOf(items[i]), items[i].type);
        } else if (in.down & BTN_Y) {
            panel.report(panel.ctl().toggleFreeze(panel.addressOf(items[i]), items[i].type));
            panel.readValuesNow();
        } else if (in.down & BTN_X) {
            openOptions(panel, (size_t)i);
        }
        return true;
    }

    void draw(Panel& panel, Painter& p) override {
        const std::vector<Favorite>& items = panel.favs();
        if (items.empty()) {
            drawEmpty(p, Icon::Star, col::gold, "Todavía no guardaste valores de este juego",
                      "Búscalos en la pestaña Buscar (o con la app) y elige \"Guardar en Mis valores\". Aquí podrás "
                      "cambiarlos sin salir del juego.");
            return;
        }
        const ProcessInfo& pi = panel.game()->info();
        const std::string build = pi.buildIdHex();
        m_list.clamp((int)items.size());
        p.pushClip(0, m_list.y, Panel::kW, m_list.h);
        for (int i = 0; i < (int)items.size(); i++) {
            if (!m_list.visible(i)) continue;
            const Favorite& f = items[i];
            const int y = m_list.rowY(i);
            const int rh = m_list.rowH - 4;
            if (i == m_list.sel) {
                roundRect(p, 10, y, 428, rh, 10, col::selBg);
                roundRectOutline(p, 10, y, 428, rh, 10, 2, col::accent);
            } else if (i > 0 && i - 1 != m_list.sel) {
                p.rect(24, y - 2, 400, 1, col::line);
            }
            uint64_t raw = 0;
            const bool ok = panel.liveValue((size_t)i, raw);
            const int vw = text(p, 424, y + 7, ok ? formatRaw(f.type, raw) : "?", 26, ok ? col::gold : col::faint,
                                Align::Right, 170);
            text(p, 24, y + 8, f.name, 22, col::text, Align::Left, 400 - vw - 24);
            drawStatus(panel, p, f, y + 40, build);
        }
        p.popClip();
        m_list.drawScrollbar(p, (int)items.size());
    }

    void hints(Panel& panel, std::vector<Hint>& out) override {
        out.push_back({"B", "Salir"});
        if (panel.favs().empty()) {
            out.push_back({"A", "Buscar"});
            return;
        }
        out.push_back({"A", "Cambiar"});
        out.push_back({"Y", "Congelar"});
        out.push_back({"X", "Más"});
    }

private:
    static constexpr int kListY = 146;
    Scroller m_list;

    // Segunda línea de cada valor: congelado, multiplicador o el tipo.
    void drawStatus(Panel& panel, Painter& p, const Favorite& f, int y, const std::string& build) {
        int x = 24;
        auto seg = [&](bool hasIcon, Icon ic, const std::string& s, Color c) {
            if (hasIcon) {
                icon(p, ic, x, y + 1, 17, c);
                x += 21;
            }
            x += p.text(x, y, s, 16, c) + 16;
        };
        const bool frozen = panel.ctl().isFrozen(panel.addressOf(f));
        const ActiveMultiplier* m = panel.ctl().multiplierFor(f);
        if (frozen) seg(true, Icon::Snowflake, "Congelado", col::ice);
        if (m)
            seg(true, Icon::Up, "x" + std::to_string(m->spec.factor) + (m->enabled ? "" : " en pausa"),
                m->enabled ? col::gold : col::faint);
        if (!frozen && !m) seg(false, Icon::Check, vtypeShort(f.type), col::faint);
        if (!f.buildId.empty() && f.buildId != build) seg(false, Icon::Check, "Otra versión del juego", col::red);
    }
};

// ------------------------------------------------------------------ página principal

class RootPage : public Page {
public:
    RootPage() { resetSelection(); }

    // Otro juego (o se reinició): cada pestaña empieza desde arriba.
    void resetSelection() {
        m_values.reset(new ValuesTab());
        m_search.reset(makeSearchTab());
    }

    bool input(Panel& panel, const PanelInput& in) override {
        if (in.down & BTN_B) return false;   // cerrar el overlay
        if (!panel.game()) {
            if ((in.down & BTN_A) || in.tap) panel.show(panel.now());   // reintentar
            return true;
        }
        // Pestañas: con la cruceta (izquierda / derecha) o tocándolas.
        if (in.down & BTN_LEFT) {
            panel.setTab(Panel::TabValues);
            return true;
        }
        if (in.down & BTN_RIGHT) {
            panel.setTab(Panel::TabSearch);
            return true;
        }
        if (in.tap && in.tapY >= kTabY && in.tapY < kTabY + kTabH) {
            panel.setTab(in.tapX < Panel::kW / 2 ? Panel::TabValues : Panel::TabSearch);
            return true;
        }
        return current(panel).input(panel, in);
    }

    void update(Panel& panel) override { current(panel).update(panel); }

    void draw(Panel& panel, Painter& p) override {
        if (!panel.game()) {
            drawEmpty(p, Icon::Game, col::faint, "No hay ningún juego abierto",
                      "Abre un juego y vuelve a abrir ValorNX desde el menú de overlays.", panel.error());
            return;
        }
        drawTabs(panel, p);
        current(panel).draw(panel, p);
    }

    void hints(Panel& panel, std::vector<Hint>& out) override {
        if (!panel.game()) {
            out.push_back({"B", "Salir"});
            out.push_back({"A", "Reintentar"});
            return;
        }
        current(panel).hints(panel, out);
    }

private:
    static constexpr int kTabY = Panel::kContentY, kTabH = 40;
    std::unique_ptr<Page> m_values, m_search;

    Page& current(Panel& panel) { return panel.tab() == Panel::TabSearch ? *m_search : *m_values; }

    void drawTabs(Panel& panel, Painter& p) {
        const int tab = panel.tab();
        for (int i = 0; i < 2; i++) {
            const int x = i == 0 ? 12 : 228, w = 208;
            const bool on = tab == i;
            roundRect(p, x, kTabY, w, kTabH, 10, on ? withAlpha(col::accent, 60) : col::keyBg);
            if (on) roundRectOutline(p, x, kTabY, w, kTabH, 10, 2, col::accent);
            const std::string label = i == 0 ? "Mis valores" : "Buscar";
            const int tw = p.measure(label, 20);
            const int sx = x + (w - (22 + 8 + tw)) / 2;
            icon(p, i == 0 ? Icon::Star : Icon::Search, sx, kTabY + 9, 22,
                 on ? (i == 0 ? col::gold : col::accent) : col::faint);
            p.text(sx + 30, kTabY + 8, label, 20, on ? col::text : col::dim);
        }
        // Flechita hacia la otra pestaña: se cambia con la cruceta.
        if (tab == Panel::TabValues)
            icon(p, Icon::Right, 228 + 208 - 24, kTabY + 13, 14, col::dim);
        else
            icon(p, Icon::Left, 12 + 10, kTabY + 13, 14, col::dim);
    }
};

}  // namespace

// ------------------------------------------------------------------ multiplicador (x2, x3...)

void openFactorMenu(Panel& panel, const Favorite& f) {
    std::vector<MenuItemO> items = {{Icon::Up, col::gold, "x2", "El doble"},
                                    {Icon::Up, col::gold, "x3", "El triple"},
                                    {Icon::Up, col::gold, "x5", ""},
                                    {Icon::Up, col::gold, "x10", ""}};
    panel.push(new MenuPage("¿Por cuánto multiplicar?",
                            "Cada vez que \"" + f.name + "\" suba, la subida se multiplica. Dura hasta que cierres el "
                            "juego.",
                            items, [f](Panel& pn, int k) {
                                static const uint32_t kFactors[] = {2, 3, 5, 10};
                                if (k < 0 || k > 3) return;
                                // Si ya estaba guardado como truco, el nuevo también se guarda.
                                pn.report(pn.ctl().activateMultiplier(f, kFactors[k], false, true));
                                pn.readValuesNow();
                            }));
}

// ------------------------------------------------------------------ Panel

Panel::Panel(PanelEnv env) : m_env(std::move(env)), m_favs(joinPath(dataDir(), "guardados")) {
    m_pages.emplace_back(new RootPage());
}

Panel::~Panel() = default;

std::string Panel::dataDir() const { return joinPath(m_env.sdRoot, "switch/ValorNX"); }

void Panel::resetPages() {
    while (m_pages.size() > 1) {
        m_trash.push_back(std::move(m_pages.back()));
        m_pages.pop_back();
    }
}

void Panel::show(uint64_t now) {
    m_now = now;
    hide();   // suelta lo anterior (y cancela una búsqueda que se quedó a medias)
    resetPages();
    m_toast.clear();
    std::string err;
    IGame* g = m_env.connect ? m_env.connect(err) : nullptr;
    m_error = err;
    m_live.clear();
    m_liveOk.clear();
    if (!g) {
        m_favs.items().clear();
        m_gameTitle.clear();
        m_session.reset();
        m_interrupted = false;
        return;
    }
    m_game = g;
    const ProcessInfo& pi = g->info();
    if (pi.titleId != m_tid || pi.processId != m_pid)
        static_cast<RootPage*>(m_pages.front().get())->resetSelection();   // otro juego (o se reinició)
    m_tid = pi.titleId;
    m_pid = pi.processId;
    auto it = m_names.find(m_tid);
    if (it == m_names.end()) it = m_names.emplace(m_tid, m_env.gameName ? m_env.gameName(m_tid) : "").first;
    m_gameTitle = it->second.empty() ? "Juego " + pi.titleIdHex() : it->second;
    m_favs.load(m_tid);   // puede haber valores nuevos guardados desde la app
    // La búsqueda es la misma que la de la app (se sigue desde cualquiera de los dos).
    loadConfig(joinPath(dataDir(), "config.txt"), m_cfg);
    m_session.reset(new SearchSession(joinPath(dataDir(), "busqueda"), true));
    m_session->open(pi);
    m_ctl.attach(g, m_env.sdRoot);
    m_lastRefresh = now;
    readValuesNow();
    if (m_interrupted) {
        m_interrupted = false;
        toast("La búsqueda se canceló porque se cerró el overlay antes de terminar.", true);
    }
}

void Panel::hide() {
    // Primero lo que usa el juego (una búsqueda a medias), después soltar el juego.
    for (std::unique_ptr<Page>& pg : m_pages) pg->abort(*this);
    m_ctl.attach(nullptr, m_env.sdRoot);
    if (m_game) {
        m_game = nullptr;
        if (m_env.disconnect) m_env.disconnect();
    }
}

bool Panel::busy() const { return !m_pages.empty() && m_pages.back()->working(); }

void Panel::saveConfig() { ::vnx::saveConfig(joinPath(dataDir(), "config.txt"), m_cfg); }

void Panel::runTask(std::unique_ptr<Task> task, std::function<void(Panel&, Task&)> done) {
    if (!task) return;
    if (task->finished()) {   // falló al empezar
        if (done) done(*this, *task);
        return;
    }
    push(makeTaskPage(std::move(task), std::move(done), m_now));
}

uint64_t Panel::addressOf(const Favorite& f) const {
    return m_game ? FavoriteStore::address(f, m_game->info()) : 0;
}

bool Panel::liveValue(size_t i, uint64_t& raw) const {
    if (i >= m_live.size() || !m_liveOk[i]) return false;
    raw = m_live[i];
    return true;
}

void Panel::readValuesNow() {
    m_lastRead = m_now ? m_now : 1;
    const std::vector<Favorite>& items = m_favs.items();
    m_live.assign(items.size(), 0);
    m_liveOk.assign(items.size(), 0);
    if (!m_game) return;
    for (size_t i = 0; i < items.size(); i++) {
        uint64_t raw = 0;
        m_liveOk[i] = m_ctl.readValue(addressOf(items[i]), items[i].type, raw) ? 1 : 0;
        m_live[i] = raw;
    }
}

void Panel::toast(const std::string& msg, bool error) {
    m_toast = msg;
    m_toastErr = error;
    m_toastUntil = m_now + (error ? 4000 : 2600);
}

void Panel::report(const OpResult& r) {
    if (!r.title.empty())
        message(r.title, r.text);
    else if (!r.text.empty())
        toast(r.text, !r.ok);
}

void Panel::message(const std::string& title, const std::string& text) { push(new MessagePage(title, text)); }

void Panel::push(Page* p) { m_pages.emplace_back(p); }

void Panel::pop() {
    if (m_pages.size() <= 1) return;   // la página principal no se quita nunca
    m_trash.push_back(std::move(m_pages.back()));
    m_pages.pop_back();
}

void Panel::popToRoot() { resetPages(); }

bool Panel::input(const PanelInput& in, uint64_t now) {
    m_now = now;
    m_trash.clear();
    PanelInput x = in;
    // Repetición al mantener una dirección (como en la app).
    const uint32_t dirs = in.held & kDirs;
    if (in.down & kDirs) {
        m_repeatSince = now;
        m_lastRepeat = now;
        m_repeating = true;
    }
    if (!dirs) {
        m_repeating = false;
    } else if (m_repeating && now - m_repeatSince >= 380 && now - m_lastRepeat >= 75) {
        x.down |= dirs;
        m_lastRepeat = now;
    }
    if (m_pages.empty()) return false;
    return m_pages.back()->input(*this, x);
}

void Panel::update(uint64_t now) {
    m_now = now;
    m_trash.clear();
    if (!m_game) return;
    const bool working = busy();
    if (now - m_lastRefresh >= 2000) {
        m_lastRefresh = now;
        if (!m_game->stillAttached()) {
            hide();
            m_error = "El juego se cerró.";
            m_interrupted = false;
            resetPages();
            return;
        }
        if (!working) m_ctl.refresh();   // por si se cambió algo desde la app, EdiZon o Breeze
    }
    if (!working && now - m_lastRead >= 250) readValuesNow();
    if (!m_pages.empty()) m_pages.back()->update(*this);
}

void Panel::draw(Painter& p) {
    p.clear(col::bg);
    p.rect(kW - 1, 0, 1, kH, col::edge);
    drawHeader(p);
    if (!m_pages.empty()) m_pages.back()->draw(*this, p);
    drawToast(p);
    drawFooter(p);
}

void Panel::drawHeader(Painter& p) {
    p.text(20, 14, "ValorNX", 30, col::text);
    const int tw = p.measure("ValorNX", 30);
    // Lo que Atmosphère está aplicando ahora mismo
    if (m_game) {
        int x = 20 + tw + 16;
        const size_t fr = m_ctl.frozenCount(), mu = m_ctl.activeMultipliers();
        if (fr) {
            icon(p, Icon::Snowflake, x, 25, 18, col::ice);
            x += 22 + p.text(x + 22, 22, std::to_string(fr), 19, col::ice) + 14;
        }
        if (mu) {
            icon(p, Icon::Up, x, 25, 18, col::gold);
            p.text(x + 22, 22, std::to_string(mu), 19, col::gold);
        }
    }
    text(p, 20, 56, m_game ? m_gameTitle : "Busca y cambia valores sin salir del juego", 18, col::dim, Align::Left,
         408);
    p.rect(20, 86, 408, 1, col::line);
}

void Panel::drawFooter(Painter& p) {
    p.rect(15, kFooterY, kW - 30, 1, col::text);
    if (m_pages.empty()) return;
    std::vector<Hint> hints;
    m_pages.back()->hints(*this, hints);
    int x = 24;
    const int y = 670;
    for (const Hint& h : hints) {
        circle(p, x + 13, y + 13, 13, col::text);
        text(p, x + 13, y + 3, h.btn, 18, col::dark, Align::Center);
        x += 30;
        x += p.text(x, y + 1, h.label, 21, col::text) + 18;
    }
}

void Panel::drawToast(Painter& p) {
    if (m_toast.empty() || m_now >= m_toastUntil) return;
    std::vector<std::string> lines = wrap(p, m_toast, 17, 356);
    if (lines.size() > 3) lines.resize(3);
    const int h = 18 + (int)lines.size() * 23;
    const int y = kContentBottom - h - 2;
    roundRect(p, 12, y, 424, h, 12, m_toastErr ? rgba(92, 30, 38, 238) : rgba(14, 60, 72, 238));
    icon(p, m_toastErr ? Icon::Warning : Icon::Check, 26, y + (h - 18) / 2, 18, m_toastErr ? col::red : col::accent);
    int ty = y + 9;
    for (const std::string& l : lines) {
        p.text(56, ty, l, 17, col::text);
        ty += 23;
    }
}

}  // namespace ovl
}  // namespace vnx
