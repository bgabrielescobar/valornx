#include "search.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <type_traits>

#include "fsutil.hpp"

namespace vnx {

namespace {

// App: 1 MiB por lectura, listas de 65536 resultados (1 MiB). Overlay: una octava parte.
constexpr SearchLimits kNormalLimits{0x100000, 0x10000, 65536, 65536, 0x40000};
constexpr SearchLimits kLowLimits{0x20000, 0x1000, 4096, 4096, 0x10000};
constexpr size_t kPage = 0x1000;
constexpr uint64_t kWindowGap = 0x4000;           // hueco máximo para juntar lecturas
constexpr uint64_t kSnapFileMax = 0x40000000ull;  // 1 GiB por archivo (FAT32 admite 4 GiB)
uint64_t kMaxCandidates = 40000000ull;            // 640 MB en la microSD
constexpr uint64_t kSpaceMargin = 64ull << 20;

const char* kTooMany =
    "Hay demasiados resultados. Es un valor muy común (como 0 o 1): "
    "cámbialo en el juego a un número más raro y busca otra vez.";
const char* kTooManyRange =
    "Hay demasiados resultados: en la memoria hay muchísimos números en ese rango. Usa un rango más "
    "pequeño o elige el tipo exacto (Entero o Decimal) en vez de Automático.";
const char* kWriteError = "No se pudo escribir en la microSD. ¿Está llena?";

struct SnapSeg {
    uint64_t addr, size;
    uint32_t file, pad;
    uint64_t fileOff;
};

bool candLess(const Candidate& a, const Candidate& b) {
    uint64_t aa = candAddr(a), ba = candAddr(b);
    if (aa != ba) return aa < ba;
    return candType(a) < candType(b);
}

int maskCount(TypeMask m) {
    int n = 0;
    for (int i = 0; i < kVTypeCount; i++)
        if (m & typeBit((VType)i)) n++;
    return n;
}

// ---------------------------------------------------------------- archivos

class CandWriter {
public:
    ~CandWriter() {
        if (m_f) fclose(m_f);
    }
    bool open(const std::string& path, size_t bufCount) {
        m_path = path;
        m_f = fopen(path.c_str(), "wb");
        m_cap = bufCount ? bufCount : 1;
        m_buf.reserve(m_cap);
        return m_f != nullptr;
    }
    void push(const Candidate& c) {
        m_buf.push_back(c);
        m_count++;
        if (m_buf.size() >= m_cap) flush();
    }
    bool flush() {
        if (!m_f) return false;
        if (!m_buf.empty()) {
            if (fwrite(m_buf.data(), sizeof(Candidate), m_buf.size(), m_f) != m_buf.size()) m_err = true;
            m_buf.clear();
        }
        return !m_err;
    }
    bool close() {
        bool ok = flush();
        if (m_f && fclose(m_f) != 0) ok = false;
        m_f = nullptr;
        return ok && !m_err;
    }
    void discard() {
        if (m_f) fclose(m_f);
        m_f = nullptr;
        if (!m_path.empty()) removeFile(m_path);
    }
    uint64_t count() const { return m_count; }
    bool failed() const { return m_err; }

private:
    FILE* m_f = nullptr;
    std::string m_path;
    std::vector<Candidate> m_buf;
    size_t m_cap = 1;
    uint64_t m_count = 0;
    bool m_err = false;
};

class CandReader {
public:
    ~CandReader() {
        if (m_f) fclose(m_f);
    }
    bool open(const std::string& path) {
        m_f = fopen(path.c_str(), "rb");
        return m_f != nullptr;
    }
    size_t next(std::vector<Candidate>& out, size_t max) {
        out.resize(max);
        size_t n = m_f ? fread(out.data(), sizeof(Candidate), max, m_f) : 0;
        out.resize(n);
        return n;
    }

private:
    FILE* m_f = nullptr;
};

// ---------------------------------------------------------------- memoria

// Lee un bloque. Si falla entero, lo intenta página a página; las páginas
// ilegibles quedan a cero y marcadas en 'bad'. Devuelve cuántas fallaron.
size_t readRobust(IGame& g, uint64_t addr, uint8_t* buf, size_t size, std::vector<uint8_t>& bad) {
    bad.clear();
    if (g.read(addr, buf, size)) return 0;
    size_t pages = (size + kPage - 1) / kPage;
    bad.assign(pages, 0);
    size_t nbad = 0, streak = 0;
    for (size_t p = 0; p < pages; p++) {
        size_t off = p * kPage;
        size_t len = std::min(kPage, size - off);
        // Si fallan muchas páginas seguidas, el resto del bloque se da por ilegible.
        const bool ok = streak < 16 && g.read(addr + off, buf + off, len);
        if (!ok) {
            memset(buf + off, 0, len);
            bad[p] = 1;
            nbad++;
            streak++;
        } else {
            streak = 0;
        }
    }
    return nbad;
}

bool onBadPage(const std::vector<uint8_t>& bad, uint64_t chunkAddr, uint64_t addr) {
    if (bad.empty()) return false;
    size_t p = (size_t)((addr - chunkAddr) / kPage);
    return p < bad.size() && bad[p];
}

// ---------------------------------------------------------------- comparaciones

// Lee un T de la memoria. Con Swap, con los bytes al revés (tipos BE de los ports).
template <typename T, bool Swap>
inline T loadT(const uint8_t* p) {
    T v;
    if (Swap) {
        uint8_t b[sizeof(T)];
        for (size_t k = 0; k < sizeof(T); k++) b[k] = p[sizeof(T) - 1 - k];
        memcpy(&v, b, sizeof(T));
    } else {
        memcpy(&v, p, sizeof(T));
    }
    return v;
}

template <typename T, bool Swap = false>
void scanEq(const uint8_t* data, size_t len, uint64_t base, VType vt, T want, std::vector<Candidate>& out) {
    const size_t n = len / sizeof(T);
    for (size_t i = 0; i < n; i++) {
        const T v = loadT<T, Swap>(data + i * sizeof(T));
        if (v == want) out.push_back(makeCand(base + i * sizeof(T), vt, (uint64_t)v));
    }
}

template <typename F, typename U, bool Swap = false>
void scanRange(const uint8_t* data, size_t len, uint64_t base, VType vt, double lo, double hi,
               std::vector<Candidate>& out) {
    const size_t n = len / sizeof(F);
    for (size_t i = 0; i < n; i++) {
        const F v = loadT<F, Swap>(data + i * sizeof(F));
        const double d = (double)v;
        if (d > lo && d < hi) {
            U u;
            memcpy(&u, &v, sizeof(F));
            out.push_back(makeCand(base + i * sizeof(F), vt, (uint64_t)u));
        }
    }
}

// Enteros entre dos límites (ambos incluidos), comparando en el tipo nativo.
template <typename T, bool Swap = false>
void scanBetween(const uint8_t* data, size_t len, uint64_t base, VType vt, int64_t lo64, int64_t hi64,
                 std::vector<Candidate>& out) {
    const int64_t tmin = (int64_t)std::numeric_limits<T>::min();
    const int64_t tmax = std::is_same<T, uint64_t>::value ? INT64_MAX : (int64_t)std::numeric_limits<T>::max();
    const T lo = (T)std::max(lo64, tmin), hi = (T)std::min(hi64, tmax);
    const size_t n = len / sizeof(T);
    for (size_t i = 0; i < n; i++) {
        const T v = loadT<T, Swap>(data + i * sizeof(T));
        if (v >= lo && v <= hi) {
            uint64_t raw = 0;
            memcpy(&raw, &v, sizeof(T));
            out.push_back(makeCand(base + i * sizeof(T), vt, raw));
        }
    }
}

// Busca el valor objetivo en un bloque (todos los tipos pedidos).
void scanTarget(const uint8_t* data, size_t len, uint64_t base, TypeMask types, const Target& tg,
                std::vector<Candidate>& out) {
    size_t first = out.size();
    for (int i = 0; i < kVTypeCount; i++) {
        VType vt = (VType)i;
        if (!(types & typeBit(vt)) || !tg.ok[i]) continue;
        if (tg.range) {
            switch (vt) {
                case VType::U8: scanBetween<uint8_t>(data, len, base, vt, tg.imin, tg.imax, out); break;
                case VType::U16: scanBetween<uint16_t>(data, len, base, vt, tg.imin, tg.imax, out); break;
                case VType::S32: scanBetween<int32_t>(data, len, base, vt, tg.imin, tg.imax, out); break;
                case VType::S64: scanBetween<int64_t>(data, len, base, vt, tg.imin, tg.imax, out); break;
                case VType::F32: scanRange<float, uint32_t>(data, len, base, vt, tg.lo, tg.hi, out); break;
                case VType::F64: scanRange<double, uint64_t>(data, len, base, vt, tg.lo, tg.hi, out); break;
                case VType::U16BE: scanBetween<uint16_t, true>(data, len, base, vt, tg.imin, tg.imax, out); break;
                case VType::S32BE: scanBetween<int32_t, true>(data, len, base, vt, tg.imin, tg.imax, out); break;
                case VType::F32BE: scanRange<float, uint32_t, true>(data, len, base, vt, tg.lo, tg.hi, out); break;
            }
            continue;
        }
        switch (vt) {
            case VType::U8: scanEq<uint8_t>(data, len, base, vt, (uint8_t)tg.raw[i], out); break;
            case VType::U16: scanEq<uint16_t>(data, len, base, vt, (uint16_t)tg.raw[i], out); break;
            case VType::S32: scanEq<uint32_t>(data, len, base, vt, (uint32_t)tg.raw[i], out); break;
            case VType::S64: scanEq<uint64_t>(data, len, base, vt, tg.raw[i], out); break;
            case VType::F32: scanRange<float, uint32_t>(data, len, base, vt, tg.lo, tg.hi, out); break;
            case VType::F64: scanRange<double, uint64_t>(data, len, base, vt, tg.lo, tg.hi, out); break;
            case VType::U16BE: scanEq<uint16_t, true>(data, len, base, vt, (uint16_t)tg.raw[i], out); break;
            case VType::S32BE: scanEq<uint32_t, true>(data, len, base, vt, (uint32_t)tg.raw[i], out); break;
            case VType::F32BE: scanRange<float, uint32_t, true>(data, len, base, vt, tg.lo, tg.hi, out); break;
        }
    }
    if (maskCount(types) > 1) std::sort(out.begin() + first, out.end(), candLess);
}

template <typename T, bool Swap = false, typename Cmp>
void cmpKernel(const uint8_t* o, const uint8_t* n, size_t len, uint64_t base, VType vt, Cmp cmp,
               std::vector<Candidate>& out) {
    const size_t cnt = len / sizeof(T);
    for (size_t i = 0; i < cnt; i++) {
        const T a = loadT<T, Swap>(o + i * sizeof(T));
        const T b = loadT<T, Swap>(n + i * sizeof(T));
        if (cmp(a, b)) {
            uint64_t raw = 0;
            memcpy(&raw, &b, sizeof(T));
            out.push_back(makeCand(base + i * sizeof(T), vt, raw));
        }
    }
}

template <typename Cmp>
void cmpTyped(VType vt, bool bitwise, const uint8_t* o, const uint8_t* n, size_t len, uint64_t base, Cmp cmp,
              std::vector<Candidate>& out) {
    switch (vt) {
        case VType::U8: cmpKernel<uint8_t>(o, n, len, base, vt, cmp, out); break;
        case VType::U16: cmpKernel<uint16_t>(o, n, len, base, vt, cmp, out); break;
        case VType::S32:
            if (bitwise)
                cmpKernel<uint32_t>(o, n, len, base, vt, cmp, out);
            else
                cmpKernel<int32_t>(o, n, len, base, vt, cmp, out);
            break;
        case VType::S64:
            if (bitwise)
                cmpKernel<uint64_t>(o, n, len, base, vt, cmp, out);
            else
                cmpKernel<int64_t>(o, n, len, base, vt, cmp, out);
            break;
        case VType::F32:
            if (bitwise)
                cmpKernel<uint32_t>(o, n, len, base, vt, cmp, out);
            else
                cmpKernel<float>(o, n, len, base, vt, cmp, out);
            break;
        case VType::F64:
            if (bitwise)
                cmpKernel<uint64_t>(o, n, len, base, vt, cmp, out);
            else
                cmpKernel<double>(o, n, len, base, vt, cmp, out);
            break;
        case VType::U16BE: cmpKernel<uint16_t, true>(o, n, len, base, vt, cmp, out); break;
        case VType::S32BE:
            if (bitwise)
                cmpKernel<uint32_t, true>(o, n, len, base, vt, cmp, out);
            else
                cmpKernel<int32_t, true>(o, n, len, base, vt, cmp, out);
            break;
        case VType::F32BE:
            if (bitwise)
                cmpKernel<uint32_t, true>(o, n, len, base, vt, cmp, out);
            else
                cmpKernel<float, true>(o, n, len, base, vt, cmp, out);
            break;
    }
}

// Compara memoria vieja (foto) con la actual según la relación.
void compareBlock(TypeMask types, Relation rel, const uint8_t* o, const uint8_t* n, size_t len, uint64_t base,
                  std::vector<Candidate>& out) {
    const size_t first = out.size();
    const bool skipSame = rel != Relation::Unchanged;   // zonas idénticas no pueden haber cambiado
    constexpr size_t kBlk = 64;
    for (size_t off = 0; off < len; off += kBlk) {
        const size_t bl = std::min(kBlk, len - off);
        if (skipSame && memcmp(o + off, n + off, bl) == 0) continue;
        for (int i = 0; i < kVTypeCount; i++) {
            VType vt = (VType)i;
            if (!(types & typeBit(vt))) continue;
            switch (rel) {
                case Relation::Increased:
                    cmpTyped(vt, false, o + off, n + off, bl, base + off, [](auto a, auto b) { return b > a; }, out);
                    break;
                case Relation::Decreased:
                    cmpTyped(vt, false, o + off, n + off, bl, base + off, [](auto a, auto b) { return b < a; }, out);
                    break;
                case Relation::Changed:
                    cmpTyped(vt, true, o + off, n + off, bl, base + off, [](auto a, auto b) { return a != b; }, out);
                    break;
                case Relation::Unchanged:
                    cmpTyped(vt, true, o + off, n + off, bl, base + off, [](auto a, auto b) { return a == b; }, out);
                    break;
                case Relation::Equal:
                case Relation::Between: break;
            }
        }
    }
    if (maskCount(types) > 1) std::sort(out.begin() + first, out.end(), candLess);
}

bool typedLess(VType t, uint64_t a, uint64_t b) {
    switch (t) {
        case VType::U8:
        case VType::U16:
        case VType::U16BE: return a < b;
        case VType::S32:
        case VType::S32BE: return (int32_t)(uint32_t)a < (int32_t)(uint32_t)b;
        case VType::S64: return (int64_t)a < (int64_t)b;
        case VType::F32:
        case VType::F64:
        case VType::F32BE: return rawAsDouble(t, a) < rawAsDouble(t, b);
    }
    return false;
}

bool relMatch(VType t, Relation rel, uint64_t oldRaw, uint64_t newRaw, const Target& tg) {
    switch (rel) {
        case Relation::Equal:
        case Relation::Between: return targetMatch(t, newRaw, tg);
        case Relation::Changed: return newRaw != oldRaw;
        case Relation::Unchanged: return newRaw == oldRaw;
        case Relation::Increased: return typedLess(t, oldRaw, newRaw);
        case Relation::Decreased: return typedLess(t, newRaw, oldRaw);
    }
    return false;
}

// Compara un bloque por trozos (así la lista temporal nunca crece mucho) y pasa los
// resultados al archivo, sin los de las páginas que no se pudieron leer.
template <typename Fn>
void scanSlices(size_t len, size_t slice, uint64_t addr, const std::vector<uint8_t>& bad, std::vector<Candidate>& tmp,
                CandWriter& w, Fn scan) {
    for (size_t off = 0; off < len; off += slice) {
        const size_t n = std::min(slice, len - off);
        tmp.clear();
        scan(off, n, tmp);
        for (const Candidate& c : tmp)
            if (!onBadPage(bad, addr, candAddr(c))) w.push(c);
    }
}

std::vector<MemRegion> wantedRegions(IGame& g, SearchZone zone) {
    std::vector<MemRegion> all, out;
    g.regions(all);
    for (const MemRegion& r : all)
        if (regionWanted(r, zone, g.info())) out.push_back(r);
    return out;
}

// ---------------------------------------------------------------- tareas

// Primera búsqueda con un valor conocido (o entre dos números).
class KnownScanTask : public Task {
public:
    KnownScanTask(SearchSession& s, IGame& g, TypeMask types, const Target& tg, const std::string& text,
                  SearchZone zone)
        : S(s), G(g), m_types(types), m_tg(tg), m_text(text), m_zone(zone) {
        m_title = "Buscando " + text + "...";
        m_regs = wantedRegions(g, zone);
        for (const MemRegion& r : m_regs) m_total += r.size;
        if (m_regs.empty() || m_total == 0) {
            fail("No se pudo leer la memoria del juego.");
            return;
        }
        m_gen = S.allocGen();
        if (!m_w.open(S.candPath(m_gen), S.limits().writerBuf)) {
            fail(kWriteError);
            return;
        }
        m_buf.resize(S.limits().chunk);
    }

