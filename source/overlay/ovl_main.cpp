// ValorNX (overlay) - arranque con libtesla: se abre desde Tesla Menu o Ultrahand.
//
// libtesla crea la capa encima del juego, lee los botones y hace las animaciones; todo lo
// demás está en Panel, que es el mismo código que se prueba en PC.
#define TESLA_INIT_IMPL
#include <array>
#include <bit>
#include <unordered_map>

#include <tesla.hpp>

#include <atomic>
#include <memory>

#include "../core/fsutil.hpp"
#include "../switch/nsinfo.hpp"
#include "../switch/switch_game.hpp"
#include "panel.hpp"
#include "tesla_painter.hpp"

namespace vnx {
uint64_t nowMs() { return armTicksToNs(armGetSystemTick()) / 1000000ull; }
}  // namespace vnx

using namespace vnx;

namespace {

std::unique_ptr<DmntGame> g_game;
ovl::Panel* g_panel = nullptr;
float g_ascent = 0.8f;   // parte de la altura del texto sobre la línea base (fuente del sistema)

// libtesla puede avisar de que se oculta desde su hilo de botones; el juego se suelta
// después, en el hilo principal, cuando la animación ya no deja ver nada.
std::atomic<bool> g_hideRequested{false};

// Toques de pantalla, traducidos a "toque corto" y "arrastrar".
bool g_tap = false, g_moved = false;
int g_tapX = 0, g_tapY = 0, g_dragY = 0;

uint32_t mapButtons(u64 k) {
    uint32_t b = 0;
    if (k & HidNpadButton_A) b |= BTN_A;
    if (k & HidNpadButton_B) b |= BTN_B;
    if (k & HidNpadButton_X) b |= BTN_X;
    if (k & HidNpadButton_Y) b |= BTN_Y;
    if (k & HidNpadButton_L) b |= BTN_L;
    if (k & HidNpadButton_R) b |= BTN_R;
    if (k & HidNpadButton_ZL) b |= BTN_ZL;
    if (k & HidNpadButton_ZR) b |= BTN_ZR;
    if (k & HidNpadButton_Plus) b |= BTN_PLUS;
    if (k & HidNpadButton_Minus) b |= BTN_MINUS;
    if (k & HidNpadButton_AnyUp) b |= BTN_UP;
    if (k & HidNpadButton_AnyDown) b |= BTN_DOWN;
    if (k & HidNpadButton_AnyLeft) b |= BTN_LEFT;
    if (k & HidNpadButton_AnyRight) b |= BTN_RIGHT;
    return b;
}

void releaseGame() {
    if (g_panel) g_panel->hide();
    g_game.reset();
}

// Un único elemento que ocupa toda la capa: dibuja el panel y recibe los toques.
// No acepta el foco, así libtesla no dibuja su propio recuadro ni mueve nada.
class PanelElement : public tsl::elm::Element {
public:
    void draw(tsl::gfx::Renderer* renderer) override {
        ovl::TeslaPainter<tsl::gfx::Renderer, tsl::Color> painter(renderer, g_ascent);
        g_panel->draw(painter);
    }
    void layout(u16, u16, u16, u16) override {}
    tsl::elm::Element* requestFocus(tsl::elm::Element*, tsl::FocusDirection) override { return nullptr; }
    bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32, s32 prevY, s32, s32) override {
        switch (event) {
            case tsl::elm::TouchEvent::Touch: g_moved = false; break;
            case tsl::elm::TouchEvent::Hold: break;
            case tsl::elm::TouchEvent::Scroll:
                g_moved = true;
                g_dragY += currY - prevY;
                break;
            case tsl::elm::TouchEvent::Release:
                if (!g_moved) {
                    g_tap = true;
                    g_tapX = currX;
                    g_tapY = currY;
                }
                g_moved = false;
                break;
        }
        return true;
    }
};

class MainGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override { return new PanelElement(); }

    void update() override {
        if (g_hideRequested) {
            // Animación de salida: ya no se busca nada más. En el último fotograma (ya invisible)
            // se cancela lo que hubiera a medias y se suelta el juego.
            if (tsl::gfx::Renderer::a(tsl::Color(0, 0, 0, 0xF)).a == 0) {
                g_hideRequested = false;
                releaseGame();
            }
            return;
        }
        g_panel->update(nowMs());
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&, HidAnalogStickState,
                     HidAnalogStickState) override {
        ovl::PanelInput in;
        in.down = mapButtons(keysDown);
        in.held = mapButtons(keysHeld);
        in.tap = g_tap;
        in.tapX = g_tapX;
        in.tapY = g_tapY;
        in.dragY = g_dragY;
        g_tap = false;
        g_dragY = 0;
        // false con la B en la lista principal: libtesla cierra el overlay.
        return g_panel->input(in, nowMs());
    }
};

class ValorOverlay : public tsl::Overlay {
public:
    void initServices() override {
        fsdevMountSdmc();
        // Medidas de la fuente del sistema, para colocar el texto por arriba (como en la app).
        PlFontData font;
        if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) {
            const u8* data = static_cast<const u8*>(font.address);
            stbtt_fontinfo info;
            if (stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
                int asc = 0, desc = 0, gap = 0;
                stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
                if (asc - desc > 0) g_ascent = (float)asc / (float)(asc - desc);
            }
        }
        ovl::PanelEnv env;
        env.sdRoot = "sdmc:/";
        env.connect = [](std::string& error) -> IGame* {
            g_game.reset();
            g_game.reset(DmntGame::connect(error));
            return g_game.get();
        };
        env.disconnect = []() { g_game.reset(); };
        env.gameName = [](uint64_t titleId) {
            std::string name;
            loadGameName(titleId, name, nullptr);
            return name;
        };
        g_panel = new ovl::Panel(env);
    }

    void exitServices() override {
        releaseGame();
        delete g_panel;
        g_panel = nullptr;
        fsdevUnmountAll();
    }

    void onShow() override {
        g_hideRequested = false;
        tsl::hlp::doWithSmSession([] { g_panel->show(nowMs()); });
    }

    void onHide() override { g_hideRequested = true; }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<MainGui>(); }
};

}  // namespace

int main(int argc, char** argv) { return tsl::loop<ValorOverlay>(argc, argv); }
