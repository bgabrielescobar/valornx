#include "value.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vnx {

int vtypeSize(VType t) {
    switch (t) {
        case VType::U8: return 1;
        case VType::U16: return 2;
        case VType::S32: return 4;
        case VType::S64: return 8;
        case VType::F32: return 4;
        case VType::F64: return 8;
    }
    return 4;
}

bool vtypeIsFloat(VType t) { return t == VType::F32 || t == VType::F64; }

const char* vtypeShort(VType t) {
    switch (t) {
        case VType::U8: return "Byte";
        case VType::U16: return "Entero 2B";
        case VType::S32: return "Entero";
        case VType::S64: return "Entero 8B";
        case VType::F32: return "Decimal";
        case VType::F64: return "Doble";
    }
    return "?";
}

const char* vtypeLabel(VType t) {
    switch (t) {
        case VType::U8: return "Byte (1 byte)";
        case VType::U16: return "Entero pequeño (2 bytes)";
        case VType::S32: return "Entero (4 bytes)";
        case VType::S64: return "Entero grande (8 bytes)";
        case VType::F32: return "Decimal (float)";
        case VType::F64: return "Decimal doble (double)";
    }
    return "?";
}

static const TypeChoice kChoices[] = {
    {kMaskAuto, "Automático",
     "Busca enteros de 4 bytes y decimales a la vez. Úsalo si no sabes cuál elegir."},
    {typeBit(VType::S32), "Entero (4 bytes)",
     "El más común: dinero, experiencia, puntos, objetos."},
    {typeBit(VType::F32), "Decimal (float)",
     "Vida o energía en juegos de acción, velocidades, tiempos."},
    {typeBit(VType::U16), "Entero pequeño (2 bytes)",
     "Estadísticas u objetos en algunos juegos (máximo 65.535)."},
    {typeBit(VType::U8), "Byte (1 byte)",
     "Cantidades pequeñas (máximo 255). Da muchísimos resultados."},
    {typeBit(VType::S64), "Entero grande (8 bytes)",
     "Cantidades enormes, por ejemplo dinero de más de 2.000 millones."},
    {typeBit(VType::F64), "Decimal doble (double)",
     "Poco común. Algunos juegos lo usan para dinero o tiempo."},
};

int typeChoiceCount() { return (int)(sizeof(kChoices) / sizeof(kChoices[0])); }

const TypeChoice& typeChoice(int index) {
    if (index < 0 || index >= typeChoiceCount()) index = 0;
    return kChoices[index];
}

int typeChoiceForMask(TypeMask mask) {
    for (int i = 0; i < typeChoiceCount(); i++)
        if (kChoices[i].mask == mask) return i;
    return -1;
}

std::string typeMaskLabel(TypeMask mask) {
    int c = typeChoiceForMask(mask);
    if (c >= 0) return kChoices[c].label;
    std::string s;
    for (int i = 0; i < kVTypeCount; i++) {
        if (mask & typeBit((VType)i)) {
            if (!s.empty()) s += " + ";
            s += vtypeShort((VType)i);
        }
    }
    return s.empty() ? "?" : s;
}

Number parseNumber(const std::string& text) {
    Number n;
    std::string s;
    for (char c : text)
        if (c != ' ') s += c;
    if (s.empty() || s.size() > 24) return n;

    size_t pos = 0;
    bool neg = false;
    if (s[0] == '-') {
        neg = true;
        pos = 1;
    }
    bool dot = false, overflow = false;
    int digitsInt = 0, digitsFrac = 0;
    int64_t ip = 0;
    for (; pos < s.size(); pos++) {
        char c = s[pos];
        if (c == '.') {
            if (dot) return n;
            dot = true;
            continue;
        }
        if (c < '0' || c > '9') return n;
        int d = c - '0';
        if (!dot) {
            digitsInt++;
            if (ip > (INT64_MAX - d) / 10)
                overflow = true;
            else
                ip = ip * 10 + d;
        } else {
            digitsFrac++;
        }
    }
    if (digitsInt == 0 && digitsFrac == 0) return n;
    n.isDecimal = dot;
    n.decimals = digitsFrac;
    n.d = strtod(s.c_str(), nullptr);
    if (!dot) {
        if (overflow) return n;
        n.i = neg ? -ip : ip;
    }
    n.valid = std::isfinite(n.d);
    return n;
}

