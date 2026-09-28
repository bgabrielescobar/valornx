#include "input.hpp"

#include <cstdlib>

namespace vnx {

Input InputFilter::process(uint32_t held, bool touching, int tx, int ty, char typed, uint64_t nowMs) {
    constexpr uint32_t kDirs = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT;
    constexpr uint64_t kDelay = 380, kRepeat = 75;

    Input in;
    in.held = held;
    in.down = held & ~m_prev;
    in.typed = typed;

    // Repetición automática de las direcciones al mantenerlas.
    const uint32_t dirs = held & kDirs;
    if (dirs == 0 || (in.down & kDirs)) {
        m_dirSince = nowMs;
        m_lastRepeat = nowMs;
    } else if (nowMs - m_dirSince >= kDelay && nowMs - m_lastRepeat >= kRepeat) {
        in.down |= dirs;
        m_lastRepeat = nowMs;
    }
    m_prev = held;

    // Pantalla táctil: toque corto = pulsar; mover el dedo = desplazar.
    in.touching = touching;
    in.touchX = tx;
    in.touchY = ty;
    if (touching) {
        if (!m_wasTouching) {
            m_startX = m_lastX = tx;
            m_startY = m_lastY = ty;
            m_dragging = false;
        } else {
            if (std::abs(ty - m_startY) > 22 || std::abs(tx - m_startX) > 22) m_dragging = true;
            if (m_dragging) in.dragY = ty - m_lastY;
            m_lastX = tx;
            m_lastY = ty;
        }
    } else if (m_wasTouching) {
        if (!m_dragging) {
            in.tap = true;
            in.tapX = m_lastX;
            in.tapY = m_lastY;
        }
        m_dragging = false;
    }
    m_wasTouching = touching;
    return in;
}

}  // namespace vnx
