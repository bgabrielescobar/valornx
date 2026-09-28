// ValorNX - entrada (botones y pantalla táctil) independiente de la plataforma.
#pragma once
#include <cstdint>

namespace vnx {

enum Btn : uint32_t {
    BTN_A = 1u << 0,
    BTN_B = 1u << 1,
    BTN_X = 1u << 2,
    BTN_Y = 1u << 3,
    BTN_L = 1u << 4,
    BTN_R = 1u << 5,
    BTN_ZL = 1u << 6,
    BTN_ZR = 1u << 7,
    BTN_PLUS = 1u << 8,
    BTN_MINUS = 1u << 9,
    BTN_UP = 1u << 10,
    BTN_DOWN = 1u << 11,
    BTN_LEFT = 1u << 12,
    BTN_RIGHT = 1u << 13,
};

struct Input {
    uint32_t down = 0;   // recién pulsados (las direcciones se repiten al mantener)
    uint32_t held = 0;
    bool tap = false;    // toque corto en la pantalla (al levantar el dedo)
    int tapX = 0, tapY = 0;
    bool touching = false;
    int touchX = 0, touchY = 0;
    int dragY = 0;       // cuánto se movió el dedo en vertical este frame
    char typed = 0;      // carácter escrito con teclado (0 si no hay)
};

class InputFilter {
public:
    Input process(uint32_t held, bool touching, int tx, int ty, char typed, uint64_t nowMs);

private:
    uint32_t m_prev = 0;
    uint64_t m_dirSince = 0, m_lastRepeat = 0;
    bool m_wasTouching = false, m_dragging = false;
    int m_startX = 0, m_startY = 0, m_lastX = 0, m_lastY = 0;
};

inline bool hitRect(int px, int py, int x, int y, int w, int h) { return px >= x && py >= y && px < x + w && py < y + h; }

}  // namespace vnx
