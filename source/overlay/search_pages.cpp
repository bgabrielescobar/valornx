// ValorNX (overlay) - buscar sin salir del juego: pestaña "Buscar", barra de progreso y
// resultados. Usa la misma búsqueda que la app (guardada en la microSD), con búferes
// pequeños porque el overlay tiene muy poca memoria.
#include <algorithm>

#include "../core/multiplier.hpp"
#include "pages.hpp"

namespace vnx {
namespace ovl {

namespace {

constexpr size_t kListMax = 100;      // resultados que se muestran en la lista
constexpr uint32_t kBudgetMs = 40;    // trabajo por fotograma mientras busca

TaskDone startDone(bool hidden, bool range) {
    return [hidden, range](Panel& pn, Task& t) {
        if (t.status() == Task::Status::Done) {
            if (hidden) {
                pn.toast("Memoria guardada. Cierra el overlay, haz que el valor cambie y vuelve a abrirlo.");
                return;
            }
            const uint64_t n = t.found();
            if (n == 0)
                pn.toast(range ? "No se encontró ningún número en ese rango." : "No se encontró ese número.", true);
            else if (n == 1)
                pn.toast("¡Se encontró 1 resultado!");
            else
                pn.toast("Se encontraron " + formatCount(n) + " resultados.");
        } else if (t.status() == Task::Status::Failed) {
            pn.message(hidden ? "No se pudo guardar la memoria" : "No se pudo buscar", t.error());
        } else {
            pn.toast("Búsqueda cancelada.");
        }
    };
}

TaskDone refineDone() {
    return [](Panel& pn, Task& t) {
        if (t.status() == Task::Status::Done) {
            const uint64_t n = t.found();
            if (n == 0)
                pn.toast("No quedó ningún resultado.", true);
            else if (n == 1)
                pn.toast("¡Queda 1 resultado!");
            else
                pn.toast("Quedan " + formatCount(n) + " resultados.");
        } else if (t.status() == Task::Status::Failed) {
            pn.message("No se pudo continuar la búsqueda", t.error());
        } else {
            pn.toast("Cancelado.");
        }
    };
}

// ------------------------------------------------------------------ progreso

class TaskPage : public Page {
public:
    TaskPage(std::unique_ptr<Task> task, TaskDone done, uint64_t now)
        : m_task(std::move(task)), m_done(std::move(done)), m_start(now) {}

    bool input(Panel&, const PanelInput& in) override {
        if (!m_task) return true;
        if ((in.down & BTN_B) || (in.tap && hitRect(in.tapX, in.tapY, kBtnX, kBtnY, kBtnW, kBtnH))) m_task->cancel();
        return true;
    }

    void update(Panel& panel) override {
        if (!m_task) {
            panel.pop();
            return;
        }
        m_task->step(kBudgetMs);
        if (!m_task->finished()) return;
        std::unique_ptr<Task> t = std::move(m_task);
        TaskDone done = std::move(m_done);
        panel.pop();
        if (done) done(panel, *t);
    }

    void abort(Panel& panel) override {
        if (!m_task) return;
        if (!m_task->finished()) {
            m_task->cancel();
            m_task->step(0);   // borra lo que había escrito a medias
            panel.noteInterrupted();
        }
        m_task.reset();
    }

    bool working() const override { return (bool)m_task; }

