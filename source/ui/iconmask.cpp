#include "iconmask.hpp"

#include <cmath>
#include <map>
#include <utility>

namespace vnx {

namespace {

struct Prim {
    enum Kind { Circle, Ring, Line, Poly } kind;
    bool sub;
    float a, b, c, d, e;
    std::vector<float> pts;
};

Prim circle(float cx, float cy, float r, bool sub = false) { return Prim{Prim::Circle, sub, cx, cy, r, 0, 0, {}}; }
Prim ring(float cx, float cy, float r, float t) { return Prim{Prim::Ring, false, cx, cy, r, t, 0, {}}; }
Prim line(float x0, float y0, float x1, float y1, float w, bool sub = false) {
    return Prim{Prim::Line, sub, x0, y0, x1, y1, w, {}};
}
Prim poly(std::vector<float> pts, bool sub = false) { return Prim{Prim::Poly, sub, 0, 0, 0, 0, 0, std::move(pts)}; }

bool hit(const Prim& p, float x, float y) {
    switch (p.kind) {
        case Prim::Circle: {
            const float dx = x - p.a, dy = y - p.b;
            return dx * dx + dy * dy <= p.c * p.c;
        }
        case Prim::Ring: {
            const float dx = x - p.a, dy = y - p.b;
            const float d = std::sqrt(dx * dx + dy * dy);
            return d <= p.c && d >= p.c - p.d;
        }
        case Prim::Line: {
            const float vx = p.c - p.a, vy = p.d - p.b;
            const float wx = x - p.a, wy = y - p.b;
            const float len2 = vx * vx + vy * vy;
            float t = len2 > 0 ? (wx * vx + wy * vy) / len2 : 0.0f;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            const float dx = x - (p.a + vx * t), dy = y - (p.b + vy * t);
            return dx * dx + dy * dy <= (p.e * 0.5f) * (p.e * 0.5f);
        }
        case Prim::Poly: {
            bool in = false;
            const size_t n = p.pts.size() / 2;
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const float xi = p.pts[i * 2], yi = p.pts[i * 2 + 1];
                const float xj = p.pts[j * 2], yj = p.pts[j * 2 + 1];
                if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) in = !in;
            }
            return in;
        }
    }
    return false;
}

