#include "app.hpp"

#include <algorithm>
#include <cstdio>

#include "../core/cheats.hpp"
#include "../core/fsutil.hpp"

namespace vnx {

MenuItem menuItem(const std::string& label, const std::string& sub) {
    MenuItem m;
    m.label = label;
    m.sub = sub;
    return m;
}

MenuItem menuItem(Icon icon, Color color, const std::string& label, const std::string& sub) {
    MenuItem m;
    m.label = label;
    m.sub = sub;
    m.hasIcon = true;
    m.icon = icon;
    m.color = color;
    return m;
}

// ------------------------------------------------------------------ ListView

int ListView::update(const Input& in, int count, bool wrap) {
    if (count <= 0) {
        sel = 0;
        scroll = 0;
        return -1;
    }
    sel = std::max(0, std::min(sel, count - 1));
    bool moved = false;
    if (in.down & BTN_UP) {
        if (sel > 0)
            sel--;
        else if (wrap)
            sel = count - 1;
        moved = true;
    }
    if (in.down & BTN_DOWN) {
        if (sel < count - 1)
            sel++;
        else if (wrap)
            sel = 0;
        moved = true;
    }
    if (moved) ensureVisible(count);
    if (in.touching && in.dragY != 0 && hitRect(in.touchX, in.touchY, x, y, w, h)) {
        scroll -= in.dragY;
        clampScroll(count);
    }
    if (in.tap && hitRect(in.tapX, in.tapY, x, y, w, h)) {
        for (int i = 0; i < count; i++) {
            const int ry = rowY(i);
            if (in.tapY >= ry && in.tapY < ry + rowH) {
                sel = i;
                ensureVisible(count);
                return i;
            }
        }
    }
    if (in.down & BTN_A) return sel;
    return -1;
}

void ListView::clampScroll(int count) {
    const int content = count * (rowH + gap) - gap;
    const int maxS = std::max(0, content - h);
    scroll = std::max(0, std::min(scroll, maxS));
}

void ListView::ensureVisible(int count) {
    const int top = sel * (rowH + gap);
    if (top < scroll) scroll = top;
    if (top + rowH > scroll + h) scroll = top + rowH - h;
    clampScroll(count);
}

void ListView::drawScrollbar(Canvas& c, int count) const {
    const int content = count * (rowH + gap) - gap;
    if (content <= h) return;
    const int tx = x + w + 12;
    const int barH = std::max(40, h * h / content);
    const int barY = y + (int)((int64_t)(h - barH) * scroll / (content - h));
    c.fillRoundRect(tx, y, 6, h, 3, theme::card);
    c.fillRoundRect(tx, barY, 6, barH, 3, theme::faint);
}

// ------------------------------------------------------------------ App

App::App(Platform& p, Font& f) : platform(p), font(f) {}
App::~App() = default;

void App::start() {
    now = nowMs();
    dataDir = joinPath(platform.sdRoot(), "switch/ValorNX");
    makeDirs(dataDir);
    loadConfig(joinPath(dataDir, "config.txt"), cfg);
    connect();
}

void App::connect() {
    popAll();
    ctl.attach(nullptr, platform.sdRoot());   // el juego anterior deja de existir al reconectar
    std::string err;
    game = platform.connectGame(err);
    gameError = err;
    if (!game) {
        session.reset();
        favs.reset();
        push(makeNoGameScreen());
        return;
    }
    session.reset(new SearchSession(joinPath(dataDir, "busqueda")));
    const bool resumed = session->open(game->info());
    favs.reset(new FavoriteStore(joinPath(dataDir, "guardados")));
    favs->load(game->info().titleId);
    ctl.attach(game, platform.sdRoot());
    m_lastCheck = now;
    push(makeHomeScreen());
    if (resumed) toast("Se recuperó tu búsqueda anterior.");
}

void App::push(Screen* s) { m_stack.emplace_back(s); }

void App::pop() {
    if (m_stack.empty()) return;
    m_trash.push_back(std::move(m_stack.back()));
    m_stack.pop_back();
}

void App::popAll() {
    while (!m_stack.empty()) pop();
}

void App::frame(const Input& in, Canvas& c) {
    now = nowMs();
    m_trash.clear();

    if (m_task) {
        const bool tapCancel = in.tap && hitRect(in.tapX, in.tapY, kScreenW / 2 - 120, 420, 240, 54);
        if ((in.down & BTN_B) || tapCancel) m_task->cancel();
        m_task->step(45);
        if (m_task->finished()) {
            std::unique_ptr<Task> t = std::move(m_task);
            std::function<void(Task&)> cb = std::move(m_taskDone);
            m_taskDone = nullptr;
            platform.keepAwake(false);
            if (cb) cb(*t);
        }
    } else if (!m_stack.empty()) {
        m_stack.back()->update(*this, in);
    }

    // De vez en cuando: ¿sigue abierto el juego? ¿qué valores están congelados?
    if (game && !m_task && now - m_lastCheck > 2500) {
        m_lastCheck = now;
        if (!game->stillAttached()) {
            game = nullptr;
            gameError = "El juego se cerró. Ábrelo otra vez y pulsa Reintentar.";
            popAll();
            session.reset();
            favs.reset();
            ctl.attach(nullptr, platform.sdRoot());
            push(makeNoGameScreen());
        } else {
            ctl.refresh();   // por si se cambió algo desde el overlay, EdiZon o Breeze
            // La búsqueda y "Mis valores" también se pueden cambiar desde el overlay.
            if (session) session->sync();
            if (favs) favs->sync();
        }
    }

    c.clear(theme::bg);
    drawHeader(c);
    int first = (int)m_stack.size() - 1;
    while (first > 0 && m_stack[first]->modal()) first--;
    for (int i = std::max(first, 0); i < (int)m_stack.size(); i++) {
        if (m_stack[i]->modal()) c.fillRect(0, 0, kScreenW, kScreenH, rgba(0, 0, 0, 150));
        m_stack[i]->draw(*this, c);
    }
    std::vector<Hint> hints;
    if (m_task) {
        drawTask(c);
        hints.push_back({"B", "Cancelar"});
    } else if (!m_stack.empty()) {
        m_stack.back()->hints(*this, hints);
    }
    const int hintsX = drawFooter(c, hints);
    drawToast(c, hintsX - 28);
}

// ------------------------------------------------------------------ diálogos

void App::toast(const std::string& msg, bool error) {
    m_toast = msg;
    m_toastErr = error;
    m_toastUntil = now + (error ? 4000 : 2800);
}

void App::message(const std::string& title, const std::string& text, Icon icon) {
    push(makeMessageScreen(title, text, icon));
}

void App::confirm(const std::string& title, const std::string& text, const std::string& yes,
                  std::function<void()> onYes) {
    menu(title, text, {menuItem(Icon::Check, theme::accent, yes), menuItem(Icon::Close, theme::dim, "Cancelar")},
         [onYes](int i) {
             if (i == 0 && onYes) onYes();
         });
}

void App::menu(const std::string& title, const std::string& sub, const std::vector<MenuItem>& items,
               std::function<void(int)> onPick, int initial) {
    push(makeMenuScreen(title, sub, items, std::move(onPick), initial));
}

void App::keypad(const KeypadSpec& spec, std::function<void(const std::string&, int)> onOk) {
    push(makeKeypadScreen(spec, std::move(onOk)));
}

void App::rangeKeypad(const KeypadSpec& spec,
                      std::function<void(const std::string&, const std::string&, int)> onOk) {
    push(makeRangeKeypadScreen(spec, std::move(onOk)));
}

void App::runTask(std::unique_ptr<Task> task, std::function<void(Task&)> done) {
    if (!task) return;
    if (task->finished()) {
        if (done) done(*task);
        return;
    }
    m_task = std::move(task);
    m_taskDone = std::move(done);
    m_taskStart = now;
    platform.keepAwake(true);
}

// ------------------------------------------------------------------ juego

void App::toggleFreeze(uint64_t addr, VType t) {
    if (game) report(ctl.toggleFreeze(addr, t));
}

void App::report(const OpResult& r) {
    if (!r.title.empty())
        message(r.title, r.text, Icon::Warning);
    else if (!r.text.empty())
        toast(r.text, !r.ok);
}

void App::saveConfig() { ::vnx::saveConfig(joinPath(dataDir, "config.txt"), cfg); }

// ------------------------------------------------------------------ acciones compartidas

static std::function<void(Task&)> searchStarted(App& app, bool hidden, bool range = false) {
    return [&app, hidden, range](Task& t) {
        if (t.status() == Task::Status::Done) {
            app.openSearch();
            if (hidden) {
                app.toast("Memoria guardada. Ahora haz que el valor cambie en el juego.");
            } else {
                const uint64_t n = t.found();
                if (n == 0)
                    app.toast(range ? "No se encontró ningún número en ese rango." : "No se encontró ese número.", true);
                else if (n == 1)
                    app.toast("¡Se encontró 1 resultado!");
                else
                    app.toast("Se encontraron " + formatCount(n) + " resultados.");
            }
        } else if (t.status() == Task::Status::Failed) {
            app.message(hidden ? "No se pudo guardar la memoria" : "No se pudo buscar", t.error(), Icon::Warning);
        } else {
            app.toast("Búsqueda cancelada.");
        }
    };
}

void App::beginKnownSearch() {
    App* self = this;
    auto go = [self]() {
        KeypadSpec k;
        k.title = "¿Qué número ves en el juego?";
        k.subtitle = "Escríbelo exactamente como aparece. Por ejemplo, tu dinero: 1500.";
        k.search = true;
        k.typeChoice = self->cfg.typeChoice;
        self->keypad(k, [self](const std::string& text, int tc) {
            self->cfg.typeChoice = tc;
            self->saveConfig();
            const Number n = parseNumber(text);
            self->runTask(self->session->startKnown(*self->game, typeChoice(tc).mask, n, text, self->cfg.zone),
                          searchStarted(*self, false));
        });
    };
    if (session->active())
        confirm("¿Empezar una búsqueda nueva?", "Se borrará la búsqueda que tienes en curso.", "Sí, empezar de nuevo", go);
    else
        go();
}

void App::beginRangeSearch() {
    App* self = this;
    auto go = [self]() {
        KeypadSpec k;
        k.title = "¿Entre qué números está?";
        k.subtitle = "Para cuando no ves el número exacto. Por ejemplo, si la vida está entre 20 y 100.";
        k.search = true;
        k.range = true;
        k.rejectZero = true;
        k.typeChoice = self->cfg.typeChoice;
        self->rangeKeypad(k, [self](const std::string& lo, const std::string& hi, int tc) {
            self->cfg.typeChoice = tc;
            self->saveConfig();
            const Number a = parseNumber(lo), b = parseNumber(hi);
            self->runTask(self->session->startRange(*self->game, typeChoice(tc).mask, a, b,
                                                    "entre " + lo + " y " + hi, self->cfg.zone),
                          searchStarted(*self, false, true));
        });
    };
    if (session->active())
        confirm("¿Empezar una búsqueda nueva?", "Se borrará la búsqueda que tienes en curso.", "Sí, empezar de nuevo", go);
    else
        go();
}

void App::beginHiddenSearch() {
    App* self = this;
    auto go = [self]() {
        std::vector<MenuItem> items;
        for (int i = 0; i < typeChoiceCount(); i++) items.push_back(menuItem(typeChoice(i).label, typeChoice(i).help));
        items[0].label = "Automático (recomendado)";
        self->menu("¿Qué tipo de valor buscas?",
                   "Se guardará una copia de la memoria del juego en la microSD. Puede tardar un poco.", items,
                   [self](int i) {
                       self->runTask(self->session->startHidden(*self->game, typeChoice(i).mask, self->cfg.zone),
                                     searchStarted(*self, true));
                   });
    };
    if (session->active())
        confirm("¿Empezar una búsqueda nueva?", "Se borrará la búsqueda que tienes en curso.", "Sí, empezar de nuevo", go);
    else
        go();
}

void App::openSearch() { push(makeSearchScreen()); }

void App::pickName(const std::string& title, std::function<void(const std::string&)> onName) {
    static const char* kPresets[] = {"Dinero", "Vida", "Vida máxima", "Experiencia", "Nivel",
                                     "Magia / Maná", "Munición", "Objetos", "Puntos", "Tiempo"};
    const int count = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
    std::vector<MenuItem> items;
    items.push_back(menuItem(Icon::Pencil, theme::accent, "Escribir otro nombre…", "Abre el teclado."));
    for (int i = 0; i < count; i++) items.push_back(menuItem(kPresets[i]));
    App* self = this;
    menu(title, "", items, [self, onName, count](int i) {
        if (i >= 1 && i <= count) {
            onName(kPresets[i - 1]);
            return;
        }
        std::string out;
        if (self->platform.keyboard("Nombre del valor", "", out) && !out.empty())
            onName(out);
        else
            self->toast("No se escribió ningún nombre.", true);
    });
}

void App::saveFavorite(uint64_t addr, VType t, std::function<void(const Favorite&)> after) {
    if (!game || !favs) return;
    const ProcessInfo& pi = game->info();
    Favorite f;
    uint64_t off = 0;
    f.region = pi.classify(addr, &off);
    f.offset = off;
    f.type = t;
    f.buildId = pi.buildIdHex();
    favs->sync();   // por si se guardó algo desde el overlay
    const int existing = favs->find(f.region, f.offset, f.type);
    if (existing >= 0) {
        const Favorite ex = favs->items()[existing];
        toast("Ya estaba guardado como \"" + ex.name + "\".");
        if (after) after(ex);
        return;
    }
    App* self = this;
    pickName("¿Cómo quieres llamar a este valor?", [self, f, after](const std::string& name) mutable {
        if (!self->favs) return;
        self->favs->sync();
        f.name = self->favs->uniqueName(name);
        self->favs->items().push_back(f);
        if (!self->favs->save()) {
            self->toast("No se pudo guardar en la microSD.", true);
            return;
        }
        self->toast("Guardado en Mis valores: " + f.name + ".");
        if (after) after(f);
    });
}

// ------------------------------------------------------------------ multiplicador

void App::openMultiplier(const Favorite& f) {
    if (!game) return;
    if (!multiplierSupported(f.type)) {
        std::string title, body;
        multiplierUnsupported(f.type, f.name, title, body);
        message(title, body, Icon::Warning);
        return;
    }
    ctl.refreshCheats();
    App* self = this;
    const ActiveMultiplier* m = multiplierFor(f);
    if (!m) {
        pickFactor(f, [self, f](uint32_t factor) { self->confirmMultiplier(f, factor); });
        return;
    }
    const std::string factor = std::to_string(m->spec.factor);
    const bool enabled = m->enabled;
    menu("x" + factor + " en " + f.name,
         enabled ? "Activo: cada subida de \"" + f.name + "\" se multiplica por " + factor + "."
                 : "En pausa: ahora mismo no multiplica nada.",
         {menuItem(Icon::Up, theme::gold, "Cambiar el multiplicador"),
          menuItem(enabled ? Icon::Pause : Icon::Play, theme::accent, enabled ? "Pausar" : "Reanudar"),
          menuItem(Icon::Trash, theme::red, "Quitar el multiplicador")},
         [self, f](int k) {
             if (k == 0)
                 self->pickFactor(f, [self, f](uint32_t fac) { self->confirmMultiplier(f, fac); });
             else if (k == 1)
                 self->report(self->ctl.toggleMultiplier(f));
             else if (k == 2)
                 self->removeMultiplier(f);
         });
}

void App::pickFactor(const Favorite& f, std::function<void(uint32_t)> onFactor) {
    App* self = this;
    menu("¿Por cuánto multiplicar lo que ganes?",
         "Cada vez que \"" + f.name + "\" suba, la subida se multiplicará. Si baja (por ejemplo, al gastar), no se toca.",
         {menuItem("x2", "El doble"), menuItem("x3", "El triple"), menuItem("x5"), menuItem("x10"),
          menuItem(Icon::Pencil, theme::accent, "Otro número…")},
         [self, onFactor](int k) {
             static const uint32_t kFactors[] = {2, 3, 5, 10};
             if (k >= 0 && k < 4) {
                 onFactor(kFactors[k]);
                 return;
             }
             KeypadSpec ks;
             ks.search = false;
             ks.editType = VType::U16;
             ks.title = "Multiplicar por…";
             ks.subtitle = "Escribe un número entero entre 2 y 1000.";
             self->keypad(ks, [self, onFactor](const std::string& text, int) {
                 const Number n = parseNumber(text);
                 if (!n.valid || n.isDecimal || n.i < (int64_t)kMinFactor || n.i > (int64_t)kMaxFactor) {
                     self->toast("El multiplicador debe ser un número entre 2 y 1000.", true);
                     return;
                 }
                 onFactor((uint32_t)n.i);
             });
         });
}

void App::confirmMultiplier(const Favorite& f, uint32_t factor) {
    App* self = this;
    // Los valores fuera de "main" pueden cambiar de sitio al reiniciar el juego: se avisa.
    const std::string saveSub = f.region == RegionKind::Main
                                    ? "Además se cargará cada vez que abras el juego."
                                    : "Se cargará al abrir el juego, si el valor sigue en el mismo sitio.";
    menu("x" + std::to_string(factor) + " en " + f.name,
         "Actívalo cuando ya estés dentro de tu partida (no en la pantalla de título). Sigue funcionando aunque "
         "cierres ValorNX, hasta que cierres el juego.",
         {menuItem(Icon::Check, theme::accent, "Activar ahora"),
          menuItem(Icon::Bolt, theme::gold, "Activar y guardar como truco", saveSub),
          menuItem(Icon::Close, theme::dim, "Cancelar")},
         [self, f, factor](int k) {
             if (k == 0) self->report(self->ctl.activateMultiplier(f, factor, false));
             if (k == 1) self->report(self->ctl.activateMultiplier(f, factor, true));
         });
}

void App::removeMultiplier(const Favorite& f, bool quiet) {
    if (!game) return;
    const OpResult r = ctl.removeMultiplier(f);
    if (!quiet) report(r);
}

void App::editValue(const std::string& title, uint64_t addr, VType t, std::function<void(uint64_t)> after) {
    uint64_t cur = 0;
    const bool ok = readValue(addr, t, cur);
    KeypadSpec k;
    k.search = false;
    k.editType = t;
    k.title = title;
    k.subtitle = "Escribe el nuevo valor y pulsa OK.";
    k.initial = ok ? formatRaw(t, cur) : "";
    if (k.initial.find_first_not_of("-0123456789.") != std::string::npos) k.initial.clear();
    k.infoLine = ok ? "Valor actual: " + formatRaw(t, cur) : "No se pudo leer el valor actual.";
    k.quickValues = true;
    App* self = this;
    keypad(k, [self, addr, t, after](const std::string& text, int) {
        uint64_t raw = 0;
        if (!encodeNumber(t, parseNumber(text), raw)) {
            self->toast("Ese número no es válido.", true);
            return;
        }
        if (self->setValue(addr, t, raw)) {
            self->toast("Listo: ahora vale " + formatRaw(t, raw) + ".");
            if (after) after(raw);
        } else {
            self->toast("No se pudo escribir en la memoria del juego.", true);
        }
    });
}

// ------------------------------------------------------------------ dibujo común

int App::buttonBadge(Canvas& c, int x, int y, const std::string& btn) {
    if (btn.size() == 1 && btn[0] >= 'A' && btn[0] <= 'Z') {
        c.fillCircle(x + 14, y + 15, 14, theme::text);
        c.text(x + 14, y + 3, btn, 19, theme::bar, Align::Center);
        return 28;
    }
    const int tw = c.measure(btn, 18);
    const int w = tw + 20;
    c.fillRoundRect(x, y + 1, w, 28, 14, theme::text);
    c.text(x + w / 2, y + 4, btn, 18, theme::bar, Align::Center);
    return w;
}

int App::hintWidth(Canvas& c, const Hint& h) {
    int bw = (h.btn.size() == 1 && h.btn[0] >= 'A' && h.btn[0] <= 'Z') ? 28 : c.measure(h.btn, 18) + 20;
    return bw + 8 + c.measure(h.label, 22);
}

void App::drawHeader(Canvas& c) {
    c.fillRect(0, 0, kScreenW, kHeaderH, theme::bar);
    c.fillRect(0, kHeaderH - 1, kScreenW, 1, theme::line);
    if (game) {
        const ProcessInfo& pi = game->info();
        int tx = 106;
        if (!pi.icon.empty() && pi.iconSize > 0) {
            c.blitImage(24, 12, pi.iconSize, pi.iconSize, pi.icon.data());
            tx = 24 + pi.iconSize + 18;
        } else {
            c.fillRoundRect(24, 12, 64, 64, 12, theme::card);
            drawIcon(c, Icon::Game, 34, 22, 44, theme::dim);
        }
        const std::string name = pi.gameName.empty() ? "Juego " + pi.titleIdHex() : pi.gameName;
        c.text(tx, 12, name, 28, theme::text, Align::Left, 800);
        c.text(tx, 52, "ID " + pi.titleIdHex() + "   ·   Build " + pi.buildIdHex(), 18, theme::dim, Align::Left, 800);
    } else {
        drawIcon(c, Icon::Search, 28, 22, 44, theme::accent);
        c.text(88, 24, kAppName, 32, theme::text);
    }
    c.text(kScreenW - 28, 12, kAppName, 26, theme::accent, Align::Right);
    // Estado de lo que Atmosphère está aplicando ahora mismo (de derecha a izquierda).
    struct Seg {
        Icon icon;
        Color color;
        std::string text;
    };
    std::vector<Seg> segs;
    const size_t mults = activeMultipliers();
    if (mults) segs.push_back({Icon::Up, theme::gold, mults == 1 ? "1 multiplicador" : formatCount(mults) + " multiplicadores"});
    const size_t frozen = ctl.frozenCount();
    if (frozen)
        segs.push_back({Icon::Snowflake, theme::ice, frozen == 1 ? "1 congelado" : formatCount(frozen) + " congelados"});
    if (segs.empty()) {
        c.text(kScreenW - 28, 52, std::string("v") + kAppVersion, 18, theme::faint, Align::Right);
    } else {
        int right = kScreenW - 28;
        for (const Seg& s : segs) {
            const int w = c.measure(s.text, 18);
            c.text(right, 52, s.text, 18, s.color, Align::Right);
            drawIcon(c, s.icon, right - w - 26, 52, 20, s.color);
            right -= w + 26 + 22;
        }
    }
}

int App::drawFooter(Canvas& c, const std::vector<Hint>& hints) {
    c.fillRect(0, kFooterY, kScreenW, kScreenH - kFooterY, theme::bg);
    c.fillRect(28, kFooterY, kScreenW - 56, 1, theme::line);
    int total = 0;
    for (const Hint& h : hints) total += hintWidth(c, h) + 34;
    const int start = hints.empty() ? kScreenW - 36 : kScreenW - 36 - (total - 34);
    int x = start;
    const int y = kFooterY + 18;
    for (const Hint& h : hints) {
        const int bw = buttonBadge(c, x, y, h.btn);
        c.text(x + bw + 8, y + 1, h.label, 22, theme::text);
        x += hintWidth(c, h) + 34;
    }
    return start;
}

void App::drawTask(Canvas& c) {
    c.fillRect(0, 0, kScreenW, kScreenH, rgba(0, 0, 0, 160));
    const int w = 760, h = 300, x = (kScreenW - w) / 2, y = 190;
    c.fillRoundRect(x, y, w, h, 22, theme::card);
    c.text(x + w / 2, y + 30, m_task->title(), 30, theme::text, Align::Center, w - 60);
    float p = m_task->progress();
    p = p < 0 ? 0 : (p > 1 ? 1 : p);
    const int bx = x + 50, by = y + 96, bw = w - 100;
    c.fillRoundRect(bx, by, bw, 24, 12, theme::bar);
    const int fw = (int)((float)bw * p);
    if (fw > 0) c.fillRoundRect(bx, by, std::max(fw, 24), 24, 12, theme::accent);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d%%", (int)(p * 100.0f));
    c.text(x + w / 2, by + 36, buf, 26, theme::text, Align::Center);
    const uint64_t secs = (now - m_taskStart) / 1000;
    std::string info = m_task->foundIsBytes() ? "Guardado: " + formatBytes(m_task->found())
                                              : "Resultados: " + formatCount(m_task->found());
    info += "   ·   " + std::to_string((unsigned long long)secs) + " s";
    c.text(x + w / 2, by + 78, info, 22, theme::dim, Align::Center);
    // Botón cancelar (también se puede tocar)
    const int cx = kScreenW / 2 - 120, cy = 420;
    c.fillRoundRect(cx, cy, 240, 54, 27, theme::cardHi);
    const int bwid = buttonBadge(c, cx + 52, cy + 12, "B");
    c.text(cx + 52 + bwid + 10, cy + 13, "Cancelar", 24, theme::text);
}

// Los avisos salen en la barra inferior, a la izquierda de las pistas de botones,
// para no tapar nunca el contenido.
void App::drawToast(Canvas& c, int maxRight) {
    if (m_toast.empty() || now >= m_toastUntil) return;
    const uint64_t left = m_toastUntil - now;
    const float fade = left < 300 ? (float)left / 300.0f : 1.0f;
    const int x = 24, y = kFooterY + 10;
    const int maxW = maxRight - x;
    if (maxW < 160) return;
    const std::string msg = c.ellipsize(m_toast, 21, maxW - 76);
    const int w = std::min(maxW, c.measure(msg, 21) + 76);
    const Color bgc = m_toastErr ? rgba(0x5C, 0x1F, 0x26) : rgba(0x0F, 0x3C, 0x46);
    c.fillRoundRect(x, y, w, 44, 22, withAlpha(bgc, (uint32_t)(245 * fade)));
    drawIcon(c, m_toastErr ? Icon::Warning : Icon::Check, x + 20, y + 11, 22,
             withAlpha(m_toastErr ? theme::red : theme::accent, (uint32_t)(255 * fade)));
    c.text(x + 54, y + 9, msg, 21, withAlpha(theme::text, (uint32_t)(255 * fade)));
}

}  // namespace vnx