    void step(uint32_t budgetMs) override {
        const uint64_t start = nowMs();
        const SearchLimits& L = S.limits();
        while (m_status == Status::Running) {
            if (m_cancel) {
                m_w.discard();
                m_status = Status::Cancelled;
                return;
            }
            if (m_ri >= m_regs.size()) {
                finish();
                return;
            }
            const MemRegion& r = m_regs[m_ri];
            const size_t len = (size_t)std::min<uint64_t>(L.chunk, r.size - m_off);
            const uint64_t addr = r.base + m_off;
            readRobust(G, addr, m_buf.data(), len, m_bad);
            scanSlices(len, L.slice, addr, m_bad, m_tmp, m_w, [&](size_t off, size_t n, std::vector<Candidate>& out) {
                scanTarget(m_buf.data() + off, n, addr + off, m_types, m_tg, out);
            });
            if (m_w.failed()) {
                fail(kWriteError);
                return;
            }
            m_off += len;
            m_done += len;
            if (m_w.count() > kMaxCandidates || projectedTooMany()) {
                fail(m_tg.range ? kTooManyRange : kTooMany);
                return;
            }
            if (m_off >= r.size) {
                m_ri++;
                m_off = 0;
            }
            m_found = m_w.count();
            m_progress = (float)((double)m_done / (double)m_total);
            if (nowMs() - start >= budgetMs) break;
        }
    }

private:
    SearchSession& S;
    IGame& G;
    TypeMask m_types;
    Target m_tg;
    std::string m_text;
    SearchZone m_zone;
    std::vector<MemRegion> m_regs;
    size_t m_ri = 0;
    uint64_t m_off = 0, m_total = 0, m_done = 0;
    std::vector<uint8_t> m_buf, m_bad;
    std::vector<Candidate> m_tmp;
    CandWriter m_w;
    uint32_t m_gen = 0;

