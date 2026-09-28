// ValorNX - punto de entrada en la Nintendo Switch.
#include <switch.h>
#include <turbojpeg.h>

#include <cmath>
#include <cstdio>
#include <memory>

#include "../core/fsutil.hpp"
#include "../ui/app.hpp"
#include "nsinfo.hpp"
#include "switch_game.hpp"

namespace vnx {

uint64_t nowMs() { return armTicksToNs(armGetSystemTick()) / 1000000ull; }

// Icono del juego: JPEG 256x256 -> 64x64 con esquinas redondeadas.
static void decodeIcon(const u8* jpg, size_t size, ProcessInfo& pi) {
    tjhandle h = tjInitDecompress();
    if (!h) return;
    int w = 0, hgt = 0, sub = 0, cs = 0;
    if (tjDecompressHeader3(h, jpg, (unsigned long)size, &w, &hgt, &sub, &cs) == 0 && w > 0 && hgt > 0 && w <= 1024 &&
        hgt <= 1024) {
        std::vector<u8> pixels((size_t)w * hgt * 4);
        if (tjDecompress2(h, jpg, (unsigned long)size, pixels.data(), w, 0, hgt, TJPF_RGBA, 0) == 0) {
            const int out = 64;
            const float radius = 12.0f;
            pi.iconSize = out;
            pi.icon.assign((size_t)out * out, 0);
            for (int y = 0; y < out; y++) {
                for (int x = 0; x < out; x++) {
                    int x0 = x * w / out, x1 = (x + 1) * w / out, y0 = y * hgt / out, y1 = (y + 1) * hgt / out;
                    if (x1 <= x0) x1 = x0 + 1;
                    if (y1 <= y0) y1 = y0 + 1;
                    u32 r = 0, g = 0, b = 0, n = 0;
                    for (int yy = y0; yy < y1; yy++) {
                        for (int xx = x0; xx < x1; xx++) {
                            const u8* p = &pixels[((size_t)yy * w + xx) * 4];
                            r += p[0];
                            g += p[1];
                            b += p[2];
                            n++;
                        }
                    }
                    // Esquinas redondeadas (transparencia con suavizado)
                    float cx = x + 0.5f, cy = y + 0.5f, a = 1.0f;
                    const float ex = cx < radius ? radius - cx : (cx > out - radius ? cx - (out - radius) : 0.0f);
                    const float ey = cy < radius ? radius - cy : (cy > out - radius ? cy - (out - radius) : 0.0f);
                    if (ex > 0.0f && ey > 0.0f) {
                        a = radius - std::sqrt(ex * ex + ey * ey) + 0.5f;
                        a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
                    }
                    pi.icon[(size_t)y * out + x] = rgba(r / n, g / n, b / n, (u32)(a * 255.0f + 0.5f));
                }
            }
        }
    }
    tjDestroy(h);
}

class SwitchPlatform : public Platform {
public:
    std::string sdRoot() const override { return "sdmc:/"; }

    IGame* connectGame(std::string& error) override {
        m_game.reset();
        m_game.reset(DmntGame::connect(error));
        if (m_game) {
            ProcessInfo& pi = m_game->editableInfo();
            std::vector<uint8_t> jpeg;
            loadGameName(pi.titleId, pi.gameName, &jpeg);
            if (!jpeg.empty()) decodeIcon(jpeg.data(), jpeg.size(), pi);
        }
        return m_game.get();
    }

    bool keyboard(const std::string& title, const std::string& initial, std::string& out) override {
        SwkbdConfig kbd;
        if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetHeaderText(&kbd, title.c_str());
        swkbdConfigSetGuideText(&kbd, title.c_str());
        swkbdConfigSetInitialText(&kbd, initial.c_str());
        swkbdConfigSetStringLenMax(&kbd, 40);
        char buf[256] = {0};
        const Result rc = swkbdShow(&kbd, buf, sizeof(buf));
        swkbdClose(&kbd);
        if (R_FAILED(rc)) return false;
        out = buf;
        return true;
    }

    void keepAwake(bool on) override { appletSetMediaPlaybackState(on); }

private:
    std::unique_ptr<DmntGame> m_game;
};

static uint32_t mapButtons(u64 k) {
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

// Si no se pueden cargar las fuentes del sistema, se avisa en modo texto.
static void fatalScreen(const char* msg) {
    consoleInit(nullptr);
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeAny(&pad);
    printf("\n  ValorNX\n\n  %s\n\n  Pulsa + para salir.\n", msg);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

}  // namespace vnx

int main(int argc, char** argv) {
    using namespace vnx;

    if (R_FAILED(plInitialize(PlServiceType_User))) {
        fatalScreen("No se pudieron cargar las fuentes del sistema (pl:u).");
        return 0;
    }

    {
        Font font;
        const PlSharedFontType fontTypes[] = {PlSharedFontType_Standard, PlSharedFontType_NintendoExt,
                                              PlSharedFontType_ChineseSimplified, PlSharedFontType_ChineseTraditional,
                                              PlSharedFontType_KO};
        for (PlSharedFontType t : fontTypes) {
            PlFontData fd;
            if (R_SUCCEEDED(plGetSharedFontByType(&fd, t))) font.addFace(fd.address, fd.size);
        }
        if (!font.ready()) {
            fatalScreen("No se pudieron cargar las fuentes del sistema.");
            plExit();
            return 0;
        }

        Framebuffer fb;
        framebufferCreate(&fb, nwindowGetDefault(), kScreenW, kScreenH, PIXEL_FORMAT_RGBA_8888, 2);
        framebufferMakeLinear(&fb);

        padConfigureInput(8, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeAny(&pad);
        hidInitializeTouchScreen();

        {
            SwitchPlatform platform;
            App app(platform, font);
            app.start();
            InputFilter filter;
            while (appletMainLoop()) {
                padUpdate(&pad);
                const uint32_t held = mapButtons(padGetButtons(&pad));
                HidTouchScreenState ts;
                bool touching = false;
                int tx = 0, ty = 0;
                if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
                    touching = true;
                    tx = (int)ts.touches[0].x;
                    ty = (int)ts.touches[0].y;
                }
                const Input in = filter.process(held, touching, tx, ty, 0, nowMs());

                u32 stride = 0;
                u32* px = (u32*)framebufferBegin(&fb, &stride);
                Canvas canvas(px, kScreenW, kScreenH, (int)(stride / sizeof(u32)), font);
                app.frame(in, canvas);
                framebufferEnd(&fb);
                if (app.wantsExit()) break;
            }
            platform.keepAwake(false);
        }
        framebufferClose(&fb);
    }
    plExit();
    return 0;
}
