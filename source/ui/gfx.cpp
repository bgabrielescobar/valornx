#include "gfx.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vnx {

namespace {

inline uint32_t blendPx(uint32_t dst, uint32_t src, uint32_t a) {
    if (a >= 255) return src | 0xFF000000u;
    const uint32_t ia = 255 - a;
    const uint32_t rb = (((src & 0x00FF00FFu) * a + (dst & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
    const uint32_t g = (((src & 0x0000FF00u) * a + (dst & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u;
    return rb | g | 0xFF000000u;
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

Color mixColor(Color a, Color b, float t) {
    t = clamp01(t);
    auto ch = [&](int shift) {
        float x = (float)((a >> shift) & 0xFF), y = (float)((b >> shift) & 0xFF);
        return (uint32_t)(x + (y - x) * t + 0.5f) & 0xFF;
    };
    return ch(0) | (ch(8) << 8) | (ch(16) << 16) | (ch(24) << 24);
}

uint32_t decodeUtf8(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x80) {
        i++;
        return c;
    }
    int extra;
    uint32_t cp;
    if (c >= 0xF0) {
        extra = 3;
        cp = c & 0x07;
    } else if (c >= 0xE0) {
        extra = 2;
        cp = c & 0x0F;
    } else if (c >= 0xC0) {
        extra = 1;
        cp = c & 0x1F;
    } else {
        i++;
        return 0xFFFD;
    }
    i++;
    for (int k = 0; k < extra; k++) {
        if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
        i++;
    }
    return cp;
}

// ------------------------------------------------------------------ Font

Font::Font() {
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) == 0) m_lib = lib;
}

Font::~Font() {
    for (FT_Face f : m_faces) FT_Done_Face(f);
    if (m_lib) FT_Done_FreeType(m_lib);
}

bool Font::addFace(const void* data, size_t size) {
    if (!m_lib || !data || size == 0) return false;
    FT_Face face = nullptr;
    if (FT_New_Memory_Face(m_lib, (const FT_Byte*)data, (FT_Long)size, 0, &face) != 0) return false;
    m_faces.push_back(face);
    return true;
}

void Font::loadMetrics(int size) {
    if (m_metrics.count(size)) return;
    int asc = size, height = size + size / 4;
    if (!m_faces.empty() && FT_Set_Pixel_Sizes(m_faces[0], 0, (FT_UInt)size) == 0) {
        asc = (int)(m_faces[0]->size->metrics.ascender >> 6);
        height = (int)(m_faces[0]->size->metrics.height >> 6);
    }
    m_metrics[size] = {asc, height};
}

int Font::ascender(int size) {
    loadMetrics(size);
    return m_metrics[size].first;
}

int Font::lineHeight(int size) {
    loadMetrics(size);
    return m_metrics[size].second;
}

const Font::Glyph* Font::glyph(uint32_t cp, int size) {
    const uint64_t key = ((uint64_t)(uint32_t)size << 32) | cp;
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return &it->second;
    Glyph g;
    g.advance = 0;
    for (FT_Face face : m_faces) {
        const FT_UInt idx = FT_Get_Char_Index(face, cp);
        if (idx == 0) continue;
        if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size) != 0) continue;
        if (FT_Load_Glyph(face, idx, FT_LOAD_RENDER) != 0) continue;
        const FT_GlyphSlot s = face->glyph;
        g.w = (int)s->bitmap.width;
        g.h = (int)s->bitmap.rows;
        g.left = s->bitmap_left;
        g.top = s->bitmap_top;
        g.advance = (int)(s->advance.x >> 6);
        if (s->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY && g.w > 0 && g.h > 0) {
            g.bmp.resize((size_t)g.w * g.h);
            for (int y = 0; y < g.h; y++)
                memcpy(&g.bmp[(size_t)y * g.w], s->bitmap.buffer + (ptrdiff_t)y * s->bitmap.pitch, (size_t)g.w);
        } else {
            g.w = g.h = 0;
        }
        break;
    }
    auto res = m_cache.emplace(key, std::move(g));
    return &res.first->second;
}

// ------------------------------------------------------------------ Canvas

Canvas::Canvas(uint32_t* pixels, int width, int height, int stridePx, Font& font)
    : m_px(pixels), m_w(width), m_h(height), m_stride(stridePx), m_font(font) {
    m_clip = {0, 0, width, height};
}

inline void Canvas::blend(int x, int y, Color c, uint32_t cov) {
    if (x < m_clip.x0 || y < m_clip.y0 || x >= m_clip.x1 || y >= m_clip.y1) return;
    const uint32_t a = ((c >> 24) * cov + 127) / 255;
    if (a == 0) return;
    uint32_t* p = m_px + (size_t)y * m_stride + x;
    *p = blendPx(*p, c, a);
}

void Canvas::clear(Color c) {
    const uint32_t v = c | 0xFF000000u;
    for (int y = 0; y < m_h; y++) std::fill(m_px + (size_t)y * m_stride, m_px + (size_t)y * m_stride + m_w, v);
}

void Canvas::fillRect(int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, m_clip.x0), y0 = std::max(y, m_clip.y0);
    const int x1 = std::min(x + w, m_clip.x1), y1 = std::min(y + h, m_clip.y1);
    if (x0 >= x1 || y0 >= y1) return;
    const uint32_t a = c >> 24;
    if (a == 0) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t* row = m_px + (size_t)yy * m_stride;
        if (a == 255) {
            std::fill(row + x0, row + x1, c);
        } else {
            for (int xx = x0; xx < x1; xx++) row[xx] = blendPx(row[xx], c, a);
        }
    }
}

void Canvas::fillRoundRect(int x, int y, int w, int h, int r, Color c) {
    r = std::min(r, std::min(w, h) / 2);
    if (r <= 0) {
        fillRect(x, y, w, h, c);
        return;
    }
    fillRect(x + r, y, w - 2 * r, h, c);
    fillRect(x, y + r, r, h - 2 * r, c);
    fillRect(x + w - r, y + r, r, h - 2 * r, c);
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            const float dx = (float)r - ((float)i + 0.5f), dy = (float)r - ((float)j + 0.5f);
            const float cov = clamp01((float)r - std::sqrt(dx * dx + dy * dy) + 0.5f);
            if (cov <= 0.0f) continue;
            const uint32_t cv = (uint32_t)(cov * 255.0f + 0.5f);
            blend(x + i, y + j, c, cv);
            blend(x + w - 1 - i, y + j, c, cv);
            blend(x + i, y + h - 1 - j, c, cv);
            blend(x + w - 1 - i, y + h - 1 - j, c, cv);
        }
    }
}