bool encodeNumber(VType t, const Number& n, uint64_t& raw) {
    if (!n.valid) return false;
    switch (t) {
        case VType::U8:
            if (n.isDecimal || n.i < -128 || n.i > 255) return false;
            raw = (uint8_t)n.i;
            return true;
        case VType::U16:
            if (n.isDecimal || n.i < -32768 || n.i > 65535) return false;
            raw = (uint16_t)n.i;
            return true;
        case VType::S32:
            if (n.isDecimal || n.i < (int64_t)INT32_MIN || n.i > (int64_t)UINT32_MAX) return false;
            raw = (uint32_t)n.i;
            return true;
        case VType::S64:
            if (n.isDecimal) return false;
            raw = (uint64_t)n.i;
            return true;
        case VType::F32: {
            float f = (float)n.d;
            if (!std::isfinite(f)) return false;
            uint32_t u;
            memcpy(&u, &f, 4);
            raw = u;
            return true;
        }
        case VType::F64: {
            double d = n.d;
            uint64_t u;
            memcpy(&u, &d, 8);
            raw = u;
            return true;
        }
    }
    return false;
}

Target makeTarget(const Number& n, TypeMask types) {
    Target t;
    for (int i = 0; i < kVTypeCount; i++) {
        VType vt = (VType)i;
        if (!(types & typeBit(vt))) continue;
        t.ok[i] = encodeNumber(vt, n, t.raw[i]);
    }
    // En decimales se acepta un margen: lo que se ve en pantalla suele estar redondeado.
    double tol = 1.0;
    for (int k = 0; k < n.decimals; k++) tol /= 10.0;
    t.lo = n.d - tol;
    t.hi = n.d + tol;
    return t;
}

static double roundingTolerance(const Number& n) {
    double tol = 1.0;
    for (int k = 0; k < n.decimals; k++) tol /= 10.0;
    return tol;
}

// Límites enteros de un número escrito por el usuario (redondeando hacia dentro del rango).
static int64_t boundInt(const Number& n, bool isMin) {
    if (!n.isDecimal) return n.i;
    const double v = isMin ? std::ceil(n.d) : std::floor(n.d);
    if (v <= -9.2e18) return INT64_MIN;
    if (v >= 9.2e18) return INT64_MAX;
    return (int64_t)v;
}

static bool lessNumber(const Number& a, const Number& b) {
    if (!a.isDecimal && !b.isDecimal) return a.i < b.i;
    return a.d < b.d;
}

Target makeRangeTarget(const Number& a, const Number& b, TypeMask types) {
    Target t;
    t.range = true;
    if (!a.valid || !b.valid) return t;
    const Number& lo = lessNumber(b, a) ? b : a;
    const Number& hi = lessNumber(b, a) ? a : b;
    t.imin = boundInt(lo, true);
    t.imax = boundInt(hi, false);
    t.lo = lo.d - roundingTolerance(lo);
    t.hi = hi.d + roundingTolerance(hi);
    for (int i = 0; i < kVTypeCount; i++) {
        const VType vt = (VType)i;
        if (!(types & typeBit(vt))) continue;
        int64_t tmin = 0, tmax = 0;
        switch (vt) {
            case VType::U8: tmax = 0xFF; break;
            case VType::U16: tmax = 0xFFFF; break;
            case VType::S32:
                tmin = INT32_MIN;
                tmax = INT32_MAX;
                break;
            case VType::S64:
                tmin = INT64_MIN;
                tmax = INT64_MAX;
                break;
            case VType::F32:
            case VType::F64: t.ok[i] = std::isfinite(t.lo) && std::isfinite(t.hi); continue;
        }
        t.ok[i] = t.imin <= t.imax && t.imax >= tmin && t.imin <= tmax;
    }
    return t;
}

int64_t rawAsInt(VType t, uint64_t raw) {
    switch (t) {
        case VType::U8: return (uint8_t)raw;
        case VType::U16: return (uint16_t)raw;
        case VType::S32: return (int32_t)(uint32_t)raw;
        default: return (int64_t)raw;
    }
}

