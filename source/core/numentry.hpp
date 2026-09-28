// ValorNX - número que se va escribiendo con el teclado numérico (app y overlay).
#pragma once
#include <string>

#include "value.hpp"

namespace vnx {

class NumberEntry {
public:
    NumberEntry() = default;
    // El texto inicial se muestra marcado: la primera cifra que se escriba lo sustituye.
    explicit NumberEntry(const std::string& initial) : m_text(initial), m_fresh(!initial.empty()) {}

    const std::string& text() const { return m_text; }
    bool empty() const { return m_text.empty(); }
    bool fresh() const { return m_fresh; }

    // Teclas: '0'-'9', '.', '-' (cambia el signo), '<' (borra una cifra), 'C' (limpia).
    // Devuelve un aviso para el usuario, o "" si todo fue bien.
    std::string press(char key, bool decimalsAllowed);
    // Valor rápido (99, 999...): sustituye lo escrito.
    void set(const std::string& text);

private:
    std::string m_text;
    bool m_fresh = false;
};

// Comprueba que el número se pueda escribir en un valor de tipo t. "" si es válido.
std::string numberErrorForType(VType t, const Number& n);

}  // namespace vnx
