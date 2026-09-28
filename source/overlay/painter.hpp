// ValorNX (overlay) - dibujo sobre la capa del overlay.
//
// En la Switch lo hace libtesla (framebuffer RGBA4444 de 448x720); en PC, un emulador
// con las mismas reglas, para poder probar el overlay y sacar capturas.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../ui/iconmask.hpp"

namespace vnx {
namespace ovl {

// Color RGBA8 con el mismo orden que la app: 0xAABBGGRR.
using Color = uint32_t;
constexpr Color rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
    return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) | ((a & 0xFF) << 24);
}
constexpr Color withAlpha(Color c, uint32_t a) { return (c & 0x00FFFFFFu) | ((a & 0xFF) << 24); }

class Painter {
public:
    virtual ~Painter() = default;
    // Rellena toda la capa, sin mezclar (el fondo del overlay).
    virtual void clear(Color c) = 0;
    // Rectángulo mezclado con lo que haya debajo.
    virtual void rect(int x, int y, int w, int h, Color c) = 0;
    // Máscara de transparencia (iconos, bordes suaves): cada byte multiplica el alfa del color.
    virtual void mask(int x, int y, int w, int h, const uint8_t* alpha, Color c) = 0;
    // Texto con la parte de arriba de la línea en y. Devuelve el ancho dibujado.
    virtual int text(int x, int y, const std::string& s, int size, Color c) = 0;
    virtual int measure(const std::string& s, int size) = 0;
    virtual void pushClip(int x, int y, int w, int h) = 0;
    virtual void popClip() = 0;
};

enum class Align { Left, Center, Right };

void roundRect(Painter& p, int x, int y, int w, int h, int r, Color c);
void roundRectOutline(Painter& p, int x, int y, int w, int h, int r, int t, Color c);
void circle(Painter& p, int cx, int cy, int r, Color c);
void icon(Painter& p, Icon ic, int x, int y, int size, Color c);
// Texto alineado y recortado con "…" si no cabe en maxW (0 = sin límite). Devuelve el ancho.
int text(Painter& p, int x, int y, const std::string& s, int size, Color c, Align a = Align::Left, int maxW = 0);
std::string ellipsize(Painter& p, const std::string& s, int size, int maxW);
std::vector<std::string> wrap(Painter& p, const std::string& s, int size, int maxW);

}  // namespace ovl
}  // namespace vnx