bool targetMatch(VType t, uint64_t raw, const Target& tg) {
    if (!tg.ok[(int)t]) return false;
    if (vtypeIsFloat(t)) {
        const double d = rawAsDouble(t, raw);
        return d > tg.lo && d < tg.hi;   // NaN nunca cumple
    }
    if (tg.range) {
        const int64_t v = rawAsInt(t, raw);
        return v >= tg.imin && v <= tg.imax;
    }
    return raw == tg.raw[(int)t];
}

uint64_t loadRaw(const uint8_t* p, VType t) {
    switch (vtypeSize(t)) {
        case 1: return p[0];
        case 2: {
            uint16_t v;
            memcpy(&v, p, 2);
            return v;
        }
        case 4: {
            uint32_t v;
            memcpy(&v, p, 4);
            return v;
        }
        default: {
            uint64_t v;
            memcpy(&v, p, 8);
            return v;
        }
    }
}

void storeRaw(uint8_t* p, VType t, uint64_t raw) {
    switch (vtypeSize(t)) {
        case 1: p[0] = (uint8_t)raw; break;
        case 2: {
            uint16_t v = (uint16_t)raw;
            memcpy(p, &v, 2);
            break;
        }
        case 4: {
            uint32_t v = (uint32_t)raw;
            memcpy(p, &v, 4);
            break;
        }
        default: memcpy(p, &raw, 8); break;
    }
}

double rawAsDouble(VType t, uint64_t raw) {
    switch (t) {
        case VType::U8: return (double)(uint8_t)raw;
        case VType::U16: return (double)(uint16_t)raw;
        case VType::S32: return (double)(int32_t)(uint32_t)raw;
        case VType::S64: return (double)(int64_t)raw;
        case VType::F32: {
            uint32_t u = (uint32_t)raw;
            float f;
            memcpy(&f, &u, 4);
            return f;
        }
        case VType::F64: {
            double d;
            memcpy(&d, &raw, 8);
            return d;
        }
    }
    return 0.0;
}

static std::string formatDouble(double x) {
    if (std::isnan(x)) return "NaN";
    if (std::isinf(x)) return x < 0 ? "-Inf" : "Inf";
    char buf[64];
    if (std::fabs(x) >= 1e12) {
        snprintf(buf, sizeof(buf), "%.4e", x);
        return buf;
    }
    snprintf(buf, sizeof(buf), "%.3f", x);
    std::string s = buf;
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

std::string formatRaw(VType t, uint64_t raw) {
    char buf[48];
    switch (t) {
        case VType::U8: snprintf(buf, sizeof(buf), "%u", (unsigned)(uint8_t)raw); return buf;
        case VType::U16: snprintf(buf, sizeof(buf), "%u", (unsigned)(uint16_t)raw); return buf;
        case VType::S32: snprintf(buf, sizeof(buf), "%d", (int)(int32_t)(uint32_t)raw); return buf;
        case VType::S64: snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)raw); return buf;
        case VType::F32:
        case VType::F64: return formatDouble(rawAsDouble(t, raw));
    }
    return "?";
}

std::string formatCount(uint64_t n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)n);
    std::string digits = buf, out;
    int len = (int)digits.size();
    for (int i = 0; i < len; i++) {
        out += digits[i];
        int rest = len - 1 - i;
        if (rest > 0 && rest % 3 == 0) out += '.';
    }
    return out;
}

std::string formatHex(uint64_t v, int digits) {
    char buf[32];
    if (digits > 0)
        snprintf(buf, sizeof(buf), "%0*llX", digits, (unsigned long long)v);
    else
        snprintf(buf, sizeof(buf), "%llX", (unsigned long long)v);
    return buf;
}

std::string formatBytes(uint64_t bytes) {
    char buf[32];
    const double kb = 1024.0, mb = kb * 1024.0, gb = mb * 1024.0;
    if (bytes >= (uint64_t)gb)
        snprintf(buf, sizeof(buf), "%.1f GB", bytes / gb);
    else if (bytes >= (uint64_t)mb)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / mb);
    else
        snprintf(buf, sizeof(buf), "%.0f KB", bytes / kb);
    std::string s = buf;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

}  // namespace vnx