    // Tras leer una octava parte de la memoria ya se ve si acabará con muchísimos más
    // resultados de los que caben: mejor avisar ya que escribir cientos de MB para nada.
    bool projectedTooMany() const {
        if (m_done < m_total / 8 || m_done == 0) return false;
        const double projected = (double)m_w.count() * (double)m_total / (double)m_done;
        return projected > 2.0 * (double)kMaxCandidates;
    }

    void fail(const std::string& e) {
        m_w.discard();
        m_error = e;
        m_status = Status::Failed;
    }
    void finish() {
        if (!m_w.close()) {
            fail(kWriteError);
            return;
        }
        StageInfo next;
        next.stage = Stage::Candidates;
        next.count = m_w.count();
        next.gen = m_gen;
        next.steps = 1;
        next.lastInput = m_text;
        S.commit(next, true, m_types, false, m_tg.range, m_zone);
        m_found = next.count;
        m_progress = 1.0f;
        m_status = Status::Done;
    }
};

// Primera búsqueda de un valor oculto: se guarda una "foto" de la memoria.
class SnapshotTask : public Task {
public:
    SnapshotTask(SearchSession& s, IGame& g, TypeMask types, SearchZone zone)
        : S(s), G(g), m_types(types), m_zone(zone) {
        m_title = "Guardando la memoria del juego...";
        m_foundIsBytes = true;
        m_regs = wantedRegions(g, zone);
        for (const MemRegion& r : m_regs) m_total += r.size;
        if (m_regs.empty() || m_total == 0) {
            fail("No se pudo leer la memoria del juego.");
            return;
        }
        const uint64_t avail = freeSpace(S.dir());
        if (avail != 0 && avail < m_total + kSpaceMargin) {
            fail("No hay espacio suficiente en la microSD: se necesitan " + formatBytes(m_total + kSpaceMargin) +
                 " libres y hay " + formatBytes(avail) + ". Prueba con la zona \"Solo heap\" en Ajustes.");
            return;
        }
        m_gen = S.allocGen();
        m_buf.resize(S.limits().chunk);
    }
    ~SnapshotTask() override {
        if (m_f) fclose(m_f);
    }