    void draw(Panel& panel, Painter& p) override {
        const Task* t = m_task.get();
        if (!t) return;
        const int top = Panel::kContentY + 34;
        icon(p, Icon::Search, 20, top + 2, 30, col::accent);
        int y = top;
        for (const std::string& l : wrap(p, t->title(), 22, 360)) {
            p.text(62, y, l, 22, col::text);
            y += 29;
        }
        float pr = t->progress();
        pr = pr < 0 ? 0 : (pr > 1 ? 1 : pr);
        const int by = 236;
        roundRect(p, 20, by, 408, 24, 12, col::keyBg);
        const int fw = (int)(408.0f * pr);
        if (fw > 0) roundRect(p, 20, by, std::max(fw, 24), 24, 12, col::accent);
        text(p, Panel::kW / 2, by + 36, std::to_string((int)(pr * 100.0f)) + "%", 26, col::text, Align::Center);
        const uint64_t secs = (panel.now() - m_start) / 1000;
        std::string info = t->foundIsBytes() ? "Guardado: " + formatBytes(t->found())
                                             : "Resultados: " + formatCount(t->found());
        info += "  ·  " + std::to_string((unsigned long long)secs) + " s";
        text(p, Panel::kW / 2, by + 76, info, 18, col::dim, Align::Center, 408);
        const std::string note =
            t->foundIsBytes()
                ? "Se guarda una copia de la memoria del juego en la microSD: puede tardar un poco. No cierres "
                  "el overlay hasta que termine."
                : "No cierres el overlay hasta que termine: si lo cierras, la búsqueda se cancela.";
        int ny = by + 128;
        for (const std::string& l : wrap(p, note, 17, 390)) {
            text(p, Panel::kW / 2, ny, l, 17, col::faint, Align::Center);
            ny += 23;
        }
        roundRect(p, kBtnX, kBtnY, kBtnW, kBtnH, kBtnH / 2, col::keySel);
        const int lw = 30 + p.measure("Cancelar", 21);
        const int lx = kBtnX + (kBtnW - lw) / 2;
        circle(p, lx + 13, kBtnY + kBtnH / 2, 13, col::text);
        text(p, lx + 13, kBtnY + kBtnH / 2 - 10, "B", 18, col::dark, Align::Center);
        p.text(lx + 30, kBtnY + kBtnH / 2 - 12, "Cancelar", 21, col::text);
    }

    void hints(Panel&, std::vector<Hint>& out) override { out.push_back({"B", "Cancelar"}); }

private:
    static constexpr int kBtnX = 124, kBtnY = 500, kBtnW = 200, kBtnH = 48;
    std::unique_ptr<Task> m_task;
    TaskDone m_done;
    uint64_t m_start = 0;
};

// ------------------------------------------------------------------ resultados

class ResultsPage : public Page {
public:
    ResultsPage() {
        m_list.y = kListY;
        m_list.h = Panel::kContentBottom - kListY;
        m_list.rowH = 62;
    }

    bool input(Panel& panel, const PanelInput& in) override {
        if (!valid(panel)) {
            panel.pop();
            return true;
        }
        if (in.down & BTN_B) {
            panel.pop();
            return true;
        }
        const int n = (int)m_c.size();
        if (n == 0) return true;
        const int tapped = m_list.handle(in, n);
        const int i = m_list.sel;
        if (tapped >= 0 || (in.down & BTN_A)) {
            options(panel, i);
        } else if (in.down & BTN_Y) {
            panel.report(panel.ctl().toggleFreeze(candAddr(m_c[i]), candType(m_c[i])));
        } else if (in.down & BTN_X) {
            editAll(panel);
        }
        return true;
    }

    void update(Panel& panel) override {
        if (!valid(panel)) return;
        if (m_lastRead != 0 && panel.now() - m_lastRead < 300) return;
        m_lastRead = panel.now() ? panel.now() : 1;
        for (int i = 0; i < (int)m_c.size(); i++) {
            if (!m_list.visible(i)) continue;
            uint64_t raw = 0;
            m_ok[i] = panel.ctl().readValue(candAddr(m_c[i]), candType(m_c[i]), raw) ? 1 : 0;
            m_live[i] = raw;
        }
    }

