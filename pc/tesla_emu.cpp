#include "tesla_emu.hpp"

#include <algorithm>
#include <cstring>
#include <cwctype>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace {

// Igual que decode_utf8 de libnx: devuelve los bytes usados (0 al final de la cadena).
ssize_t decodeUtf8(uint32_t* out, const unsigned char* in) {
    const unsigned char c = in[0];
    if (c == 0) {
        *out = 0;
        return 0;
    }
    if (c < 0x80) {
        *out = c;
        return 1;
    }
    int extra = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : (c >= 0xC0 ? 1 : -1));
    if (extra < 0) return -1;
    uint32_t cp = c & (extra == 3 ? 0x07 : (extra == 2 ? 0x0F : 0x1F));
    for (int k = 1; k <= extra; k++) {
        if ((in[k] & 0xC0) != 0x80) return -1;
        cp = (cp << 6) | (in[k] & 0x3F);
    }
    *out = cp;
    return extra + 1;
}

uint8_t blendColor(uint8_t src, uint8_t dst, uint8_t alpha) {
    const uint8_t oneMinusAlpha = 0x0F - alpha;
    return (uint8_t)((dst * alpha + src * oneMinusAlpha) / float(0xF));
}

}  // namespace

TeslaEmu::TeslaEmu(const std::string& fontData) : m_fontData(fontData), m_fb((size_t)kW * kH, 0) {
    stbtt_fontinfo* f = new stbtt_fontinfo();
    m_font = f;
    const unsigned char* data = (const unsigned char*)m_fontData.data();
    if (!m_fontData.empty() && stbtt_InitFont(f, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
        if (asc - desc > 0) m_ascent = (float)asc / (float)(asc - desc);
        m_ok = true;
    }
}

TeslaEmu::~TeslaEmu() {
    for (auto& kv : m_glyphs)
        if (kv.second.bmp) stbtt_FreeBitmap(kv.second.bmp, nullptr);
    delete (stbtt_fontinfo*)m_font;
}

bool TeslaEmu::visible(int x, int y) const {
    if (x < 0 || y < 0 || x >= kW || y >= kH) return false;
    if (!m_scissor.empty()) {
        const Rect& s = m_scissor.back();
        // Como en libtesla: el borde derecho e inferior también cuentan.
        if (x < s.x || y < s.y || x > s.x + s.w || y > s.y + s.h) return false;
    }
    return true;
}

void TeslaEmu::fillScreen(EmuColor c) { std::fill(m_fb.begin(), m_fb.end(), c.rgba); }

void TeslaEmu::setPixel(int x, int y, EmuColor c) {
    if (visible(x, y)) m_fb[(size_t)y * kW + x] = c.rgba;
}

void TeslaEmu::setPixelBlendDst(int x, int y, EmuColor color) {
    if (!visible(x, y)) return;
    EmuColor src(m_fb[(size_t)y * kW + x]);
    EmuColor dst(color);
    EmuColor end(0);
    end.r = blendColor(src.r, dst.r, dst.a);
    end.g = blendColor(src.g, dst.g, dst.a);
    end.b = blendColor(src.b, dst.b, dst.a);
    end.a = std::min(dst.a + src.a, 0xF);
    m_fb[(size_t)y * kW + x] = end.rgba;
}

void TeslaEmu::drawRect(int x, int y, int w, int h, EmuColor c) {
    for (int x1 = x; x1 < x + w; x1++)
        for (int y1 = y; y1 < y + h; y1++) setPixelBlendDst(x1, y1, c);
}

std::pair<uint32_t, uint32_t> TeslaEmu::drawString(const char* string, bool monospace, int x, int y, float fontSize,
                                                   EmuColor color, ssize_t maxWidth) {
    int maxX = x, currX = x, currY = y;
    stbtt_fontinfo* font = (stbtt_fontinfo*)m_font;
    if (!m_ok) return {0, 0};
    do {
        if (maxWidth > 0 && maxWidth < (currX - x)) break;
        uint32_t ch = 0;
        const ssize_t cw = decodeUtf8(&ch, (const unsigned char*)string);
        if (cw <= 0) break;
        string += cw;
        if (ch == '\n') {
            maxX = std::max(currX, maxX);
            currX = x;
            currY += (int)fontSize;
            continue;
        }
        uint32_t sizeBits;
        memcpy(&sizeBits, &fontSize, 4);
        const uint64_t key = ((uint64_t)ch << 32) | ((uint64_t)monospace << 31) | sizeBits;
        auto it = m_glyphs.find(key);
        if (it == m_glyphs.end()) {
            Glyph g;
            g.scale = stbtt_ScaleForPixelHeight(font, fontSize);
            stbtt_GetCodepointBitmapBoxSubpixel(font, (int)ch, g.scale, g.scale, 0, 0, &g.bounds[0], &g.bounds[1],
                                                &g.bounds[2], &g.bounds[3]);
            int yAdvance = 0;
            stbtt_GetCodepointHMetrics(font, monospace ? 'W' : (int)ch, &g.xAdvance, &yAdvance);
            g.bmp = stbtt_GetCodepointBitmap(font, g.scale, g.scale, (int)ch, &g.w, &g.h, nullptr, nullptr);
            it = m_glyphs.emplace(key, g).first;
        }
        const Glyph& g = it->second;
        if (g.bmp && !std::iswspace((wint_t)ch) && fontSize > 0 && color.a != 0x0) {
            const int gx = currX + g.bounds[0], gy = currY + g.bounds[1];
            for (int by = 0; by < g.h; by++) {
                for (int bx = 0; bx < g.w; bx++) {
                    const int v = g.bmp[g.w * by + bx] >> 4;
                    if (v == 0xF) {
                        setPixel(gx + bx, gy + by, color);
                    } else if (v != 0x0) {
                        EmuColor tmp = color;
                        tmp.a = (uint8_t)(v * (float(tmp.a) / 0xF));
                        setPixelBlendDst(gx + bx, gy + by, tmp);
                    }
                }
            }
        }
        currX += (int)(g.xAdvance * g.scale);
    } while (*string != '\0');
    maxX = std::max(currX, maxX);
    return {(uint32_t)(maxX - x), (uint32_t)(currY - y)};
}

void TeslaEmu::composite(uint32_t* dst, int stride) const {
    for (int y = 0; y < kH; y++) {
        for (int x = 0; x < kW; x++) {
            const EmuColor c(m_fb[(size_t)y * kW + x]);
            const uint32_t a = c.a * 17, ia = 255 - a;
            uint32_t& d = dst[(size_t)y * stride + x];
            const uint32_t r = (c.r * 17 * a + (d & 0xFF) * ia) / 255;
            const uint32_t g = (c.g * 17 * a + ((d >> 8) & 0xFF) * ia) / 255;
            const uint32_t b = (c.b * 17 * a + ((d >> 16) & 0xFF) * ia) / 255;
            d = r | (g << 8) | (b << 16) | 0xFF000000u;
        }
    }
}