    void step(uint32_t budgetMs) override {
        const uint64_t start = nowMs();
        while (m_status == Status::Running) {
            if (m_cancel) {
                cleanup();
                m_status = Status::Cancelled;
                return;
            }
            if (m_ri >= m_regs.size()) {
                finish();
                return;
            }
            const MemRegion& r = m_regs[m_ri];
            const size_t len = (size_t)std::min<uint64_t>(S.limits().chunk, r.size - m_off);
            const uint64_t addr = r.base + m_off;
            // ¿Hace falta un archivo nuevo (inicio o se llenó el actual)?
            if (!m_f || m_fileSize + len > kSnapFileMax) {
                if (!openNextFile()) {
                    fail(kWriteError);
                    return;
                }
                m_newSeg = true;
            }
            if (m_off == 0) m_newSeg = true;
            if (m_newSeg) {
                SnapSeg sg{};
                sg.addr = addr;
                sg.size = 0;
                sg.file = m_fileIdx;
                sg.fileOff = m_fileSize;
                m_segs.push_back(sg);
                m_newSeg = false;
            }
            readRobust(G, addr, m_buf.data(), len, m_bad);
            if (fwrite(m_buf.data(), 1, len, m_f) != len) {
                fail(kWriteError);
                return;
            }
            m_fileSize += len;
            m_segs.back().size += len;
            m_off += len;
            m_done += len;
            if (m_off >= r.size) {
                m_ri++;
                m_off = 0;
            }
            m_found = m_done;
            m_progress = (float)((double)m_done / (double)m_total);
            if (nowMs() - start >= budgetMs) break;
        }
    }

private:
    SearchSession& S;
    IGame& G;
    TypeMask m_types;
    SearchZone m_zone;
    std::vector<MemRegion> m_regs;
    size_t m_ri = 0;
    uint64_t m_off = 0, m_total = 0, m_done = 0;
    std::vector<uint8_t> m_buf, m_bad;
    std::vector<SnapSeg> m_segs;
    FILE* m_f = nullptr;
    uint32_t m_fileIdx = 0;
    uint32_t m_filesOpened = 0;
    uint64_t m_fileSize = 0;
    bool m_newSeg = true;
    uint32_t m_gen = 0;