    void draw(Panel& panel, Painter& p) override {
        SearchSession* s = panel.session();
        if (!s || !panel.game()) return;
        const uint64_t total = s->state().cur.count;
        text(p, 20, Panel::kContentY + 2, "Resultados", 24, col::text);
        const std::string right = total > m_c.size() ? formatCount(m_c.size()) + " de " + formatCount(total)
                                                     : formatCount(total) + (total == 1 ? " resultado" : " resultados");
        text(p, 428, Panel::kContentY + 9, right, 16, col::dim, Align::Right);
        if (m_c.empty()) {
            text(p, Panel::kW / 2, 300, "No hay resultados que mostrar.", 19, col::dim, Align::Center);
            return;
        }
        const ProcessInfo& pi = panel.game()->info();
        p.pushClip(0, m_list.y, Panel::kW, m_list.h);
        for (int i = 0; i < (int)m_c.size(); i++) {
            if (!m_list.visible(i)) continue;
            const Candidate& cd = m_c[i];
            const uint64_t addr = candAddr(cd);
            const VType t = candType(cd);
            const int y = m_list.rowY(i);
            const int rh = m_list.rowH - 4;
            if (i == m_list.sel) {
                roundRect(p, 10, y, 428, rh, 10, col::selBg);
                roundRectOutline(p, 10, y, 428, rh, 10, 2, col::accent);
            } else if (i > 0 && i - 1 != m_list.sel) {
                p.rect(24, y - 2, 400, 1, col::line);
            }
            p.text(22, y + 9, std::to_string(i + 1), 15, col::faint);
            const bool ok = m_ok[i] != 0;
            text(p, 62, y + 5, ok ? formatRaw(t, m_live[i]) : "?", 24, ok ? col::gold : col::faint, Align::Left, 230);
            text(p, 424, y + 9, vtypeShort(t), 16, col::dim, Align::Right);
            text(p, 62, y + 36, pi.describe(addr), 15, col::faint, Align::Left, 230);
            if (panel.ctl().isFrozen(addr)) {
                const int w = p.measure("Congelado", 15);
                icon(p, Icon::Snowflake, 424 - w - 20, y + 37, 15, col::ice);
                p.text(424 - w, y + 36, "Congelado", 15, col::ice);
            }
        }
        p.popClip();
        m_list.drawScrollbar(p, (int)m_c.size());
    }

    void hints(Panel&, std::vector<Hint>& out) override {
        out.push_back({"B", "Volver"});
        if (m_c.empty()) return;
        out.push_back({"A", "Opciones"});
        out.push_back({"Y", "Congelar"});
        out.push_back({"X", "Todos"});
    }

private:
    static constexpr int kListY = Panel::kContentY + 40;
    std::vector<Candidate> m_c;
    std::vector<uint64_t> m_live;
    std::vector<uint8_t> m_ok;
    uint32_t m_gen = 0xFFFFFFFFu;
    uint64_t m_lastRead = 0;
    Scroller m_list;

    // Carga la lista si cambió; false si ya no hay resultados que ver.
    bool valid(Panel& panel) {
        SearchSession* s = panel.session();
        if (!s || !panel.game() || s->state().cur.stage != Stage::Candidates) return false;
        if (s->state().cur.gen != m_gen) {
            s->loadCandidates(m_c, kListMax);
            m_live.assign(m_c.size(), 0);
            m_ok.assign(m_c.size(), 0);
            m_gen = s->state().cur.gen;
            m_lastRead = 0;
            m_list.sel = std::min(m_list.sel, std::max(0, (int)m_c.size() - 1));
            m_list.clamp((int)m_c.size());
        }
        return true;
    }