void Canvas::strokeRoundRect(int x, int y, int w, int h, int r, int t, Color c) {
    r = std::min(r, std::min(w, h) / 2);
    if (r < t) r = t;
    fillRect(x + r, y, w - 2 * r, t, c);
    fillRect(x + r, y + h - t, w - 2 * r, t, c);
    fillRect(x, y + r, t, h - 2 * r, c);
    fillRect(x + w - t, y + r, t, h - 2 * r, c);
    const float inner = (float)(r - t);
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            const float dx = (float)r - ((float)i + 0.5f), dy = (float)r - ((float)j + 0.5f);
            const float d = std::sqrt(dx * dx + dy * dy);
            const float cov = clamp01((float)r - d + 0.5f) - clamp01(inner - d + 0.5f);
            if (cov <= 0.0f) continue;
            const uint32_t cv = (uint32_t)(cov * 255.0f + 0.5f);
            blend(x + i, y + j, c, cv);
            blend(x + w - 1 - i, y + j, c, cv);
            blend(x + i, y + h - 1 - j, c, cv);
            blend(x + w - 1 - i, y + h - 1 - j, c, cv);
        }
    }
}

void Canvas::fillCircle(int cx, int cy, int r, Color c) {
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            const float dx = (float)x + 0.5f - (float)cx, dy = (float)y + 0.5f - (float)cy;
            const float cov = clamp01((float)r - std::sqrt(dx * dx + dy * dy) + 0.5f);
            if (cov > 0.0f) blend(x, y, c, (uint32_t)(cov * 255.0f + 0.5f));
        }
    }
}

void Canvas::blitMask(int x, int y, int w, int h, const uint8_t* mask, Color c) {
    for (int j = 0; j < h; j++) {
        const int yy = y + j;
        if (yy < m_clip.y0 || yy >= m_clip.y1) continue;
        for (int i = 0; i < w; i++) {
            const uint8_t m = mask[(size_t)j * w + i];
            if (m) blend(x + i, yy, c, m);
        }
    }
}

void Canvas::blitImage(int x, int y, int w, int h, const uint32_t* img) {
    for (int j = 0; j < h; j++) {
        const int yy = y + j;
        if (yy < m_clip.y0 || yy >= m_clip.y1) continue;
        uint32_t* row = m_px + (size_t)yy * m_stride;
        for (int i = 0; i < w; i++) {
            const int xx = x + i;
            if (xx < m_clip.x0 || xx >= m_clip.x1) continue;
            const uint32_t p = img[(size_t)j * w + i];
            row[xx] = blendPx(row[xx], p, p >> 24);
        }
    }
}