    bool openNextFile() {
        if (m_f) {
            if (fclose(m_f) != 0) {
                m_f = nullptr;
                return false;
            }
            m_f = nullptr;
            m_fileIdx++;
        }
        m_f = fopen(S.snapPath(m_gen, m_fileIdx).c_str(), "wb");
        m_fileSize = 0;
        m_filesOpened++;
        return m_f != nullptr;
    }
    void cleanup() {
        if (m_f) fclose(m_f);
        m_f = nullptr;
        for (uint32_t i = 0; i < m_filesOpened + 1; i++) removeFile(S.snapPath(m_gen, i));
        removeFile(S.snapIndexPath(m_gen));
    }
    void fail(const std::string& e) {
        if (m_gen) cleanup();
        m_error = e;
        m_status = Status::Failed;
    }
    void finish() {
        if (m_f && fclose(m_f) != 0) {
            m_f = nullptr;
            fail(kWriteError);
            return;
        }
        m_f = nullptr;
        FILE* idx = fopen(S.snapIndexPath(m_gen).c_str(), "wb");
        bool ok = idx != nullptr;
        if (ok) {
            const uint32_t hdr[3] = {0x53584E56u /* "VNXS" */, 1u, (uint32_t)m_segs.size()};
            ok = fwrite(hdr, sizeof(hdr), 1, idx) == 1;
            if (ok && !m_segs.empty()) ok = fwrite(m_segs.data(), sizeof(SnapSeg), m_segs.size(), idx) == m_segs.size();
            if (fclose(idx) != 0) ok = false;
        }
        if (!ok) {
            fail(kWriteError);
            return;
        }
        StageInfo next;
        next.stage = Stage::Snapshot;
        next.count = m_done;
        next.gen = m_gen;
        next.steps = 1;
        S.commit(next, true, m_types, true, false, m_zone);
        m_progress = 1.0f;
        m_status = Status::Done;
    }
};

bool loadSnapIndex(const std::string& path, std::vector<SnapSeg>& segs) {
    segs.clear();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t hdr[3];
    bool ok = fread(hdr, sizeof(hdr), 1, f) == 1 && hdr[0] == 0x53584E56u && hdr[1] == 1u && hdr[2] < 1000000u;
    if (ok) {
        segs.resize(hdr[2]);
        if (hdr[2]) ok = fread(segs.data(), sizeof(SnapSeg), hdr[2], f) == hdr[2];
    }
    fclose(f);
    return ok;
}

// Segundo paso de un valor oculto: compara la foto con la memoria actual.
class CompareSnapshotTask : public Task {
public:
    CompareSnapshotTask(SearchSession& s, IGame& g, Relation rel, const Target& tg, const std::string& text)
        : S(s), G(g), m_rel(rel), m_text(text), m_tg(tg) {
        m_title = "Comparando con la memoria guardada...";
        const SessionState& st = S.state();
        m_types = st.types;
        m_snapGen = st.cur.gen;
        if (!loadSnapIndex(S.snapIndexPath(m_snapGen), m_segs)) {
            fail("No se encontró la memoria guardada. Empieza la búsqueda de nuevo.");
            return;
        }
        for (const SnapSeg& sg : m_segs) m_total += sg.size;
        m_gen = S.allocGen();
        if (!m_w.open(S.candPath(m_gen), S.limits().writerBuf)) {
            fail(kWriteError);
            return;
        }
        m_old.resize(S.limits().chunk);
        m_new.resize(S.limits().chunk);
    }
    ~CompareSnapshotTask() override {
        if (m_f) fclose(m_f);
    }

    void step(uint32_t budgetMs) override {
        const uint64_t start = nowMs();
        const SearchLimits& L = S.limits();
        while (m_status == Status::Running) {
            if (m_cancel) {
                m_w.discard();
                m_status = Status::Cancelled;
                return;
            }
            if (m_si >= m_segs.size()) {
                finish();
                return;
            }
            const SnapSeg& sg = m_segs[m_si];
            const size_t len = (size_t)std::min<uint64_t>(L.chunk, sg.size - m_off);
            const uint64_t addr = sg.addr + m_off;
            if (!m_f || m_fileIdx != sg.file) {
                if (m_f) fclose(m_f);
                m_f = fopen(S.snapPath(m_snapGen, sg.file).c_str(), "rb");
                m_fileIdx = sg.file;
                if (!m_f) {
                    fail("No se pudo leer la memoria guardada en la microSD.");
                    return;
                }
            }
            if (fseek(m_f, (long)(sg.fileOff + m_off), SEEK_SET) != 0 || fread(m_old.data(), 1, len, m_f) != len) {
                fail("No se pudo leer la memoria guardada en la microSD.");
                return;
            }
            readRobust(G, addr, m_new.data(), len, m_bad);
            const bool target = m_rel == Relation::Equal || m_rel == Relation::Between;
            scanSlices(len, L.slice, addr, m_bad, m_tmp, m_w, [&](size_t off, size_t n, std::vector<Candidate>& out) {
                if (target)
                    scanTarget(m_new.data() + off, n, addr + off, m_types, m_tg, out);
                else
                    compareBlock(m_types, m_rel, m_old.data() + off, m_new.data() + off, n, addr + off, out);
            });
            if (m_w.failed()) {
                fail(kWriteError);
                return;
            }
            if (m_w.count() > kMaxCandidates) {
                fail("Hay demasiados resultados. Vuelve al juego, haz que el valor cambie y elige "
                     "\"Aumentó\" o \"Disminuyó\".");
                return;
            }
            m_off += len;
            m_done += len;
            if (m_off >= sg.size) {
                m_si++;
                m_off = 0;
            }
            m_found = m_w.count();
            m_progress = m_total ? (float)((double)m_done / (double)m_total) : 1.0f;
            if (nowMs() - start >= budgetMs) break;
        }
    }

private:
    SearchSession& S;
    IGame& G;
    Relation m_rel;
    std::string m_text;
    TypeMask m_types = 0;
    Target m_tg;
    uint32_t m_snapGen = 0, m_gen = 0;
    std::vector<SnapSeg> m_segs;
    size_t m_si = 0;
    uint64_t m_off = 0, m_total = 0, m_done = 0;
    FILE* m_f = nullptr;
    uint32_t m_fileIdx = 0;
    std::vector<uint8_t> m_old, m_new, m_bad;
    std::vector<Candidate> m_tmp;
    CandWriter m_w;