    void options(Panel& panel, int i) {
        const Candidate cd = m_c[i];
        const uint64_t addr = candAddr(cd);
        const VType t = candType(cd);
        const bool frozen = panel.ctl().isFrozen(addr);
        enum Act { EDIT, FREEZE, SAVE, MULTIPLY, REMOVE };
        std::vector<MenuItemO> items = {
            {Icon::Pencil, col::accent, "Cambiar valor", ""},
            {Icon::Snowflake, col::ice, frozen ? "Descongelar" : "Congelar",
             frozen ? "El juego podrá volver a cambiarlo." : "El juego ya no podrá cambiarlo."},
            {Icon::Star, col::gold, "Guardar en Mis valores", "Para tenerlo a mano la próxima vez."}};
        std::vector<Act> acts = {EDIT, FREEZE, SAVE};
        if (multiplierSupported(t)) {
            items.push_back({Icon::Up, col::gold, "Multiplicar lo que ganes", "Se guarda en Mis valores."});
            acts.push_back(MULTIPLY);
        }
        items.push_back({Icon::Trash, col::red, "Quitar de la lista", ""});
        acts.push_back(REMOVE);
        const std::string sub = std::string(vtypeLabel(t)) + "  ·  Valor actual: " +
                                (m_ok[i] ? formatRaw(t, m_live[i]) : std::string("?"));
        ResultsPage* self = this;
        panel.push(new MenuPage(panel.game()->info().describe(addr), sub, items,
                                [self, acts, cd, addr, t](Panel& pn, int k) {
                                    if (k < 0 || k >= (int)acts.size()) return;
                                    switch (acts[k]) {
                                        case EDIT: editValue(pn, "Cambiar valor", "", addr, t); break;
                                        case FREEZE: pn.report(pn.ctl().toggleFreeze(addr, t)); break;
                                        case SAVE: saveFavorite(pn, addr, t); break;
                                        case MULTIPLY:
                                            saveFavorite(pn, addr, t,
                                                         [](Panel& p2, const Favorite& f) { openFactorMenu(p2, f); });
                                            break;
                                        case REMOVE:
                                            if (pn.session() && pn.session()->removeCandidates({cd}))
                                                pn.toast("Quitado de la lista.");
                                            else
                                                pn.toast("No se pudo quitar.", true);
                                            break;
                                    }
                                    self->m_lastRead = 0;
                                }));
    }

    void editAll(Panel& panel) {
        bool anyFloat = false, allFloat = true;
        for (const Candidate& cd : m_c) {
            const bool f = vtypeIsFloat(candType(cd));
            anyFloat = anyFloat || f;
            allFloat = allFloat && f;
        }
        KeypadSpec k;
        k.editType = anyFloat ? VType::F32 : candType(m_c[0]);
        if (allFloat) k.editType = candType(m_c[0]);
        k.title = m_c.size() == 1 ? "Cambiar el resultado" : "Cambiar los " + std::to_string(m_c.size()) + " resultados";
        k.info = "Se escribe el mismo número en todos.";
        k.quick = true;
        const std::vector<Candidate> list = m_c;
        ResultsPage* self = this;
        panel.push(new KeypadPage(k, [self, list](Panel& pn, const std::string& text, const std::string&, int) {
            const Number n = parseNumber(text);
            int ok = 0, fail = 0;
            for (const Candidate& cd : list) {
                uint64_t raw = 0;
                if (encodeNumber(candType(cd), n, raw) && pn.ctl().setValue(candAddr(cd), candType(cd), raw))
                    ok++;
                else
                    fail++;
            }
            self->m_lastRead = 0;
            if (fail == 0)
                pn.toast(ok == 1 ? "Listo: se cambió el valor." : "Listo: se cambiaron " + std::to_string(ok) + " valores.");
            else
                pn.toast("Se cambiaron " + std::to_string(ok) + " de " + std::to_string(ok + fail) + " valores.", ok == 0);
            pn.readValuesNow();
        }));
    }
};

// ------------------------------------------------------------------ pestaña "Buscar"

class SearchTab : public Page {
public:
    bool input(Panel& panel, const PanelInput& in) override {
        if (!panel.session()) return true;
        build(panel);
        const int tapped = m_list.handle(in, (int)m_opts.size());
        const int pick = tapped >= 0 ? tapped : ((in.down & BTN_A) ? m_list.sel : -1);
        if (pick >= 0 && pick < (int)m_opts.size()) run(panel, m_opts[pick].action);
        return true;
    }

