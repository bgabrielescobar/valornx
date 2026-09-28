#include "icons.hpp"

namespace vnx {

void drawIcon(Canvas& c, Icon icon, int x, int y, int size, Color color) {
    const std::vector<uint8_t>& m = iconMask(icon, size);
    c.blitMask(x, y, size, size, m.data(), color);
}

}  // namespace vnx