    void fail(const std::string& e) {
        m_w.discard();
        m_error = e;
        m_status = Status::Failed;
    }
    void finish() {
        if (m_f) fclose(m_f);
        m_f = nullptr;
        if (!m_w.close()) {
            fail(kWriteError);
            return;
        }
        StageInfo next;
        next.stage = Stage::Candidates;
        next.count = m_w.count();
        next.gen = m_gen;
        next.steps = S.state().cur.steps + 1;
        next.lastInput = m_text;
        S.commit(next, false, m_types, true, false, S.state().zone);
        m_found = next.count;
        m_progress = 1.0f;
        m_status = Status::Done;
    }
};

// Refinar una lista de resultados existente.
class RefineTask : public Task {
public:
    RefineTask(SearchSession& s, IGame& g, Relation rel, const Target& tg, const std::string& text)
        : S(s), G(g), m_rel(rel), m_text(text), m_tg(tg) {
        m_title = "Refinando resultados...";
        const SessionState& st = S.state();
        m_types = st.types;
        m_total = st.cur.count;
        std::vector<MemRegion> all;
        G.regions(all);
        for (const MemRegion& r : all)
            if (r.size && (r.perm & memperm::R)) m_regs.push_back(r);
        std::sort(m_regs.begin(), m_regs.end(), [](const MemRegion& a, const MemRegion& b) { return a.base < b.base; });
        if (!m_r.open(S.candPath(st.cur.gen))) {
            fail("No se encontraron los resultados anteriores. Empieza la búsqueda de nuevo.");
            return;
        }
        m_gen = S.allocGen();
        if (!m_w.open(S.candPath(m_gen), S.limits().writerBuf)) {
            fail(kWriteError);
            return;
        }
        m_win.resize(S.limits().windowMax + 16);
    }

    void step(uint32_t budgetMs) override {
        const uint64_t start = nowMs();
        while (m_status == Status::Running) {
            if (m_cancel) {
                m_w.discard();
                m_status = Status::Cancelled;
                return;
            }
            if (m_bi >= m_block.size()) {
                m_bi = 0;
                if (m_r.next(m_block, S.limits().readerBlock) == 0) {
                    finish();
                    return;
                }
            }
            processWindow();
            if (m_w.failed()) {
                fail(kWriteError);
                return;
            }
            m_found = m_w.count();
            m_progress = m_total ? (float)((double)m_done / (double)m_total) : 1.0f;
            if (nowMs() - start >= budgetMs) break;
        }
    }

private:
    SearchSession& S;
    IGame& G;
    Relation m_rel;
    std::string m_text;
    TypeMask m_types = 0;
    Target m_tg;
    std::vector<MemRegion> m_regs;
    size_t m_lastReg = 0;
    CandReader m_r;
    CandWriter m_w;
    std::vector<Candidate> m_block;
    size_t m_bi = 0;
    uint64_t m_total = 0, m_done = 0;
    std::vector<uint8_t> m_win;
    uint32_t m_gen = 0;

    const MemRegion* findRegion(uint64_t addr) {
        if (m_lastReg < m_regs.size()) {
            const MemRegion& r = m_regs[m_lastReg];
            if (addr >= r.base && addr < r.end()) return &r;
        }
        size_t lo = 0, hi = m_regs.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (m_regs[mid].end() <= addr)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo < m_regs.size() && m_regs[lo].base <= addr && addr < m_regs[lo].end()) {
            m_lastReg = lo;
            return &m_regs[lo];
        }
        return nullptr;
    }

    void evaluate(const Candidate& c, uint64_t newRaw) {
        const VType t = candType(c);
        if (relMatch(t, m_rel, c.value, newRaw, m_tg)) m_w.push(makeCand(candAddr(c), t, newRaw));
    }

    // Lee de una vez varios resultados cercanos y los evalúa.
    void processWindow() {
        const Candidate& c0 = m_block[m_bi];
        const uint64_t a0 = candAddr(c0);
        const MemRegion* r = findRegion(a0);
        const uint64_t e0 = a0 + (uint64_t)vtypeSize(candType(c0));
        if (!r || e0 > r->end()) {   // esa memoria ya no existe: se descarta
            m_bi++;
            m_done++;
            return;
        }
        uint64_t wEnd = e0;
        size_t j = m_bi + 1;
        const uint64_t windowMax = S.limits().windowMax;
        while (j < m_block.size()) {
            const uint64_t a = candAddr(m_block[j]);
            const uint64_t e = a + (uint64_t)vtypeSize(candType(m_block[j]));
            if (a < a0 || e > r->end() || a > wEnd + kWindowGap || e - a0 > windowMax) break;
            if (e > wEnd) wEnd = e;
            j++;
        }
        const size_t len = (size_t)(wEnd - a0);
        const bool ok = G.read(a0, m_win.data(), len);
        for (size_t k = m_bi; k < j; k++) {
            const Candidate& c = m_block[k];
            const uint64_t a = candAddr(c);
            const VType t = candType(c);
            if (ok) {
                evaluate(c, loadRaw(m_win.data() + (a - a0), t));
            } else {
                uint8_t tmp[8];
                if (G.read(a, tmp, (size_t)vtypeSize(t))) evaluate(c, loadRaw(tmp, t));
            }
        }
        m_done += j - m_bi;
        m_bi = j;
    }

    void fail(const std::string& e) {
        m_w.discard();
        m_error = e;
        m_status = Status::Failed;
    }
    void finish() {
        if (!m_w.close()) {
            fail(kWriteError);
            return;
        }
        StageInfo next;
        next.stage = Stage::Candidates;
        next.count = m_w.count();
        next.gen = m_gen;
        next.steps = S.state().cur.steps + 1;
        next.lastInput = m_text;
        const SessionState& st = S.state();
        S.commit(next, false, st.types, st.hidden, st.range, st.zone);
        m_found = next.count;
        m_progress = 1.0f;
        m_status = Status::Done;
    }
};

// Tarea que falla de inmediato (para errores al empezar).
class FailedTask : public Task {
public:
    explicit FailedTask(const std::string& e) {
        m_error = e;
        m_status = Status::Failed;
    }
    void step(uint32_t) override {}
};

std::string kv(const std::map<std::string, std::string>& m, const char* key) {
    auto it = m.find(key);
    return it == m.end() ? std::string() : it->second;
}
uint64_t kvNum(const std::map<std::string, std::string>& m, const char* key, int base = 10) {
    std::string v = kv(m, key);
    return v.empty() ? 0 : strtoull(v.c_str(), nullptr, base);
}

}  // namespace