    void draw(Panel& panel, Painter& p) override {
        SearchSession* s = panel.session();
        if (!s) return;
        build(panel);
        if (s->active()) drawStatus(p, s->state());
        p.pushClip(0, m_list.y, Panel::kW, m_list.h);
        for (int i = 0; i < (int)m_opts.size(); i++) {
            if (!m_list.visible(i)) continue;
            const Opt& o = m_opts[i];
            const int ry = m_list.rowY(i);
            const int rh = m_list.rowH - 6;
            if (i == m_list.sel) {
                roundRect(p, 12, ry, 424, rh, 10, col::selBg);
                roundRectOutline(p, 12, ry, 424, rh, 10, 2, col::accent);
            } else {
                roundRect(p, 12, ry, 424, rh, 10, col::keyBg);
            }
            const int is = o.sub.empty() ? 24 : 26;
            icon(p, o.icon, 28, ry + (rh - is) / 2, is, o.color);
            if (o.sub.empty()) {
                text(p, 66, ry + (rh - 25) / 2, o.label, 20, col::text, Align::Left, 356);
            } else {
                text(p, 68, ry + 8, o.label, 21, col::text, Align::Left, 354);
                text(p, 68, ry + 38, o.sub, 16, col::dim, Align::Left, 354);
            }
        }
        p.popClip();
        m_list.drawScrollbar(p, (int)m_opts.size());
        if (!s->active()) {
            int y = m_list.y + (int)m_opts.size() * m_list.rowH + 14;
            for (const std::string& l :
                 wrap(p,
                      "La búsqueda se guarda sola. Entre un paso y otro, cierra el overlay, juega para que el valor "
                      "cambie y vuelve a abrirlo: te espera aquí.",
                      17, 404)) {
                p.text(22, y, l, 17, col::dim);
                y += 23;
            }
        }
    }

    void hints(Panel&, std::vector<Hint>& out) override {
        out.push_back({"B", "Salir"});
        out.push_back({"A", "Elegir"});
    }

private:
    enum Action { A_KNOWN, A_RANGE_NEW, A_HIDDEN, A_VIEW, A_EQUAL, A_RANGE, A_INC, A_DEC, A_CHANGED, A_UNCHANGED,
                  A_UNDO, A_RESET };
    struct Opt {
        Icon icon;
        Color color;
        std::string label, sub;
        Action action;
    };
    static constexpr int kTop = 146, kStatusH = 108;
    std::vector<Opt> m_opts;
    Scroller m_list;
    bool m_seenActive = false;
    uint32_t m_seenGen = 0xFFFFFFFFu;
    Stage m_seenStage = Stage::Empty;

