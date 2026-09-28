// Emulador en PC del renderizador de libtesla (tsl::gfx::Renderer), para probar el
// overlay de ValorNX y sacar capturas. Imita lo que usa TeslaPainter con las mismas
// reglas: capa de 448x720 en RGBA4444, mezcla con 4 bits y texto con stb_truetype
// ("fontSize" = altura del texto en píxeles, dibujado desde la línea base).
#pragma once
#include <sys/types.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct EmuColor {
    union {
        struct {
            uint16_t r : 4, g : 4, b : 4, a : 4;
        } __attribute__((packed));
        uint16_t rgba;
    };
    constexpr EmuColor(uint16_t raw) : rgba(raw) {}
    constexpr EmuColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r(r), g(g), b(b), a(a) {}
};

class TeslaEmu {
public:
    static constexpr int kW = 448, kH = 720;

    explicit TeslaEmu(const std::string& fontData);
    ~TeslaEmu();
    bool ok() const { return m_ok; }
    float ascentRatio() const { return m_ascent; }

    static EmuColor a(const EmuColor& c) { return c; }   // sin animación de aparición
    void fillScreen(EmuColor c);
    void setPixel(int x, int y, EmuColor c);
    void setPixelBlendDst(int x, int y, EmuColor c);
    void drawRect(int x, int y, int w, int h, EmuColor c);
    std::pair<uint32_t, uint32_t> drawString(const char* s, bool monospace, int x, int y, float fontSize, EmuColor color,
                                             ssize_t maxWidth = 0);
    void enableScissoring(int x, int y, int w, int h) { m_scissor.push_back({x, y, w, h}); }
    void disableScissoring() {
        if (!m_scissor.empty()) m_scissor.pop_back();
    }

    // Mezcla la capa sobre una imagen RGBA8 (0xAABBGGRR) de 'stride' píxeles por fila.
    void composite(uint32_t* dst, int stride) const;

private:
    struct Rect {
        int x, y, w, h;
    };
    struct Glyph {
        int bounds[4] = {};
        int xAdvance = 0;
        unsigned char* bmp = nullptr;
        int w = 0, h = 0;
        float scale = 0;
    };
    std::string m_fontData;
    void* m_font = nullptr;   // stbtt_fontinfo
    bool m_ok = false;
    float m_ascent = 0.8f;
    std::vector<uint16_t> m_fb;
    std::vector<Rect> m_scissor;
    std::unordered_map<uint64_t, Glyph> m_glyphs;

    bool visible(int x, int y) const;
};