int Canvas::measure(const std::string& s, int size) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = decodeUtf8(s, i);
        if (const Font::Glyph* g = m_font.glyph(cp, size)) w += g->advance;
    }
    return w;
}

std::string Canvas::ellipsize(const std::string& s, int size, int maxW) {
    if (maxW <= 0 || measure(s, size) <= maxW) return s;
    static const std::string dots = "\xE2\x80\xA6";   // "…"
    const std::string tail = m_font.glyph(0x2026, size)->advance > 0 ? dots : "...";
    std::string cut = s;
    while (!cut.empty()) {
        size_t n = cut.size() - 1;
        while (n > 0 && ((unsigned char)cut[n] & 0xC0) == 0x80) n--;
        cut.resize(n);
        while (!cut.empty() && cut.back() == ' ') cut.pop_back();
        if (measure(cut + tail, size) <= maxW) break;
    }
    return cut + tail;
}

int Canvas::text(int x, int y, const std::string& str, int size, Color c, Align a, int maxW) {
    const std::string s = maxW > 0 ? ellipsize(str, size, maxW) : str;
    const int w = measure(s, size);
    if (a == Align::Center)
        x -= w / 2;
    else if (a == Align::Right)
        x -= w;
    const int base = y + m_font.ascender(size);
    int pen = x;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = decodeUtf8(s, i);
        const Font::Glyph* g = m_font.glyph(cp, size);
        if (!g) continue;
        if (g->w > 0) {
            const int gx = pen + g->left, gy = base - g->top;
            const int i0 = std::max(0, m_clip.x0 - gx), j0 = std::max(0, m_clip.y0 - gy);
            const int i1 = std::min(g->w, m_clip.x1 - gx), j1 = std::min(g->h, m_clip.y1 - gy);
            for (int j = j0; j < j1; j++) {
                const uint8_t* src = &g->bmp[(size_t)j * g->w];
                uint32_t* row = m_px + (size_t)(gy + j) * m_stride + gx;
                for (int k = i0; k < i1; k++) {
                    if (!src[k]) continue;
                    const uint32_t alpha = ((c >> 24) * src[k] + 127) / 255;
                    row[k] = blendPx(row[k], c, alpha);
                }
            }
        }
        pen += g->advance;
    }
    return w;
}

std::vector<std::string> Canvas::wrap(const std::string& s, int size, int maxW) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        const std::string para = s.substr(start, nl - start);
        std::string line;
        size_t p = 0;
        if (para.empty()) lines.push_back("");
        while (p < para.size()) {
            size_t sp = para.find(' ', p);
            if (sp == std::string::npos) sp = para.size();
            std::string word = para.substr(p, sp - p);
            p = sp + 1;
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (measure(candidate, size) <= maxW) {
                line = candidate;
                continue;
            }
            if (!line.empty()) lines.push_back(line);
            // Palabra más larga que la línea: se corta por letras.
            while (measure(word, size) > maxW && word.size() > 1) {
                size_t cut = 0, i = 0;
                while (i < word.size()) {
                    size_t j = i;
                    decodeUtf8(word, j);
                    if (measure(word.substr(0, j), size) > maxW) break;
                    cut = j;
                    i = j;
                }
                if (cut == 0) cut = 1;
                lines.push_back(word.substr(0, cut));
                word = word.substr(cut);
            }
            line = word;
        }
        if (!line.empty()) lines.push_back(line);
        start = nl + 1;
    }
    return lines;
}

int Canvas::textBlock(int x, int y, const std::string& s, int size, Color c, int maxW, int lineGap) {
    const int lh = m_font.lineHeight(size) + lineGap;
    int yy = y;
    for (const std::string& line : wrap(s, size, maxW)) {
        text(x, yy, line, size, c);
        yy += lh;
    }
    return yy - y;
}

void Canvas::pushClip(int x, int y, int w, int h) {
    m_clipStack.push_back(m_clip);
    Rect r{std::max(x, m_clip.x0), std::max(y, m_clip.y0), std::min(x + w, m_clip.x1), std::min(y + h, m_clip.y1)};
    if (r.x1 < r.x0) r.x1 = r.x0;
    if (r.y1 < r.y0) r.y1 = r.y0;
    m_clip = r;
}

void Canvas::popClip() {
    if (m_clipStack.empty()) return;
    m_clip = m_clipStack.back();
    m_clipStack.pop_back();
}

}  // namespace vnx
