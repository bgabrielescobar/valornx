// ValorNX (overlay) - Painter sobre el renderizador de libtesla.
//
// Es una plantilla para poder usarla también con el emulador de PC, que imita
// tsl::gfx::Renderer (mismos nombres y mismas reglas de mezcla en RGBA4444).
#pragma once
#include <string>

#include "painter.hpp"

namespace vnx {
namespace ovl {

template <typename Renderer, typename TColor>
class TeslaPainter final : public Painter {
public:
    // ascentRatio: parte de la altura del texto que queda sobre la línea base
    // (libtesla dibuja el texto a partir de la línea base).
    TeslaPainter(Renderer* r, float ascentRatio) : m_r(r), m_ascent(ascentRatio) {}

    void clear(Color c) override { m_r->fillScreen(Renderer::a(conv(c))); }

    void rect(int x, int y, int w, int h, Color c) override {
        if (w <= 0 || h <= 0 || (c >> 24) < 8) return;
        m_r->drawRect(x, y, w, h, Renderer::a(conv(c)));
    }

    void mask(int x, int y, int w, int h, const uint8_t* alpha, Color c) override {
        const uint32_t ca = c >> 24;
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                const uint32_t m = alpha[j * w + i];
                if (m < 8) continue;
                m_r->setPixelBlendDst(x + i, y + j, Renderer::a(conv(withAlpha(c, ca * m / 255))));
            }
        }
    }

    int text(int x, int y, const std::string& s, int size, Color c) override {
        if (s.empty()) return 0;   // libtesla lee más allá del final con cadenas vacías
        const int base = y + (int)((float)size * m_ascent + 0.5f);
        return (int)m_r->drawString(s.c_str(), false, x, base, (float)size, Renderer::a(conv(c))).first;
    }

    int measure(const std::string& s, int size) override {
        if (s.empty()) return 0;
        return (int)m_r->drawString(s.c_str(), false, 0, 0, (float)size, TColor(0, 0, 0, 0)).first;
    }

    void pushClip(int x, int y, int w, int h) override { m_r->enableScissoring(x, y, w, h); }
    void popClip() override { m_r->disableScissoring(); }

private:
    Renderer* m_r;
    float m_ascent;

    static uint8_t to4(uint32_t v) { return (uint8_t)((v * 15 + 127) / 255); }
    static TColor conv(Color c) {
        return TColor(to4(c & 0xFF), to4((c >> 8) & 0xFF), to4((c >> 16) & 0xFF), to4(c >> 24));
    }
};

}  // namespace ovl
}  // namespace vnx