const char* relationLabel(Relation r) {
    switch (r) {
        case Relation::Equal: return "Es igual a";
        case Relation::Increased: return "Aumentó";
        case Relation::Decreased: return "Disminuyó";
        case Relation::Changed: return "Cambió";
        case Relation::Unchanged: return "No cambió";
        case Relation::Between: return "Entre dos números";
    }
    return "?";
}

uint64_t SearchSession::maxCandidates() { return kMaxCandidates; }
void SearchSession::setMaxCandidates(uint64_t n) { kMaxCandidates = n; }

SearchSession::SearchSession(const std::string& dir, bool lowMemory)
    : m_dir(dir), m_lim(lowMemory ? kLowLimits : kNormalLimits) {}

std::string SearchSession::candPath(uint32_t gen) const {
    char b[48];
    snprintf(b, sizeof(b), "cand_%u.bin", gen);
    return joinPath(m_dir, b);
}

std::string SearchSession::snapPath(uint32_t gen, uint32_t idx) const {
    char b[48];
    snprintf(b, sizeof(b), "snap_%u_%u.bin", gen, idx);
    return joinPath(m_dir, b);
}

std::string SearchSession::snapIndexPath(uint32_t gen) const {
    char b[48];
    snprintf(b, sizeof(b), "snap_%u.idx", gen);
    return joinPath(m_dir, b);
}

uint32_t SearchSession::allocGen() {
    uint32_t g = m_st.nextGen++;
    if (m_st.nextGen == 0) m_st.nextGen = 1;
    save();
    return g;
}

bool SearchSession::open(const ProcessInfo& pi) {
    makeDirs(m_dir);
    const bool loaded = load();
    const bool same = loaded && m_st.titleId == pi.titleId && m_st.processId == pi.processId &&
                      m_st.mainBase == pi.main.base && m_st.heapBase == pi.heap.base;
    if (!same) {
        deleteAll();
        const uint32_t nextGen = m_st.nextGen ? m_st.nextGen : 1;
        m_st = SessionState();
        m_st.nextGen = nextGen;
        m_st.titleId = pi.titleId;
        m_st.processId = pi.processId;
        m_st.mainBase = pi.main.base;
        m_st.heapBase = pi.heap.base;
        save();
    } else {
        cleanupOrphans();
    }
    return active();
}

bool SearchSession::sync() {
    SessionState disk;
    if (!readState(disk) || disk.rev == m_st.rev) return false;
    // Si es de otro juego (o de otra vez que se abrió), no es la nuestra: se ignora.
    if (disk.titleId != m_st.titleId || disk.processId != m_st.processId || disk.mainBase != m_st.mainBase ||
        disk.heapBase != m_st.heapBase)
        return false;
    m_st = disk;
    return true;
}

void SearchSession::reset() {
    deleteAll();
    m_st.cur = StageInfo();
    m_st.prev = StageInfo();
    save();
}

bool SearchSession::undo() {
    sync();
    if (!canUndo()) return false;
    if (m_st.cur.gen != m_st.prev.gen) deleteStageFiles(m_st.cur);
    m_st.cur = m_st.prev;
    m_st.prev = StageInfo();
    save();
    return true;
}

void SearchSession::commit(const StageInfo& next, bool newSearch, TypeMask types, bool hidden, bool range,
                           SearchZone zone) {
    if (newSearch) {
        if (m_st.cur.stage != Stage::Empty && m_st.cur.gen != next.gen) deleteStageFiles(m_st.cur);
        if (m_st.prev.stage != Stage::Empty && m_st.prev.gen != next.gen) deleteStageFiles(m_st.prev);
        m_st.prev = StageInfo();
    } else {
        if (m_st.prev.stage != Stage::Empty && m_st.prev.gen != m_st.cur.gen && m_st.prev.gen != next.gen)
            deleteStageFiles(m_st.prev);
        m_st.prev = m_st.cur;
    }
    m_st.types = types;
    m_st.hidden = hidden;
    m_st.range = range;
    m_st.zone = zone;
    m_st.cur = next;
    save();
}

std::unique_ptr<Task> SearchSession::startKnown(IGame& g, TypeMask types, const Number& value, const std::string& text,
                                                SearchZone zone) {
    if (!value.valid) return std::unique_ptr<Task>(new FailedTask("El número no es válido."));
    sync();
    return std::unique_ptr<Task>(new KnownScanTask(*this, g, types, makeTarget(value, types), text, zone));
}

std::unique_ptr<Task> SearchSession::startRange(IGame& g, TypeMask types, const Number& a, const Number& b,
                                                const std::string& text, SearchZone zone) {
    if (!a.valid || !b.valid) return std::unique_ptr<Task>(new FailedTask("El número no es válido."));
    sync();
    return std::unique_ptr<Task>(new KnownScanTask(*this, g, types, makeRangeTarget(a, b, types), text, zone));
}

std::unique_ptr<Task> SearchSession::startHidden(IGame& g, TypeMask types, SearchZone zone) {
    sync();
    return std::unique_ptr<Task>(new SnapshotTask(*this, g, types, zone));
}

std::unique_ptr<Task> SearchSession::refine(IGame& g, Relation rel, const Number* value, const std::string& text,
                                            const Number* value2) {
    sync();   // por si se siguió la búsqueda desde el overlay (o desde la app)
    Target tg;
    if (rel == Relation::Equal) {
        if (!value || !value->valid) return std::unique_ptr<Task>(new FailedTask("El número no es válido."));
        tg = makeTarget(*value, m_st.types);
    } else if (rel == Relation::Between) {
        if (!value || !value->valid || !value2 || !value2->valid)
            return std::unique_ptr<Task>(new FailedTask("El número no es válido."));
        tg = makeRangeTarget(*value, *value2, m_st.types);
    }
    switch (m_st.cur.stage) {
        case Stage::Snapshot:
            return std::unique_ptr<Task>(new CompareSnapshotTask(*this, g, rel, tg, text));
        case Stage::Candidates:
            return std::unique_ptr<Task>(new RefineTask(*this, g, rel, tg, text));
        case Stage::Empty: break;
    }
    return std::unique_ptr<Task>(new FailedTask("No hay ninguna búsqueda en curso."));
}

