// ValorNX - dibujo por software sobre el framebuffer (1280x720, RGBA8).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct FT_LibraryRec_;
struct FT_FaceRec_;

namespace vnx {

// Color en el orden de bytes del framebuffer (R, G, B, A) => 0xAABBGGRR.
using Color = uint32_t;
constexpr Color rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
    return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) | ((a & 0xFF) << 24);
}
constexpr Color withAlpha(Color c, uint32_t a) { return (c & 0x00FFFFFFu) | ((a & 0xFF) << 24); }
Color mixColor(Color a, Color b, float t);

uint32_t decodeUtf8(const std::string& s, size_t& i);

class Font {
public:
    struct Glyph {
        int w = 0, h = 0, left = 0, top = 0, advance = 0;
        std::vector<uint8_t> bmp;
    };

    Font();
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // La primera fuente es la principal; las demás se usan si falta un carácter.
    bool addFace(const void* data, size_t size);
    bool ready() const { return !m_faces.empty(); }
    const Glyph* glyph(uint32_t cp, int size);
    int ascender(int size);
    int lineHeight(int size);

private:
    FT_LibraryRec_* m_lib = nullptr;
    std::vector<FT_FaceRec_*> m_faces;
    std::unordered_map<uint64_t, Glyph> m_cache;
    std::unordered_map<int, std::pair<int, int>> m_metrics;
    void loadMetrics(int size);
};

enum class Align { Left, Center, Right };

class Canvas {
public:
    Canvas(uint32_t* pixels, int width, int height, int stridePx, Font& font);

    int width() const { return m_w; }
    int height() const { return m_h; }
    Font& font() { return m_font; }

    void clear(Color c);
    void fillRect(int x, int y, int w, int h, Color c);
    void fillRoundRect(int x, int y, int w, int h, int r, Color c);
    void strokeRoundRect(int x, int y, int w, int h, int r, int t, Color c);
    void fillCircle(int cx, int cy, int r, Color c);
    void blitMask(int x, int y, int w, int h, const uint8_t* mask, Color c);
    void blitImage(int x, int y, int w, int h, const uint32_t* img);

    // (x, y) = esquina superior izquierda de la línea. Devuelve el ancho dibujado.
    int text(int x, int y, const std::string& s, int size, Color c, Align a = Align::Left, int maxW = 0);
    int measure(const std::string& s, int size);
    std::string ellipsize(const std::string& s, int size, int maxW);
    std::vector<std::string> wrap(const std::string& s, int size, int maxW);
    // Texto en varias líneas; devuelve la altura usada.
    int textBlock(int x, int y, const std::string& s, int size, Color c, int maxW, int lineGap = 6);

    void pushClip(int x, int y, int w, int h);
    void popClip();

private:
    struct Rect {
        int x0, y0, x1, y1;
    };
    uint32_t* m_px;
    int m_w, m_h, m_stride;
    Font& m_font;
    Rect m_clip;
    std::vector<Rect> m_clipStack;

    inline void blend(int x, int y, Color c, uint32_t cov);
};

}  // namespace vnx
