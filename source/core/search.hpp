// ValorNX - motor de búsqueda de valores en la memoria del juego.
//
// Los resultados se guardan en la microSD, así la búsqueda sigue ahí aunque se
// cierre la app para volver al juego.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "game.hpp"
#include "value.hpp"

namespace vnx {

enum class Relation : uint8_t { Equal = 0, Increased, Decreased, Changed, Unchanged, Between };
const char* relationLabel(Relation r);

// Un resultado: dirección + tipo (byte alto) y el último valor leído.
struct Candidate {
    uint64_t addrType;
    uint64_t value;
};
static_assert(sizeof(Candidate) == 16, "Candidate debe ocupar 16 bytes");

constexpr uint64_t kAddrMask = 0x00FFFFFFFFFFFFFFull;
inline uint64_t candAddr(const Candidate& c) { return c.addrType & kAddrMask; }
inline VType candType(const Candidate& c) { return (VType)(c.addrType >> 56); }
inline Candidate makeCand(uint64_t addr, VType t, uint64_t v) {
    return Candidate{(addr & kAddrMask) | ((uint64_t)t << 56), v};
}

enum class Stage : uint8_t { Empty = 0, Snapshot = 1, Candidates = 2 };

struct StageInfo {
    Stage stage = Stage::Empty;
    uint64_t count = 0;   // nº de resultados, o bytes guardados si es Snapshot
    uint32_t gen = 0;     // número de los archivos de este paso
    uint32_t steps = 0;
    std::string lastInput;
};

struct SessionState {
    StageInfo cur, prev;  // prev sirve para "deshacer" (Empty si no hay)
    TypeMask types = 0;
    bool hidden = false;  // empezó como "valor oculto"
    bool range = false;   // empezó como "entre dos números"
    SearchZone zone = SearchZone::All;
    uint32_t nextGen = 1;
    uint64_t titleId = 0, processId = 0, mainBase = 0, heapBase = 0;
    uint64_t rev = 0;     // cambia cada vez que se guarda (la app y el overlay comparten la búsqueda)
};

// Tamaños de los búferes de la búsqueda. El overlay tiene muy poca memoria (4 MB para todo),
// así que usa bloques pequeños: tarda algo más, pero los resultados son exactamente los mismos.
struct SearchLimits {
    size_t chunk;         // bytes de memoria del juego por lectura
    size_t slice;         // bytes que se comparan de una vez (limita la lista temporal)
    size_t writerBuf;     // resultados por escritura en la microSD
    size_t readerBlock;   // resultados por lectura de la microSD
    size_t windowMax;     // ventana máxima de memoria al refinar
};

// Trabajo largo que se hace poco a poco para que la pantalla no se congele.
class Task {
public:
    enum class Status { Running, Done, Cancelled, Failed };
    virtual ~Task() = default;
    virtual void step(uint32_t budgetMs) = 0;
    Status status() const { return m_status; }
    bool finished() const { return m_status != Status::Running; }
    float progress() const { return m_progress; }
    uint64_t found() const { return m_found; }
    bool foundIsBytes() const { return m_foundIsBytes; }   // found() cuenta bytes en vez de resultados
    const std::string& error() const { return m_error; }
    const std::string& title() const { return m_title; }
    void cancel() { m_cancel = true; }

protected:
    Status m_status = Status::Running;
    float m_progress = 0.0f;
    uint64_t m_found = 0;
    bool m_foundIsBytes = false;
    std::string m_error, m_title;
    bool m_cancel = false;
};

class SearchSession {
public:
    // lowMemory: búferes pequeños (para el overlay).
    explicit SearchSession(const std::string& dir, bool lowMemory = false);

    // Carga la búsqueda guardada si es de este mismo proceso del juego.
    // Devuelve true si hay una búsqueda en curso para continuar.
    bool open(const ProcessInfo& pi);
    // Vuelve a leer la búsqueda si la cambió otro programa (la app o el overlay) mientras
    // tanto. Devuelve true si cambió. Las operaciones de abajo ya lo hacen solas.
    bool sync();
    const SessionState& state() const { return m_st; }
    const SearchLimits& limits() const { return m_lim; }
    bool active() const { return m_st.cur.stage != Stage::Empty; }
    bool canUndo() const { return m_st.prev.stage != Stage::Empty; }
    void reset();
    bool undo();

    std::unique_ptr<Task> startKnown(IGame& g, TypeMask types, const Number& value, const std::string& text,
                                     SearchZone zone);
    // Primera búsqueda "entre dos números" (da igual el orden).
    std::unique_ptr<Task> startRange(IGame& g, TypeMask types, const Number& a, const Number& b,
                                     const std::string& text, SearchZone zone);
    std::unique_ptr<Task> startHidden(IGame& g, TypeMask types, SearchZone zone);
    // Equal usa 'value'; Between usa 'value' y 'value2'; el resto no necesita números.
    std::unique_ptr<Task> refine(IGame& g, Relation rel, const Number* value, const std::string& text,
                                 const Number* value2 = nullptr);

    bool loadCandidates(std::vector<Candidate>& out, size_t max);
    // Reescribe los resultados actuales sin los que se indiquen (por dirección+tipo).
    bool removeCandidates(const std::vector<Candidate>& remove);

    static uint64_t maxCandidates();
    static void setMaxCandidates(uint64_t n);   // solo para pruebas

    // Uso interno de las tareas.
    const std::string& dir() const { return m_dir; }
    std::string candPath(uint32_t gen) const;
    std::string snapPath(uint32_t gen, uint32_t idx) const;
    std::string snapIndexPath(uint32_t gen) const;
    uint32_t allocGen();
    void commit(const StageInfo& next, bool newSearch, TypeMask types, bool hidden, bool range, SearchZone zone);

private:
    std::string m_dir;
    SessionState m_st;
    SearchLimits m_lim;

    bool load();
    bool readState(SessionState& out) const;
    void save();
    void deleteStageFiles(const StageInfo& s);
    void deleteAll();
    void cleanupOrphans();
};

}  // namespace vnx
