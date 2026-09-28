#include "numentry.hpp"

namespace vnx {

std::string NumberEntry::press(char key, bool decimalsAllowed) {
    switch (key) {
        case 'C':
            m_text.clear();
            m_fresh = false;
            break;
        case '<':
            m_fresh = false;
            if (!m_text.empty()) m_text.pop_back();
            break;
        case '-':
            if (m_fresh) {
                m_fresh = false;
                m_text.clear();
            }
            if (!m_text.empty() && m_text[0] == '-')
                m_text.erase(0, 1);
            else
                m_text.insert(0, "-");
            break;
        case '.':
            if (!decimalsAllowed) return "Este tipo de valor no admite decimales.";
            if (m_fresh) {
                m_fresh = false;
                m_text.clear();
            }
            if (m_text.find('.') == std::string::npos) {
                if (m_text.empty() || m_text == "-") m_text += "0";
                m_text += '.';
            }
            break;
        default:
            if (key < '0' || key > '9') break;
            if (m_fresh) {
                m_fresh = false;
                m_text.clear();
            }
            if (m_text == "0")
                m_text.clear();
            else if (m_text == "-0")
                m_text = "-";
            if (m_text.size() < 18) m_text += key;
            break;
    }
    return "";
}

void NumberEntry::set(const std::string& text) {
    m_text = text;
    m_fresh = false;
}

std::string numberErrorForType(VType t, const Number& n) {
    if (!n.valid) return "Ese número no es válido.";
    uint64_t raw = 0;
    if (encodeNumber(t, n, raw)) return "";
    if (n.isDecimal && !vtypeIsFloat(t)) return "Este valor es un número entero: no admite decimales.";
    if (t == VType::U8) return "Para este tipo el número debe estar entre 0 y 255.";
    if (t == VType::U16) return "Para este tipo el número debe estar entre 0 y 65535.";
    return "El número es demasiado grande para este tipo de valor.";
}

}  // namespace vnx
