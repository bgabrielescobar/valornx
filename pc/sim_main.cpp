// Simulador de ValorNX para PC: ejecuta la interfaz real con un juego simulado,
// siguiendo un guion de pulsaciones, y guarda capturas PNG de la pantalla.
//
//   sim <fuente.ttf> <guion.txt> <carpeta_capturas>
//
// Órdenes del guion (una por línea):
//   press A|B|X|Y|L|R|PLUS|MINUS|UP|DOWN|LEFT|RIGHT [veces]
//   tap X Y             toque en la pantalla
//   drag X Y DY         arrastrar el dedo DY píxeles
//   type 1500           escribir con teclado
//   wait N              N frames sin tocar nada
//   waittask            esperar a que termine la búsqueda en curso
//   shot nombre         guardar captura nombre.png
//   game spend N | damage X | noise N | tick | set_money N
//   expect_money N      comprobar el dinero del juego simulado
//   expect_frozen_money 0|1
//   nogame 0|1          simular que no hay juego abierto
//   reconnect           volver a conectar con el juego
//   kbd texto           respuesta del teclado del sistema
//   addfav money|exp|hp|level|items Nombre   guardar un valor en "Mis valores"
//
// Overlay (el mismo panel que en la Switch, dibujado con un emulador de libtesla):
//   ovl show | hide     abrir / cerrar el overlay
//   ovl press BOTÓN [veces] | ovl tap X Y | ovl type 99999 | ovl wait N
//   ovl shot nombre     captura del overlay encima de un juego inventado
//   ovl expect_closed 0|1   ¿el último botón cerró el overlay?
//   ovl waittask        esperar a que termine la búsqueda del overlay
//   ovl expect_busy 0|1 ¿está buscando ahora mismo?
//
// Memoria (para comprobar que el overlay cabe en los 4 MB de nx-ovlloader):
//   mem reset           empezar a medir desde aquí
//   mem report [máx_KB] memoria máxima pedida desde "mem reset" (falla si pasa de máx_KB)
#include <malloc.h>
#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../source/core/fsutil.hpp"
#include "../source/core/favorites.hpp"
#include "../source/overlay/panel.hpp"
#include "../source/overlay/tesla_painter.hpp"
#include "../source/ui/app.hpp"
#include "mock_game.hpp"
#include "tesla_emu.hpp"

using namespace vnx;

// Memoria pedida con new (vectores, textos, páginas...): la actual y el máximo.
static size_t g_memCur = 0, g_memPeak = 0;
static void* countedAlloc(size_t n) {
    void* p = malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    g_memCur += malloc_usable_size(p);
    if (g_memCur > g_memPeak) g_memPeak = g_memCur;
    return p;
}
static void countedFree(void* p) {
    if (!p) return;
    g_memCur -= malloc_usable_size(p);
    free(p);
}
void* operator new(size_t n) { return countedAlloc(n); }
void* operator new[](size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, size_t) noexcept { countedFree(p); }
void operator delete[](void* p, size_t) noexcept { countedFree(p); }

static uint64_t g_clock = 1000;
namespace vnx {
uint64_t nowMs() { return g_clock++; }   // reloj virtual: avanza 1 ms en cada consulta
}

class SimPlatform : public Platform {
public:
    std::unique_ptr<MockGame> game;
    bool noGame = false;
    std::string kbdText = "Mi valor";
    std::string root;

    std::string sdRoot() const override { return root; }
    IGame* connectGame(std::string& error) override {
        error.clear();
        if (noGame) return nullptr;
        if (!game) game.reset(new MockGame());
        return game.get();
    }
    bool keyboard(const std::string& title, const std::string& initial, std::string& out) override {
        printf("    [teclado] \"%s\" -> \"%s\"\n", title.c_str(), kbdText.c_str());
        out = kbdText;
        return true;
    }
};

static void writePng(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        for (int x = 0; x < w; x++) {
            uint32_t p = px[(size_t)y * w + x];
            raw.push_back(p & 0xFF);
            raw.push_back((p >> 8) & 0xFF);
            raw.push_back((p >> 16) & 0xFF);
        }
    }
    uLongf clen = compressBound(raw.size());
    std::vector<uint8_t> comp(clen);
    compress2(comp.data(), &clen, raw.data(), raw.size(), 6);
    comp.resize(clen);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    auto be32 = [&](uint32_t v) {
        uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
        fwrite(b, 1, 4, f);
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32((uint32_t)data.size());
        fwrite(type, 1, 4, f);
        if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
        uLong crc = crc32(0, (const Bytef*)type, 4);
        if (!data.empty()) crc = crc32(crc, data.data(), (uInt)data.size());
        be32((uint32_t)crc);
    };
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                                 (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
                                 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", comp);
    chunk("IEND", {});
    fclose(f);
}

