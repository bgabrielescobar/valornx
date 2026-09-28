// ValorNX - tipos de valor, lectura del número que escribe el usuario y formato.
#pragma once
#include <cstdint>
#include <string>

namespace vnx {

// Tipos de dato que se pueden buscar en la memoria del juego.
enum class VType : uint8_t { U8 = 0, U16 = 1, S32 = 2, S64 = 3, F32 = 4, F64 = 5 };
constexpr int kVTypeCount = 6;

using TypeMask = uint8_t;
constexpr TypeMask typeBit(VType t) { return (TypeMask)(1u << (unsigned)t); }
// "Automático": entero de 4 bytes + decimal (float). Cubre la gran mayoría de juegos.
constexpr TypeMask kMaskAuto = (TypeMask)((1u << (unsigned)VType::S32) | (1u << (unsigned)VType::F32));

int vtypeSize(VType t);
bool vtypeIsFloat(VType t);
const char* vtypeShort(VType t);   // "Entero", "Decimal", ...
const char* vtypeLabel(VType t);   // "Entero (4 bytes)", ...

// Opciones de tipo que se muestran al usuario (la 0 es "Automático").
struct TypeChoice {
    TypeMask mask;
    const char* label;
    const char* help;
};
int typeChoiceCount();
const TypeChoice& typeChoice(int index);
int typeChoiceForMask(TypeMask mask);   // -1 si no coincide con ninguna
std::string typeMaskLabel(TypeMask mask);

// Número tal y como lo escribió el usuario.
struct Number {
    bool valid = false;
    bool isDecimal = false;   // lleva punto decimal
    int decimals = 0;         // cifras tras el punto
    int64_t i = 0;            // valor entero (si !isDecimal)
    double d = 0.0;           // valor como double (siempre)
};
Number parseNumber(const std::string& text);

// Codifica el número en el tipo pedido. false si no cabe o no es representable.
bool encodeNumber(VType t, const Number& n, uint64_t& raw);

// Objetivo de búsqueda precalculado para cada tipo.
struct Target {
    bool ok[kVTypeCount] = {};
    uint64_t raw[kVTypeCount] = {};
    double lo = 0.0, hi = 0.0;   // rango abierto para decimales
    // "Entre dos números": los enteros valen si están en [imin, imax] (ambos incluidos).
    bool range = false;
    int64_t imin = 0, imax = 0;
};
Target makeTarget(const Number& n, TypeMask types);
// Rango entre dos números (da igual el orden). Los decimales llevan el mismo margen de
// redondeo que la búsqueda exacta.
Target makeRangeTarget(const Number& a, const Number& b, TypeMask types);
// ¿El valor (del tipo t) cumple el objetivo?
bool targetMatch(VType t, uint64_t raw, const Target& tg);
// Valor como entero con signo, tal y como se compara (U8/U16 sin signo, S32/S64 con signo).
int64_t rawAsInt(VType t, uint64_t raw);

uint64_t loadRaw(const uint8_t* p, VType t);
void storeRaw(uint8_t* p, VType t, uint64_t raw);
double rawAsDouble(VType t, uint64_t raw);
std::string formatRaw(VType t, uint64_t raw);   // para mostrar al usuario
std::string formatCount(uint64_t n);            // 1234567 -> "1.234.567"
std::string formatHex(uint64_t v, int digits = 0);
std::string formatBytes(uint64_t bytes);        // "1,5 GB"

}  // namespace vnx