std::vector<Prim> shape(Icon icon) {
    switch (icon) {
        case Icon::Search: return {ring(0.42f, 0.42f, 0.30f, 0.10f), line(0.63f, 0.63f, 0.88f, 0.88f, 0.15f)};
        case Icon::Heart:
            return {circle(0.30f, 0.36f, 0.21f), circle(0.70f, 0.36f, 0.21f),
                    poly({0.10f, 0.42f, 0.90f, 0.42f, 0.50f, 0.88f})};
        case Icon::Play: return {poly({0.28f, 0.14f, 0.86f, 0.50f, 0.28f, 0.86f})};
        case Icon::Star: {
            std::vector<float> pts;
            for (int i = 0; i < 10; i++) {
                const float ang = -1.5707963f + (float)i * 0.6283185f;
                const float r = (i % 2 == 0) ? 0.47f : 0.20f;
                pts.push_back(0.5f + r * std::cos(ang));
                pts.push_back(0.54f + r * std::sin(ang));
            }
            return {poly(pts)};
        }
        case Icon::Sliders:
            return {line(0.12f, 0.26f, 0.88f, 0.26f, 0.08f), line(0.12f, 0.50f, 0.88f, 0.50f, 0.08f),
                    line(0.12f, 0.74f, 0.88f, 0.74f, 0.08f), circle(0.34f, 0.26f, 0.11f),
                    circle(0.66f, 0.50f, 0.11f), circle(0.44f, 0.74f, 0.11f)};
        case Icon::List:
            return {circle(0.16f, 0.25f, 0.075f), line(0.34f, 0.25f, 0.88f, 0.25f, 0.11f),
                    circle(0.16f, 0.50f, 0.075f), line(0.34f, 0.50f, 0.88f, 0.50f, 0.11f),
                    circle(0.16f, 0.75f, 0.075f), line(0.34f, 0.75f, 0.88f, 0.75f, 0.11f)};
        case Icon::Undo: return {line(0.32f, 0.50f, 0.86f, 0.50f, 0.14f), poly({0.08f, 0.50f, 0.44f, 0.18f, 0.44f, 0.82f})};
        case Icon::Close:
            return {line(0.20f, 0.20f, 0.80f, 0.80f, 0.14f), line(0.80f, 0.20f, 0.20f, 0.80f, 0.14f)};
        case Icon::Up: return {line(0.50f, 0.88f, 0.50f, 0.40f, 0.15f), poly({0.50f, 0.08f, 0.86f, 0.48f, 0.14f, 0.48f})};
        case Icon::Down:
            return {line(0.50f, 0.12f, 0.50f, 0.60f, 0.15f), poly({0.50f, 0.92f, 0.86f, 0.52f, 0.14f, 0.52f})};
        case Icon::NotEqual:
            return {line(0.14f, 0.38f, 0.86f, 0.38f, 0.12f), line(0.14f, 0.62f, 0.86f, 0.62f, 0.12f),
                    line(0.66f, 0.12f, 0.34f, 0.88f, 0.11f)};
        case Icon::Equal: return {line(0.14f, 0.36f, 0.86f, 0.36f, 0.13f), line(0.14f, 0.64f, 0.86f, 0.64f, 0.13f)};
        case Icon::Snowflake:
            return {line(0.50f, 0.06f, 0.50f, 0.94f, 0.10f), line(0.12f, 0.28f, 0.88f, 0.72f, 0.10f),
                    line(0.12f, 0.72f, 0.88f, 0.28f, 0.10f), circle(0.50f, 0.50f, 0.12f),
                    line(0.38f, 0.14f, 0.50f, 0.24f, 0.07f), line(0.62f, 0.14f, 0.50f, 0.24f, 0.07f),
                    line(0.38f, 0.86f, 0.50f, 0.76f, 0.07f), line(0.62f, 0.86f, 0.50f, 0.76f, 0.07f)};
        case Icon::Pencil:
            return {line(0.36f, 0.64f, 0.80f, 0.20f, 0.20f), poly({0.10f, 0.90f, 0.20f, 0.58f, 0.42f, 0.80f})};
        case Icon::Backspace:
            return {poly({0.03f, 0.50f, 0.30f, 0.15f, 0.97f, 0.15f, 0.97f, 0.85f, 0.30f, 0.85f}),
                    line(0.47f, 0.33f, 0.79f, 0.67f, 0.10f, true), line(0.79f, 0.33f, 0.47f, 0.67f, 0.10f, true)};
        case Icon::Check:
            return {line(0.14f, 0.52f, 0.40f, 0.78f, 0.15f), line(0.40f, 0.78f, 0.88f, 0.24f, 0.15f)};
        case Icon::Trash:
            return {poly({0.22f, 0.30f, 0.78f, 0.30f, 0.72f, 0.92f, 0.28f, 0.92f}), line(0.12f, 0.20f, 0.88f, 0.20f, 0.09f),
                    line(0.40f, 0.09f, 0.60f, 0.09f, 0.09f), line(0.41f, 0.42f, 0.42f, 0.80f, 0.06f, true),
                    line(0.59f, 0.42f, 0.58f, 0.80f, 0.06f, true)};
        case Icon::Bolt:
            return {poly({0.60f, 0.03f, 0.16f, 0.57f, 0.46f, 0.57f, 0.36f, 0.97f, 0.84f, 0.41f, 0.54f, 0.41f, 0.68f, 0.03f})};
        case Icon::Info:
            return {ring(0.50f, 0.50f, 0.46f, 0.09f), circle(0.50f, 0.30f, 0.07f), line(0.50f, 0.46f, 0.50f, 0.74f, 0.12f)};
        case Icon::Warning:
            return {poly({0.50f, 0.05f, 0.97f, 0.90f, 0.03f, 0.90f}), line(0.50f, 0.36f, 0.50f, 0.60f, 0.11f, true),
                    circle(0.50f, 0.75f, 0.065f, true)};
        case Icon::Save:
            return {line(0.50f, 0.14f, 0.50f, 0.86f, 0.16f), line(0.14f, 0.50f, 0.86f, 0.50f, 0.16f)};
        case Icon::Pause: return {line(0.34f, 0.20f, 0.34f, 0.80f, 0.19f), line(0.66f, 0.20f, 0.66f, 0.80f, 0.19f)};
        case Icon::Game:
            return {circle(0.28f, 0.52f, 0.25f), circle(0.72f, 0.52f, 0.25f),
                    poly({0.28f, 0.27f, 0.72f, 0.27f, 0.72f, 0.77f, 0.28f, 0.77f}),
                    line(0.17f, 0.52f, 0.39f, 0.52f, 0.08f, true), line(0.28f, 0.41f, 0.28f, 0.63f, 0.08f, true),
                    circle(0.66f, 0.45f, 0.05f, true), circle(0.77f, 0.57f, 0.05f, true)};
        case Icon::Range:
            // Dos topes con la zona de en medio marcada: "entre esto y esto"
            return {line(0.12f, 0.22f, 0.12f, 0.78f, 0.12f), line(0.88f, 0.22f, 0.88f, 0.78f, 0.12f),
                    line(0.26f, 0.50f, 0.74f, 0.50f, 0.12f), poly({0.22f, 0.50f, 0.40f, 0.34f, 0.40f, 0.66f}),
                    poly({0.78f, 0.50f, 0.60f, 0.34f, 0.60f, 0.66f})};
        case Icon::Left: return {poly({0.72f, 0.12f, 0.72f, 0.88f, 0.22f, 0.50f})};
        case Icon::Right: return {poly({0.28f, 0.12f, 0.78f, 0.50f, 0.28f, 0.88f})};
    }
    return {};
}

}  // namespace

const std::vector<uint8_t>& iconMask(Icon icon, int size) {
    static std::map<std::pair<int, int>, std::vector<uint8_t>> cache;
    const auto key = std::make_pair((int)icon, size);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const std::vector<Prim> prims = shape(icon);
    std::vector<uint8_t> m((size_t)size * size, 0);
    constexpr int kSS = 4;
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            int count = 0;
            for (int sy = 0; sy < kSS; sy++) {
                for (int sx = 0; sx < kSS; sx++) {
                    const float x = ((float)i + ((float)sx + 0.5f) / kSS) / (float)size;
                    const float y = ((float)j + ((float)sy + 0.5f) / kSS) / (float)size;
                    bool in = false;
                    for (const Prim& p : prims)
                        if (!p.sub && hit(p, x, y)) {
                            in = true;
                            break;
                        }
                    if (in)
                        for (const Prim& p : prims)
                            if (p.sub && hit(p, x, y)) {
                                in = false;
                                break;
                            }
                    if (in) count++;
                }
            }
            m[(size_t)j * size + i] = (uint8_t)(count * 255 / (kSS * kSS));
        }
    }
    return cache.emplace(key, std::move(m)).first->second;
}

}  // namespace vnx