// "Pantalla del juego" inventada, para ver el overlay encima de algo.
static void drawBackdrop(std::vector<uint32_t>& px, Font& font) {
    Canvas c(px.data(), kScreenW, kScreenH, kScreenW, font);
    for (int y = 0; y < kScreenH; y++)
        c.fillRect(0, y, kScreenW, 1, mixColor(rgba(38, 84, 150), rgba(150, 200, 235), (float)y / 480.0f));
    c.fillCircle(1110, 130, 58, rgba(255, 226, 130));
    c.fillCircle(360, 900, 520, rgba(66, 138, 72));
    c.fillCircle(1000, 860, 440, rgba(58, 124, 66));
    c.fillRect(0, 600, kScreenW, 120, rgba(52, 110, 58));
    c.fillRoundRect(790, 26, 460, 96, 18, rgba(0, 0, 0, 110));
    c.text(816, 36, "Aventura de Prueba", 28, rgba(255, 255, 255));
    c.text(816, 78, "(pantalla del juego simulada)", 19, rgba(225, 230, 240));
}

static uint32_t parseBtn(const std::string& s) {
    if (s == "A") return BTN_A;
    if (s == "B") return BTN_B;
    if (s == "X") return BTN_X;
    if (s == "Y") return BTN_Y;
    if (s == "L") return BTN_L;
    if (s == "R") return BTN_R;
    if (s == "ZL") return BTN_ZL;
    if (s == "ZR") return BTN_ZR;
    if (s == "PLUS") return BTN_PLUS;
    if (s == "MINUS") return BTN_MINUS;
    if (s == "UP") return BTN_UP;
    if (s == "DOWN") return BTN_DOWN;
    if (s == "LEFT") return BTN_LEFT;
    if (s == "RIGHT") return BTN_RIGHT;
    fprintf(stderr, "botón desconocido: %s\n", s.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "uso: sim <fuente.ttf> <guion.txt> <carpeta_capturas>\n");
        return 2;
    }
    std::string fontData;
    if (!readFile(argv[1], fontData)) {
        fprintf(stderr, "no se pudo leer la fuente %s\n", argv[1]);
        return 2;
    }
    Font font;
    font.addFace(fontData.data(), fontData.size());
    const std::string shots = argv[3];
    makeDirs(shots);

    SimPlatform platform;
    platform.root = "sim_sd/";
    makeDirs(platform.root);

    const int W = kScreenW, H = kScreenH;
    std::vector<uint32_t> fb((size_t)W * H, 0);
    App app(platform, font);
    app.start();

    // Overlay: el mismo panel que en la Switch, dibujado con el emulador de libtesla.
    ovl::PanelEnv env;
    env.sdRoot = platform.root;
    env.connect = [&platform](std::string& err) -> IGame* { return platform.connectGame(err); };
    env.disconnect = []() {};
    env.gameName = [](uint64_t) { return std::string("Aventura de Prueba"); };
    ovl::Panel panel(env);
    TeslaEmu emu(fontData);
    std::vector<uint32_t> backdrop((size_t)W * H, 0);
    drawBackdrop(backdrop, font);
    bool ovlUsed = true;   // lo que devolvió el panel con el último botón
    auto ovlFrame = [&](const ovl::PanelInput* in) {
        g_clock += 16;
        if (in) {
            const bool used = panel.input(*in, nowMs());
            if (in->down) ovlUsed = used;
        }
        panel.update(nowMs());
        ovl::TeslaPainter<TeslaEmu, EmuColor> painter(&emu, emu.ascentRatio());
        panel.draw(painter);
        if (platform.game) platform.game->tick();
    };

    int failures = 0;
    uint64_t frames = 0;
    auto frame = [&](const Input& in) {
        Canvas c(fb.data(), W, H, W, font);
        app.frame(in, c);
        if (platform.game) platform.game->tick();
        frames++;
    };
    frame(Input());

    std::ifstream script(argv[2]);
    std::string line;
    int lineNo = 0;
    while (std::getline(script, line)) {
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd == "press") {
            std::string b;
            int times = 1;
            ss >> b >> times;
            if (times <= 0) times = 1;
            for (int i = 0; i < times; i++) {
                Input in;
                in.down = in.held = parseBtn(b);
                frame(in);
                frame(Input());
            }
        } else if (cmd == "tap") {
            Input in;
            in.tap = true;
            ss >> in.tapX >> in.tapY;
            frame(in);
            frame(Input());
        } else if (cmd == "drag") {
            int x, y, dy;
            ss >> x >> y >> dy;
            const int steps = 10;
            for (int i = 1; i <= steps; i++) {
                Input in;
                in.touching = true;
                in.touchX = x;
                in.touchY = y + dy * i / steps;
                in.dragY = dy / steps;
                frame(in);
            }
            frame(Input());
        } else if (cmd == "type") {
            std::string text;
            ss >> text;
            for (char ch : text) {
                Input in;
                in.typed = ch;
                frame(in);
            }
        } else if (cmd == "wait") {
            int n = 1;
            ss >> n;
            for (int i = 0; i < n; i++) frame(Input());
        } else if (cmd == "waittask") {
            int guard = 0;
            while (app.busy() && guard++ < 100000) frame(Input());
            frame(Input());
        } else if (cmd == "shot") {
            std::string name;
            ss >> name;
            const size_t peak = g_memPeak;
            writePng(joinPath(shots, name + ".png"), fb, W, H);
            g_memPeak = peak;
            printf("  captura: %s.png\n", name.c_str());
        } else if (cmd == "game") {
            std::string what;
            ss >> what;
            MockGame* g = platform.game.get();
            if (!g) continue;
            if (what == "spend") {
                int n;
                ss >> n;
                g->spendMoney(n);
            } else if (what == "damage") {
                float x;
                ss >> x;
                g->takeDamage(x);
            } else if (what == "noise") {
                int n;
                ss >> n;
                g->noise(n);
            } else if (what == "set_money") {
                int n;
                ss >> n;
                g->setS32(g->moneyAddr, n);
                g->setS32(g->moneyCopyAddr, n);
            } else if (what == "tick") {
                g->tick();
            } else if (what == "gain_exp") {
                int n;
                ss >> n;
                g->gainExp(n);
            }
        } else if (cmd == "expect_money") {
            int want;
            ss >> want;
            const int got = platform.game ? platform.game->getS32(platform.game->moneyAddr) : -1;
            if (got != want) {
                printf("  FALLO línea %d: dinero = %d, se esperaba %d\n", lineNo, got, want);
                failures++;
            } else {
                printf("  ok: dinero = %d\n", got);
            }
        } else if (cmd == "expect_exp") {
            int want;
            ss >> want;
            const int got = platform.game ? platform.game->getS32(platform.game->expAddr) : -1;
            if (got != want) {
                printf("  FALLO línea %d: experiencia = %d, se esperaba %d\n", lineNo, got, want);
                failures++;
            } else {
                printf("  ok: experiencia = %d\n", got);
            }
        } else if (cmd == "expect_nofile") {
            std::string path;
            ss >> path;
            if (fileExists(joinPath(platform.root, path))) {
                printf("  FALLO línea %d: %s no debería existir\n", lineNo, path.c_str());
                failures++;
            } else {
                printf("  ok: %s no existe\n", path.c_str());
            }
        } else if (cmd == "expect_hp") {
            float want;
            ss >> want;
            const float got = platform.game ? platform.game->getF32(platform.game->hpAddr) : -1;
            if (got != want) {
                printf("  FALLO línea %d: vida = %g, se esperaba %g\n", lineNo, got, want);
                failures++;
            } else {
                printf("  ok: vida = %g\n", got);
            }
        } else if (cmd == "expect_frozen_money") {
            int want;
            ss >> want;
            std::vector<uint64_t> fr;
            if (platform.game) platform.game->frozenAddresses(fr);
            bool got = false;
            for (uint64_t a : fr) got = got || a == platform.game->moneyAddr;
            if ((int)got != want) {
                printf("  FALLO línea %d: dinero congelado = %d, se esperaba %d\n", lineNo, (int)got, want);
                failures++;
            } else {
                printf("  ok: dinero congelado = %d\n", (int)got);
            }
        } else if (cmd == "expect_file") {
            std::string path;
            ss >> path;
            std::string content;
            if (!readFile(joinPath(platform.root, path), content)) {
                printf("  FALLO línea %d: no existe %s\n", lineNo, path.c_str());
                failures++;
            } else {
                printf("  ok: %s\n%s", path.c_str(), content.c_str());
            }
        } else if (cmd == "nogame") {
            int v;
            ss >> v;
            platform.noGame = v != 0;
        } else if (cmd == "reconnect") {
            app.connect();
            frame(Input());
        } else if (cmd == "kbd") {
            std::getline(ss, platform.kbdText);
            if (!platform.kbdText.empty() && platform.kbdText[0] == ' ') platform.kbdText.erase(0, 1);
        } else if (cmd == "addfav") {
            // addfav money|exp|hp|level|items Nombre del valor
            std::string which, name;
            ss >> which;
            std::getline(ss, name);
            if (!name.empty() && name[0] == ' ') name.erase(0, 1);
            std::string err;
            platform.connectGame(err);
            MockGame* g = platform.game.get();
            if (!g) continue;
            uint64_t addr = g->moneyAddr;
            VType t = VType::S32;
            if (which == "exp") addr = g->expAddr;
            if (which == "hp") {
                addr = g->hpAddr;
                t = VType::F32;
            }
            if (which == "level") {
                addr = g->levelAddr;
                t = VType::U16;
            }
            if (which == "items") {
                addr = g->itemsAddr;
                t = VType::U8;
            }
            FavoriteStore fs(joinPath(platform.root, "switch/ValorNX/guardados"));
            fs.load(g->info().titleId);
            Favorite f;
            f.name = name;
            f.type = t;
            f.region = g->info().classify(addr, &f.offset);
            f.buildId = g->info().buildIdHex();
            fs.items().push_back(f);
            fs.save();
        } else if (cmd == "ovl") {
            std::string sub;
            ss >> sub;
            if (sub == "show") {
                panel.show(nowMs());
                ovlFrame(nullptr);
            } else if (sub == "hide") {
                panel.hide();
            } else if (sub == "press") {
                std::string b;
                int times = 1;
                ss >> b >> times;
                if (times <= 0) times = 1;
                for (int i = 0; i < times; i++) {
                    ovl::PanelInput in;
                    in.down = in.held = parseBtn(b);
                    ovlFrame(&in);
                    ovl::PanelInput up;
                    ovlFrame(&up);
                }
            } else if (sub == "tap") {
                ovl::PanelInput in;
                in.tap = true;
                ss >> in.tapX >> in.tapY;
                ovlFrame(&in);
            } else if (sub == "type") {
                std::string text;
                ss >> text;
                for (char ch : text) {
                    ovl::PanelInput in;
                    in.typed = ch;
                    ovlFrame(&in);
                }
            } else if (sub == "wait") {
                int n = 1;
                ss >> n;
                for (int i = 0; i < n; i++) ovlFrame(nullptr);
            } else if (sub == "shot") {
                std::string name;
                ss >> name;
                ovlFrame(nullptr);
                // La imagen de la captura es cosa del simulador: no cuenta en la memoria del overlay.
                const size_t peak = g_memPeak;
                {
                    std::vector<uint32_t> img = backdrop;
                    emu.composite(img.data(), W);
                    writePng(joinPath(shots, name + ".png"), img, W, H);
                }
                g_memPeak = peak;
                printf("  captura: %s.png\n", name.c_str());
            } else if (sub == "waittask") {
                int guard = 0;
                while (panel.busy() && guard++ < 100000) ovlFrame(nullptr);
                ovlFrame(nullptr);
            } else if (sub == "expect_busy") {
                int want = 0;
                ss >> want;
                const int got = panel.busy() ? 1 : 0;
                if (got != want) {
                    printf("  FALLO línea %d: overlay buscando = %d, se esperaba %d\n", lineNo, got, want);
                    failures++;
                } else {
                    printf("  ok: overlay buscando = %d\n", got);
                }
            } else if (sub == "expect_closed") {
                int want = 0;
                ss >> want;
                const int got = ovlUsed ? 0 : 1;
                if (got != want) {
                    printf("  FALLO línea %d: overlay cerrado = %d, se esperaba %d\n", lineNo, got, want);
                    failures++;
                } else {
                    printf("  ok: overlay cerrado = %d\n", got);
                }
            } else {
                printf("  orden de overlay desconocida en línea %d: %s\n", lineNo, sub.c_str());
            }
        } else if (cmd == "mem") {
            std::string sub;
            ss >> sub;
            static size_t base = 0;
            if (sub == "reset") {
                base = g_memCur;
                g_memPeak = g_memCur;
            } else if (sub == "report") {
                size_t maxKb = 0;
                ss >> maxKb;
                const size_t kb = (g_memPeak - base + 1023) / 1024;
                if (maxKb && kb > maxKb) {
                    printf("  FALLO línea %d: memoria máxima %zu KB (límite %zu KB)\n", lineNo, kb, maxKb);
                    failures++;
                } else {
                    printf("  ok: memoria máxima pedida: %zu KB\n", kb);
                }
            }
        } else if (cmd == "echo") {
            std::string rest;
            std::getline(ss, rest);
            printf("%s\n", rest.c_str());
        } else {
            printf("  orden desconocida en línea %d: %s\n", lineNo, cmd.c_str());
        }
        if (app.wantsExit()) {
            printf("  (la app pidió salir)\n");
            break;
        }
    }
    printf("frames: %llu, fallos: %d\n", (unsigned long long)frames, failures);
    return failures ? 1 : 0;
}