bool SearchSession::loadCandidates(std::vector<Candidate>& out, size_t max) {
    out.clear();
    sync();
    if (m_st.cur.stage != Stage::Candidates) return false;
    CandReader r;
    if (!r.open(candPath(m_st.cur.gen))) return false;
    r.next(out, max);
    return true;
}

bool SearchSession::removeCandidates(const std::vector<Candidate>& remove) {
    sync();
    if (m_st.cur.stage != Stage::Candidates || remove.empty()) return false;
    std::vector<uint64_t> keys;
    for (const Candidate& c : remove) keys.push_back(c.addrType);
    std::sort(keys.begin(), keys.end());
    CandReader r;
    if (!r.open(candPath(m_st.cur.gen))) return false;
    const uint32_t gen = allocGen();
    CandWriter w;
    if (!w.open(candPath(gen), m_lim.writerBuf)) return false;
    std::vector<Candidate> block;
    while (r.next(block, m_lim.readerBlock) > 0)
        for (const Candidate& c : block)
            if (!std::binary_search(keys.begin(), keys.end(), c.addrType)) w.push(c);
    if (!w.close()) {
        w.discard();
        return false;
    }
    if (m_st.prev.stage == Stage::Empty || m_st.prev.gen != m_st.cur.gen) deleteStageFiles(m_st.cur);
    m_st.cur.gen = gen;
    m_st.cur.count = w.count();
    save();
    return true;
}

bool SearchSession::load() { return readState(m_st); }

bool SearchSession::readState(SessionState& out) const {
    std::string text;
    if (!readFile(joinPath(m_dir, "sesion.txt"), text)) return false;
    std::map<std::string, std::string> m;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos) m[line.substr(0, eq)] = line.substr(eq + 1);
    }
    if (kv(m, "vnx_session") != "1") return false;
    SessionState st;
    st.titleId = kvNum(m, "title", 16);
    st.processId = kvNum(m, "pid");
    st.mainBase = kvNum(m, "main", 16);
    st.heapBase = kvNum(m, "heap", 16);
    st.types = (TypeMask)kvNum(m, "types");
    st.hidden = kvNum(m, "hidden") != 0;
    st.range = kvNum(m, "range") != 0;
    st.zone = (SearchZone)kvNum(m, "zone");
    st.nextGen = (uint32_t)kvNum(m, "next_gen");
    if (st.nextGen == 0) st.nextGen = 1;
    st.rev = kvNum(m, "rev");
    auto readStage = [&](StageInfo& s, const std::string& p) {
        s.stage = (Stage)kvNum(m, (p + "_stage").c_str());
        s.count = kvNum(m, (p + "_count").c_str());
        s.gen = (uint32_t)kvNum(m, (p + "_gen").c_str());
        s.steps = (uint32_t)kvNum(m, (p + "_steps").c_str());
        s.lastInput = kv(m, (p + "_input").c_str());
        if (s.stage != Stage::Snapshot && s.stage != Stage::Candidates) s = StageInfo();
    };
    readStage(st.cur, "cur");
    readStage(st.prev, "prev");
    if (st.cur.stage == Stage::Empty) st.prev = StageInfo();
    out = st;
    return true;
}

void SearchSession::save() {
    // Número de versión distinto en cada guardado (la hora), para que el otro programa
    // (app u overlay) note que la búsqueda cambió.
    m_st.rev = std::max<uint64_t>(m_st.rev + 1, nowMs());
    char b[512];
    std::string out = "vnx_session=1\n";
    snprintf(b, sizeof(b),
             "title=%016llX\npid=%llu\nmain=%llX\nheap=%llX\ntypes=%u\nhidden=%d\nrange=%d\nzone=%d\nnext_gen=%u\n"
             "rev=%llu\n",
             (unsigned long long)m_st.titleId, (unsigned long long)m_st.processId,
             (unsigned long long)m_st.mainBase, (unsigned long long)m_st.heapBase, (unsigned)m_st.types,
             m_st.hidden ? 1 : 0, m_st.range ? 1 : 0, (int)m_st.zone, m_st.nextGen, (unsigned long long)m_st.rev);
    out += b;
    auto writeStage = [&](const StageInfo& s, const char* p) {
        std::string input = s.lastInput;
        for (char& c : input)
            if (c == '\n' || c == '\r') c = ' ';
        snprintf(b, sizeof(b), "%s_stage=%d\n%s_count=%llu\n%s_gen=%u\n%s_steps=%u\n", p, (int)s.stage, p,
                 (unsigned long long)s.count, p, s.gen, p, s.steps);
        out += b;
        out += std::string(p) + "_input=" + input + "\n";
    };
    writeStage(m_st.cur, "cur");
    writeStage(m_st.prev, "prev");
    writeFile(joinPath(m_dir, "sesion.txt"), out);
}

void SearchSession::deleteStageFiles(const StageInfo& s) {
    if (s.stage == Stage::Candidates) {
        removeFile(candPath(s.gen));
    } else if (s.stage == Stage::Snapshot) {
        removeFile(snapIndexPath(s.gen));
        for (uint32_t i = 0; i < 64; i++)
            if (!removeFile(snapPath(s.gen, i)) && i > 8) break;
    }
}

void SearchSession::deleteAll() {
    for (const std::string& name : listDir(m_dir))
        if (name.compare(0, 5, "cand_") == 0 || name.compare(0, 5, "snap_") == 0) removeFile(joinPath(m_dir, name));
}

void SearchSession::cleanupOrphans() {
    auto keep = [&](Stage stage, uint32_t gen) {
        return (m_st.cur.stage == stage && m_st.cur.gen == gen) || (m_st.prev.stage == stage && m_st.prev.gen == gen);
    };
    for (const std::string& name : listDir(m_dir)) {
        unsigned gen = 0;
        bool remove = false;
        if (sscanf(name.c_str(), "cand_%u.bin", &gen) == 1)
            remove = !keep(Stage::Candidates, gen);
        else if (sscanf(name.c_str(), "snap_%u", &gen) == 1)
            remove = !keep(Stage::Snapshot, gen);
        if (remove) removeFile(joinPath(m_dir, name));
    }
}

}  // namespace vnx