    void build(Panel& panel) {
        SearchSession* s = panel.session();
        m_opts.clear();
        const bool active = s->active();
        if (!active) {
            m_opts.push_back({Icon::Search, col::accent, "Buscar un valor que ves", "Escribe el número que sale en pantalla.",
                              A_KNOWN});
            m_opts.push_back({Icon::Range, col::purple, "Buscar entre dos números", "Si no sabes el número exacto.",
                              A_RANGE_NEW});
            m_opts.push_back({Icon::Heart, col::pink, "Buscar un valor oculto", "Barras de vida sin número.", A_HIDDEN});
            m_list.y = kTop;
            m_list.rowH = 74;
            m_list.h = (int)m_opts.size() * m_list.rowH;
        } else {
            const SessionState& st = s->state();
            const uint64_t n = st.cur.count;
            // Si se empezó sin saber el número (oculto o entre dos números), lo normal es seguir
            // con "aumentó / disminuyó"; si se sabía, con el número nuevo.
            const bool unknown = st.hidden || st.range;
            const Opt equal{Icon::Search, col::accent, unknown ? "Ahora veo el número" : "Buscar el nuevo valor", "",
                            A_EQUAL};
            const Opt between{Icon::Range, col::purple, "Está entre dos números", "", A_RANGE};
            if (st.cur.stage == Stage::Snapshot) {
                m_opts.push_back({Icon::Down, col::pink, "Disminuyó", "", A_DEC});
                m_opts.push_back({Icon::Up, col::green, "Aumentó", "", A_INC});
                m_opts.push_back({Icon::NotEqual, col::purple, "Cambió (no sé si subió o bajó)", "", A_CHANGED});
                m_opts.push_back(equal);
                m_opts.push_back(between);
            } else if (n > 0) {
                const bool few = n <= kListMax;
                if (few) m_opts.push_back({Icon::List, col::gold, "Ver resultados (" + formatCount(n) + ")", "", A_VIEW});
                if (!unknown) m_opts.push_back(equal);
                m_opts.push_back({Icon::Down, col::pink, "Disminuyó", "", A_DEC});
                m_opts.push_back({Icon::Up, col::green, "Aumentó", "", A_INC});
                m_opts.push_back({Icon::NotEqual, col::purple, "Cambió", "", A_CHANGED});
                m_opts.push_back({Icon::Equal, col::dim, "No cambió", "", A_UNCHANGED});
                m_opts.push_back(between);
                if (unknown) m_opts.push_back(equal);
                if (!few) m_opts.push_back({Icon::List, col::gold, "Ver los primeros 100", "", A_VIEW});
            }
            if (s->canUndo()) m_opts.push_back({Icon::Undo, col::dim, "Deshacer el último paso", "", A_UNDO});
            m_opts.push_back({Icon::Close, col::red, "Empezar de nuevo", "", A_RESET});
            m_list.y = kTop + kStatusH;
            m_list.rowH = 52;
            m_list.h = Panel::kContentBottom - m_list.y;
        }
        const SessionState& st = s->state();
        if (active != m_seenActive || st.cur.gen != m_seenGen || st.cur.stage != m_seenStage) {
            m_seenActive = active;
            m_seenGen = st.cur.gen;
            m_seenStage = st.cur.stage;
            m_list.sel = 0;
            m_list.scroll = 0;
        }
        m_list.clamp((int)m_opts.size());
    }

    static std::string guidance(const SessionState& st) {
        const uint64_t n = st.cur.count;
        if (st.cur.stage == Stage::Snapshot)
            return "Cierra el overlay, haz que el valor cambie (recibe daño, gasta energía…) y vuelve para elegir "
                   "qué pasó.";
        if (n == 0) return "No quedó nada: puede que el tipo o el número no fueran exactos. Deshaz o empieza de nuevo.";
        if (n == 1) return "¡Lo encontraste! Ábrelo en \"Ver resultados\" para cambiarlo o guardarlo.";
        if (n <= kListMax) return "Quedan pocos. Míralos: si hay varios, cambia uno y mira cuál es el bueno.";
        if (st.hidden || st.range)
            return "Cierra el overlay, haz que el valor cambie y elige si aumentó o disminuyó.";
        return "Cierra el overlay, haz que el número cambie en el juego y busca el nuevo valor.";
    }

    void drawStatus(Painter& p, const SessionState& st) {
        const char* kind = st.hidden ? "VALOR OCULTO" : (st.range ? "ENTRE DOS NÚMEROS" : "BÚSQUEDA EN CURSO");
        p.text(20, kTop, kind, 15, col::dim);
        text(p, 428, kTop, "Paso " + std::to_string(st.cur.steps), 15, col::dim, Align::Right);
        int x = 20;
        if (st.cur.stage == Stage::Snapshot) {
            x += p.text(x, kTop + 20, formatBytes(st.cur.count), 30, col::accent);
            p.text(x + 8, kTop + 30, "de memoria guardada", 18, col::dim);
        } else {
            const uint64_t n = st.cur.count;
            x += p.text(x, kTop + 20, formatCount(n), 30, n == 0 ? col::red : col::accent);
            p.text(x + 8, kTop + 30, n == 1 ? "resultado" : "resultados", 18, col::dim);
            if (!st.cur.lastInput.empty())
                text(p, 428, kTop + 30, "Último: " + st.cur.lastInput, 16, col::faint, Align::Right, 170);
        }
        std::vector<std::string> lines = wrap(p, guidance(st), 16, 408);
        if (lines.size() > 2) lines.resize(2);
        int y = kTop + 60;
        for (const std::string& l : lines) {
            p.text(20, y, l, 16, col::dim);
            y += 21;
        }
    }

