#include "painter.hpp"

#include <algorithm>
#include <cmath>

namespace vnx {
namespace ovl {

namespace {

Color scaleAlpha(Color c, float k) {
    const uint32_t a = (uint32_t)((float)(c >> 24) * k + 0.5f);
    return withAlpha(c, a > 255 ? 255 : a);
}

// Longitud en bytes del carácter UTF-8 que empieza en s[i].
size_t utf8Len(const std::string& s, size_t i) {
    const unsigned char c = (unsigned char)s[i];
    size_t n = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 1)));
    return std::min(n, s.size() - i);
}

}  // namespace

// Esquinas redondeadas fila a fila: cada píxel se pinta una sola vez (así la
// transparencia no se acumula) y el borde lleva un poco de suavizado.
void roundRect(Painter& p, int x, int y, int w, int h, int r, Color c) {
    if (w <= 0 || h <= 0) return;
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    if (h > 2 * r) p.rect(x, y + r, w, h - 2 * r, c);
    for (int j = 0; j < r; j++) {
        const float dy = (float)r - (float)j - 0.5f;
        const float inset = (float)r - std::sqrt(std::max(0.0f, (float)(r * r) - dy * dy));
        const int full = (int)std::ceil(inset);
        const float edge = (float)full - inset;   // parte cubierta del píxel del borde
        for (int pass = 0; pass < 2; pass++) {
            const int row = pass == 0 ? y + j : y + h - 1 - j;
            if (w - 2 * full > 0) p.rect(x + full, row, w - 2 * full, 1, c);
            if (full > 0 && edge > 0.05f) {
                const Color ec = scaleAlpha(c, edge);
                p.rect(x + full - 1, row, 1, 1, ec);
                p.rect(x + w - full, row, 1, 1, ec);
            }
        }
    }
}

void roundRectOutline(Painter& p, int x, int y, int w, int h, int r, int t, Color c) {
    if (w <= 0 || h <= 0 || t <= 0) return;
    r = std::max(t, std::min(r, std::min(w, h) / 2));
    // Lados rectos
    p.rect(x + r, y, w - 2 * r, t, c);
    p.rect(x + r, y + h - t, w - 2 * r, t, c);
    p.rect(x, y + r, t, h - 2 * r, c);
    p.rect(x + w - t, y + r, t, h - 2 * r, c);
    // Esquinas: anillo entre r - t y r, con suavizado
    const float ro = (float)r, ri = (float)(r - t);
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            const float dx = ro - (float)i - 0.5f, dy = ro - (float)j - 0.5f;
            const float d = std::sqrt(dx * dx + dy * dy);
            float cov = std::min(ro - d + 0.5f, d - ri + 0.5f);
            if (cov <= 0.05f) continue;
            if (cov > 1.0f) cov = 1.0f;
            const Color pc = scaleAlpha(c, cov);
            p.rect(x + i, y + j, 1, 1, pc);
            p.rect(x + w - 1 - i, y + j, 1, 1, pc);
            p.rect(x + i, y + h - 1 - j, 1, 1, pc);
            p.rect(x + w - 1 - i, y + h - 1 - j, 1, 1, pc);
        }
    }
}

void circle(Painter& p, int cx, int cy, int r, Color c) {
    if (r <= 0) return;
    for (int j = -r; j < r; j++) {
        const float dy = (float)j + 0.5f;
        const float half = std::sqrt(std::max(0.0f, (float)(r * r) - dy * dy));
        const int full = (int)std::floor(half);
        if (full > 0) p.rect(cx - full, cy + j, 2 * full, 1, c);
        const float edge = half - (float)full;
        if (edge > 0.05f) {
            const Color ec = scaleAlpha(c, edge);
            p.rect(cx - full - 1, cy + j, 1, 1, ec);
            p.rect(cx + full, cy + j, 1, 1, ec);
        }
    }
}

void icon(Painter& p, Icon ic, int x, int y, int size, Color c) {
    const std::vector<uint8_t>& m = iconMask(ic, size);
    p.mask(x, y, size, size, m.data(), c);
}

std::string ellipsize(Painter& p, const std::string& s, int size, int maxW) {
    if (maxW <= 0 || s.empty() || p.measure(s, size) <= maxW) return s;
    const std::string dots = "…";
    // Se quitan caracteres del final hasta que quepa con "…"
    std::vector<size_t> starts;
    for (size_t i = 0; i < s.size(); i += utf8Len(s, i)) starts.push_back(i);
    size_t lo = 0, hi = starts.size();   // número de caracteres que se conservan
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        const std::string t = s.substr(0, starts[mid - 1] + utf8Len(s, starts[mid - 1])) + dots;
        if (p.measure(t, size) <= maxW)
            lo = mid;
        else
            hi = mid - 1;
    }
    if (lo == 0) return dots;
    std::string out = s.substr(0, starts[lo - 1] + utf8Len(s, starts[lo - 1]));
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + dots;
}

int text(Painter& p, int x, int y, const std::string& s, int size, Color c, Align a, int maxW) {
    if (s.empty()) return 0;
    const std::string t = maxW > 0 ? ellipsize(p, s, size, maxW) : s;
    const int w = p.measure(t, size);
    const int dx = a == Align::Left ? x : (a == Align::Center ? x - w / 2 : x - w);
    p.text(dx, y, t, size, c);
    return w;
}

std::vector<std::string> wrap(Painter& p, const std::string& s, int size, int maxW) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        const std::string para = s.substr(pos, nl - pos);
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            if (sp == std::string::npos) sp = para.size();
            const std::string word = para.substr(i, sp - i);
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && p.measure(cand, size) > maxW) {
                lines.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            i = sp + 1;
        }
        lines.push_back(line);
        if (nl >= s.size()) break;
        pos = nl + 1;
    }
    return lines;
}

}  // namespace ovl
}  // namespace vnx
