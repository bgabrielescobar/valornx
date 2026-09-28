// ValorNX - iconos vectoriales sencillos, rasterizados a una máscara de transparencia.
// No dependen de ningún sistema de dibujo: los usan la app (Canvas) y el overlay.
#pragma once
#include <cstdint>
#include <vector>

namespace vnx {

enum class Icon {
    Search,
    Heart,
    Play,
    Star,
    Sliders,
    List,
    Undo,
    Close,
    Up,
    Down,
    NotEqual,
    Equal,
    Snowflake,
    Pencil,
    Backspace,
    Check,
    Trash,
    Bolt,
    Info,
    Warning,
    Save,
    Game,
    Pause,
    Range,
    Left,
    Right,
};

// Máscara size x size (0 = transparente, 255 = opaco). Se calcula una vez y se guarda.
const std::vector<uint8_t>& iconMask(Icon icon, int size);

}  // namespace vnx