    void run(Panel& panel, Action a) {
        SearchSession* s = panel.session();
        if (!s || !panel.game()) return;
        switch (a) {
            case A_KNOWN: {
                KeypadSpec k;
                k.title = "¿Qué número ves?";
                k.info = "Escríbelo tal como aparece en el juego.";
                k.search = true;
                k.typeChoice = panel.cfg().typeChoice;
                panel.push(new KeypadPage(k, [](Panel& pn, const std::string& text, const std::string&, int tc) {
                    if (!pn.session() || !pn.game()) return;
                    pn.cfg().typeChoice = tc;
                    pn.saveConfig();
                    pn.runTask(pn.session()->startKnown(*pn.game(), typeChoice(tc).mask, parseNumber(text), text,
                                                        pn.cfg().zone),
                               startDone(false, false));
                }));
                break;
            }
            case A_RANGE_NEW: {
                KeypadSpec k;
                k.title = "¿Entre qué números está?";
                k.info = "Por ejemplo, la vida está entre 20 y 100.";
                k.search = true;
                k.range = true;
                k.rejectZero = true;
                k.typeChoice = panel.cfg().typeChoice;
                panel.push(new KeypadPage(k, [](Panel& pn, const std::string& lo, const std::string& hi, int tc) {
                    if (!pn.session() || !pn.game()) return;
                    pn.cfg().typeChoice = tc;
                    pn.saveConfig();
                    pn.runTask(pn.session()->startRange(*pn.game(), typeChoice(tc).mask, parseNumber(lo),
                                                        parseNumber(hi), "entre " + lo + " y " + hi, pn.cfg().zone),
                               startDone(false, true));
                }));
                break;
            }
            case A_HIDDEN: {
                std::vector<MenuItemO> items;
                for (int i = 0; i < typeChoiceCount(); i++)
                    items.push_back({Icon::Check, i == 0 ? col::accent : col::dim,
                                     i == 0 ? "Automático (recomendado)" : typeChoice(i).label, shortTypeHelp(i)});
                panel.push(new MenuPage("¿Qué tipo de valor buscas?",
                                        "Se guarda una copia de la memoria del juego en la microSD. Puede tardar un "
                                        "poco.",
                                        items, [](Panel& pn, int i) {
                                            if (!pn.session() || !pn.game()) return;
                                            pn.runTask(pn.session()->startHidden(*pn.game(), typeChoice(i).mask,
                                                                                 pn.cfg().zone),
                                                       startDone(true, false));
                                        }));
                break;
            }
            case A_VIEW: panel.push(new ResultsPage()); break;
            case A_EQUAL: {
                KeypadSpec k;
                k.title = "¿Qué número ves ahora?";
                k.info = "Tipo: " + typeMaskLabel(s->state().types);
                k.search = true;
                k.lockedMask = s->state().types;
                panel.push(new KeypadPage(k, [](Panel& pn, const std::string& text, const std::string&, int) {
                    if (!pn.session() || !pn.game()) return;
                    const Number n = parseNumber(text);
                    pn.runTask(pn.session()->refine(*pn.game(), Relation::Equal, &n, text), refineDone());
                }));
                break;
            }
            case A_RANGE: {
                KeypadSpec k;
                k.title = "¿Entre qué números está ahora?";
                k.info = "Tipo: " + typeMaskLabel(s->state().types);
                k.search = true;
                k.range = true;
                k.lockedMask = s->state().types;
                panel.push(new KeypadPage(k, [](Panel& pn, const std::string& lo, const std::string& hi, int) {
                    if (!pn.session() || !pn.game()) return;
                    const Number a = parseNumber(lo), b = parseNumber(hi);
                    pn.runTask(pn.session()->refine(*pn.game(), Relation::Between, &a, "entre " + lo + " y " + hi, &b),
                               refineDone());
                }));
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
                panel.runTask(s->refine(*panel.game(), rel, nullptr, relationLabel(rel)), refineDone());
                break;
            }
            case A_UNDO:
                if (s->undo()) panel.toast("Se deshizo el último paso.");
                break;
            case A_RESET:
                confirm(panel, "¿Empezar de nuevo?", "Se borrará esta búsqueda (también en la app).", "Sí, borrarla",
                        [](Panel& pn) {
                            if (!pn.session()) return;
                            pn.session()->reset();
                            pn.toast("Búsqueda borrada.");
                        });
                break;
        }
    }
};

}  // namespace

Page* makeSearchTab() { return new SearchTab(); }

Page* makeTaskPage(std::unique_ptr<Task> task, TaskDone done, uint64_t now) {
    return new TaskPage(std::move(task), std::move(done), now);
}

void editValue(Panel& panel, const std::string& title, const std::string& name, uint64_t addr, VType t) {
    KeypadSpec k;
    k.title = title;
    k.editType = t;
    k.quick = true;
    uint64_t raw = 0;
    if (panel.ctl().readValue(addr, t, raw)) {
        const std::string cur = formatRaw(t, raw);
        k.info = "Ahora vale " + cur;
        if (cur.find_first_not_of("-0123456789.") == std::string::npos) k.initial = cur;
    }
    panel.push(new KeypadPage(k, [name, addr, t](Panel& pn, const std::string& text, const std::string&, int) {
        uint64_t v = 0;
        encodeNumber(t, parseNumber(text), v);
        if (pn.ctl().setValue(addr, t, v))
            pn.toast((name.empty() ? std::string("Listo") : name) + ": ahora vale " + formatRaw(t, v) + ".");
        else
            pn.toast("No se pudo escribir en la memoria del juego.", true);
        pn.readValuesNow();
    }));
}

void saveFavorite(Panel& panel, uint64_t addr, VType t, std::function<void(Panel&, const Favorite&)> after) {
    if (!panel.game()) return;
    const ProcessInfo& pi = panel.game()->info();
    Favorite f;
    uint64_t off = 0;
    f.region = pi.classify(addr, &off);
    f.offset = off;
    f.type = t;
    f.buildId = pi.buildIdHex();
    panel.favStore().sync();   // por si se cambió desde la app
    panel.readValuesNow();
    const int existing = panel.findFav(f.region, f.offset, f.type);
    if (existing >= 0) {
        const Favorite ex = panel.favs()[existing];
        panel.toast("Ya estaba guardado como \"" + ex.name + "\".");
        if (after) after(panel, ex);
        return;
    }
    static const char* kNames[] = {"Dinero", "Vida",   "Vida máxima", "Experiencia", "Nivel", "Magia / Maná",
                                   "Munición", "Objetos", "Puntos",    "Tiempo",      "Valor"};
    const int count = (int)(sizeof(kNames) / sizeof(kNames[0]));
    std::vector<MenuItemO> items;
    for (int i = 0; i < count; i++) items.push_back({Icon::Star, col::gold, kNames[i], ""});
    panel.push(new MenuPage("¿Cómo se llama este valor?", "Luego puedes ponerle otro nombre desde la app.", items,
                            [f, after, count](Panel& pn, int k) mutable {
                                if (k < 0 || k >= count) return;
                                FavoriteStore& store = pn.favStore();
                                store.sync();
                                f.name = store.uniqueName(kNames[k]);
                                store.items().push_back(f);
                                if (!store.save()) {
                                    store.items().pop_back();
                                    pn.toast("No se pudo guardar en la microSD.", true);
                                    return;
                                }
                                pn.readValuesNow();
                                pn.toast("Guardado en Mis valores: " + f.name + ".");
                                if (after) after(pn, f);
                            }));
}

}  // namespace ovl
}  // namespace vnx
