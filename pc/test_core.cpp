// Pruebas del núcleo de ValorNX con un juego simulado (se ejecutan en PC).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "../source/core/cheats.hpp"
#include "../source/core/control.hpp"
#include "../source/core/favorites.hpp"
#include "../source/core/fsutil.hpp"
#include "../source/core/multiplier.hpp"
#include "../source/core/numentry.hpp"
#include "../source/core/search.hpp"
#include "../source/core/value.hpp"
#include "mock_game.hpp"

using namespace vnx;

static int g_fail = 0, g_ok = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (cond) {                                                    \
            g_ok++;                                                    \
        } else {                                                       \
            g_fail++;                                                  \
            printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                              \
    } while (0)

static Task::Status run(std::unique_ptr<Task> t, uint64_t* found = nullptr, std::string* err = nullptr) {
    while (!t->finished()) t->step(1000);
    if (found) *found = t->found();
    if (err) *err = t->error();
    if (t->status() == Task::Status::Failed) printf("  (tarea falló: %s)\n", t->error().c_str());
    return t->status();
}

static bool containsAddr(SearchSession& s, uint64_t addr, VType t) {
    std::vector<Candidate> all;
    s.loadCandidates(all, 100000000);
    for (const Candidate& c : all)
        if (candAddr(c) == addr && candType(c) == t) return true;
    return false;
}

static std::string freshDir(const char* name) {
    std::string d = std::string("/tmp/vnx_test/") + name;
    makeDirs(d);
    for (const std::string& f : listDir(d)) removeFile(joinPath(d, f));
    return d;
}

static void testValues() {
    printf("[valores]\n");
    Number n = parseNumber("1500");
    CHECK(n.valid && !n.isDecimal && n.i == 1500);
    n = parseNumber("-25");
    CHECK(n.valid && n.i == -25);
    n = parseNumber("87.5");
    CHECK(n.valid && n.isDecimal && n.decimals == 1 && std::fabs(n.d - 87.5) < 1e-9);
    CHECK(!parseNumber("").valid);
    CHECK(!parseNumber("-").valid);
    CHECK(!parseNumber("1.2.3").valid);
    CHECK(!parseNumber("12a").valid);
    CHECK(!parseNumber("99999999999999999999").valid);
    uint64_t raw = 0;
    CHECK(encodeNumber(VType::S32, parseNumber("-1"), raw) && raw == 0xFFFFFFFFull);
    CHECK(encodeNumber(VType::U8, parseNumber("255"), raw) && raw == 255);
    CHECK(!encodeNumber(VType::U8, parseNumber("256"), raw));
    CHECK(!encodeNumber(VType::S32, parseNumber("1.5"), raw));
    CHECK(encodeNumber(VType::F32, parseNumber("1.5"), raw) && raw == 0x3FC00000ull);
    CHECK(encodeNumber(VType::S32, parseNumber("4294967295"), raw) && raw == 0xFFFFFFFFull);
    CHECK(formatRaw(VType::S32, 0xFFFFFFFFull) == "-1");
    CHECK(formatRaw(VType::F32, 0x3FC00000ull) == "1.5");
    CHECK(formatRaw(VType::F32, 0x42AF0000ull) == "87.5");
    CHECK(formatRaw(VType::U16, 65535) == "65535");
    CHECK(formatCount(1234567) == "1.234.567");
    CHECK(formatCount(999) == "999");
    CHECK(formatBytes(1536ull << 20) == "1,5 GB");
    Target tg = makeTarget(parseNumber("100"), kMaskAuto);
    CHECK(tg.ok[(int)VType::S32] && tg.raw[(int)VType::S32] == 100);
    CHECK(tg.lo < 99.5 && tg.hi > 100.5);
}

static void testKnownSearch() {
    printf("[búsqueda con valor conocido]\n");
    MockGame g;
    SearchSession s(freshDir("known"));
    CHECK(!s.open(g.info()));
    uint64_t found = 0;
    CHECK(run(s.startKnown(g, kMaskAuto, parseNumber("1500"), "1500", SearchZone::All), &found) == Task::Status::Done);
    printf("  1500 -> %llu resultados\n", (unsigned long long)found);
    CHECK(found > 2);
    CHECK(s.state().cur.count == found);
    CHECK(containsAddr(s, g.moneyAddr, VType::S32));
    CHECK(containsAddr(s, g.moneyCopyAddr, VType::S32));
    // Gasta dinero y refina.
    g.spendMoney(275);
    g.noise(5000);
    const Number n1225 = parseNumber("1225");
    CHECK(run(s.refine(g, Relation::Equal, &n1225, "1225"), &found) == Task::Status::Done);
    printf("  1225 -> %llu resultados\n", (unsigned long long)found);
    CHECK(containsAddr(s, g.moneyAddr, VType::S32));
    CHECK(found >= 2 && found < 20);
    CHECK(s.state().cur.steps == 2);
    CHECK(s.canUndo());
    // "Disminuyó" tras gastar más.
    g.spendMoney(25);
    CHECK(run(s.refine(g, Relation::Decreased, nullptr, ""), &found) == Task::Status::Done);
    printf("  disminuyó -> %llu resultados\n", (unsigned long long)found);
    CHECK(containsAddr(s, g.moneyAddr, VType::S32));
    std::vector<Candidate> list;
    CHECK(s.loadCandidates(list, 100));
    bool valueOk = false;
    for (const Candidate& c : list)
        if (candAddr(c) == g.moneyAddr) valueOk = c.value == 1200;
    CHECK(valueOk);
    // Deshacer vuelve al paso anterior.
    const uint64_t before = s.state().prev.count;
    CHECK(s.undo());
    CHECK(s.state().cur.count == before);
    CHECK(!s.canUndo());
    // Quitar un resultado de la lista.
    s.loadCandidates(list, 100);
    std::vector<Candidate> rm;
    for (const Candidate& c : list)
        if (candAddr(c) == g.moneyCopyAddr) rm.push_back(c);
    CHECK(rm.size() == 1);
    CHECK(s.removeCandidates(rm));
    CHECK(s.state().cur.count == before - 1);
    CHECK(!containsAddr(s, g.moneyCopyAddr, VType::S32));
    CHECK(containsAddr(s, g.moneyAddr, VType::S32));

    // La sesión se recupera desde la microSD con el mismo proceso...
    SearchSession s2(s.dir());
    CHECK(s2.open(g.info()));
    CHECK(s2.state().cur.count == before - 1);
    CHECK(containsAddr(s2, g.moneyAddr, VType::S32));
    // ...pero se descarta si el juego se reinició (otro proceso).
    g.setPid(0x99);
    SearchSession s3(s.dir());
    CHECK(!s3.open(g.info()));
    CHECK(listDir(s.dir()).size() == 1);   // solo queda sesion.txt
}

static void testRegionsAndBadPages() {
    printf("[zonas de memoria y páginas ilegibles]\n");
    MockGame g;
    SearchSession s(freshDir("regions"));
    s.open(g.info());
    uint64_t found = 0;
    CHECK(run(s.startKnown(g, typeBit(VType::S32), parseNumber("424242"), "424242", SearchZone::All), &found) ==
          Task::Status::Done);
    CHECK(!containsAddr(s, g.ioValueAddr, VType::S32));        // memoria de E/S: nunca se lee
    CHECK(!containsAddr(s, g.badPageValueAddr, VType::S32));   // página ilegible
    CHECK(containsAddr(s, g.goodPageValueAddr, VType::S32));   // resto de esa zona sí
    // Solo main: encuentra la experiencia y no el dinero.
    CHECK(run(s.startKnown(g, typeBit(VType::S32), parseNumber("12345"), "12345", SearchZone::Main), &found) ==
          Task::Status::Done);
    CHECK(containsAddr(s, g.expAddr, VType::S32));
    std::vector<Candidate> list;
    s.loadCandidates(list, 1000000);
    bool allMain = true;
    for (const Candidate& c : list) allMain = allMain && g.info().main.contains(candAddr(c));
    CHECK(allMain);
    // Tipos pequeños.
    CHECK(run(s.startKnown(g, typeBit(VType::U16), parseNumber("12"), "12", SearchZone::Heap), &found) ==
          Task::Status::Done);
    CHECK(containsAddr(s, g.levelAddr, VType::U16));
    CHECK(run(s.startKnown(g, typeBit(VType::U8), parseNumber("5"), "5", SearchZone::Heap), &found) ==
          Task::Status::Done);
    CHECK(containsAddr(s, g.itemsAddr, VType::U8));
}

static void testHiddenSearch() {
    printf("[búsqueda de valor oculto]\n");
    MockGame g;
    SearchSession s(freshDir("hidden"));
    s.open(g.info());
    uint64_t found = 0;
    CHECK(run(s.startHidden(g, kMaskAuto, SearchZone::All), &found) == Task::Status::Done);
    CHECK(s.state().cur.stage == Stage::Snapshot);
    CHECK(s.state().hidden);
    g.takeDamage(10.0f);
    g.noise(20000);
    CHECK(run(s.refine(g, Relation::Decreased, nullptr, ""), &found) == Task::Status::Done);
    printf("  disminuyó -> %llu resultados\n", (unsigned long long)found);
    CHECK(s.state().cur.stage == Stage::Candidates);
    CHECK(containsAddr(s, g.hpAddr, VType::F32));
    CHECK(s.canUndo() && s.state().prev.stage == Stage::Snapshot);
    g.noise(20000);
    CHECK(run(s.refine(g, Relation::Unchanged, nullptr, ""), &found) == Task::Status::Done);
    printf("  no cambió -> %llu resultados\n", (unsigned long long)found);
    CHECK(containsAddr(s, g.hpAddr, VType::F32));
    // Los archivos de la foto se borran cuando ya no se pueden deshacer.
    bool snapLeft = false;
    for (const std::string& f : listDir(s.dir())) snapLeft = snapLeft || f.compare(0, 5, "snap_") == 0;
    CHECK(!snapLeft);
    g.takeDamage(2.5f);
    CHECK(run(s.refine(g, Relation::Decreased, nullptr, ""), &found) == Task::Status::Done);
    printf("  disminuyó -> %llu resultados\n", (unsigned long long)found);
    CHECK(containsAddr(s, g.hpAddr, VType::F32));
    const Number n75 = parseNumber("75");
    CHECK(run(s.refine(g, Relation::Equal, &n75, "75"), &found) == Task::Status::Done);
    printf("  = 75 (vida 75.5) -> %llu resultados\n", (unsigned long long)found);
    CHECK(containsAddr(s, g.hpAddr, VType::F32));
    CHECK(found < 50);
}

static void testTooManyAndCancel() {
    printf("[demasiados resultados y cancelar]\n");
    MockGame g(8ull << 20);
    SearchSession s(freshDir("toomany"));
    s.open(g.info());
    std::string err;
    uint64_t found = 0;
    CHECK(run(s.startKnown(g, typeBit(VType::U8), parseNumber("0"), "0", SearchZone::All), &found, &err) ==
          Task::Status::Done);
    printf("  0 (byte) -> %llu resultados\n", (unsigned long long)found);
    // Con un límite bajo, un valor tan común se rechaza sin romper la búsqueda anterior.
    const uint64_t zeros = s.state().cur.count;
    SearchSession::setMaxCandidates(1000);
    CHECK(run(s.startKnown(g, typeBit(VType::U8), parseNumber("0"), "0", SearchZone::All), &found, &err) ==
          Task::Status::Failed);
    CHECK(err.find("demasiados") != std::string::npos);
    CHECK(s.state().cur.count == zeros);
    SearchSession::setMaxCandidates(40000000ull);
    // Cancelar una búsqueda no estropea la anterior.
    const uint64_t prevCount = s.state().cur.count;
    auto t = s.startKnown(g, kMaskAuto, parseNumber("7"), "7", SearchZone::All);
    t->step(0);
    t->cancel();
    t->step(10);
    CHECK(t->status() == Task::Status::Cancelled);
    CHECK(s.state().cur.count == prevCount);
    // Número no válido.
    Number bad;
    CHECK(run(s.startKnown(g, kMaskAuto, bad, "", SearchZone::All)) == Task::Status::Failed);
}

static void testCheats() {
    printf("[trucos de Atmosphère]\n");
    std::vector<uint32_t> ops = cheatStoreStatic(RegionKind::Heap, 0x123450, VType::S32, 99999);
    CHECK(ops.size() == 3 && ops[0] == 0x04100000u && ops[1] == 0x00123450u && ops[2] == 0x0001869Fu);
    ops = cheatStoreStatic(RegionKind::Main, 0x12AABBCCDDull, VType::S64, 0x1122334455667788ull);
    CHECK(ops.size() == 4 && ops[0] == 0x08000012u && ops[1] == 0xAABBCCDDu && ops[2] == 0x11223344u &&
          ops[3] == 0x55667788u);
    ops = cheatStoreStatic(RegionKind::Heap, 0x10, VType::U8, 0x1FF);
    CHECK(ops.size() == 3 && ops[0] == 0x01100000u && ops[2] == 0xFFu);
    std::string block = cheatBlock("Dinero = 99999", cheatStoreStatic(RegionKind::Heap, 0x123450, VType::S32, 99999));
    CHECK(block == "[Dinero = 99999]\n04100000 00123450 0001869F\n");
    CHECK(cheatSafeName("a[b]c{d}") == "abcd");
    std::string merged = mergeCheatFile("", "X", "[X]\n11111111\n");
    CHECK(merged == "[X]\n11111111\n");
    std::string existing = "[Otro]\n04000000 00000010 00000001\n\n[X]\n22222222\n\n[Final]\n33333333\n";
    merged = mergeCheatFile(existing, "X", "[X]\n44444444\n");
    CHECK(merged == "[Otro]\n04000000 00000010 00000001\n\n[X]\n44444444\n\n[Final]\n33333333\n");
    merged = mergeCheatFile("[Otro]\n11111111\n\n\n", "Nuevo", "[Nuevo]\n55555555\n");
    CHECK(merged == "[Otro]\n11111111\n\n[Nuevo]\n55555555\n");
    MockGame g;
    std::string root = freshDir("sd");
    std::string path;
    CHECK(writeCheat(root, g.info(), "Vida", {0x04100000u, 0x2340u, 0x42C80000u}, path));
    CHECK(writeCheat(root, g.info(), "Dinero", {0x04100000u, 0x123450u, 0x1869Fu}, path));
    CHECK(writeCheat(root, g.info(), "Vida", {0x04100000u, 0x2340u, 0x43480000u}, path));
    std::string content;
    CHECK(readFile(path, content));
    CHECK(content == "[Vida]\n04100000 00002340 43480000\n\n[Dinero]\n04100000 00123450 0001869F\n");
    CHECK(path.find("atmosphere/contents/0100ABCD12340000/cheats/A0A1A2A3A4A5A6A7.txt") != std::string::npos);

    const std::string file = "[A]\n11111111\n\n[X]\n22222222\n\n[B]\n33333333\n";
    CHECK(removeCheatBlock(file, "X") == "[A]\n11111111\n\n[B]\n33333333\n");
    CHECK(removeCheatBlock(file, "Z") == file);
    CHECK(removeCheatBlock("[X]\n1\n", "X").empty());
    CHECK(removeCheatBlock("[A]\n1\n\n[X]\n2\n", "X") == "[A]\n1\n");
    CHECK(removeCheatFromFile(root, g.info(), "Vida"));
    CHECK(readFile(path, content) && content == "[Dinero]\n04100000 00123450 0001869F\n");
    CHECK(removeCheatFromFile(root, g.info(), "Dinero"));
    CHECK(!fileExists(path));
    CHECK(removeCheatFromFile(root, g.info(), "Nada"));
}

static void testFavorites() {
    printf("[valores guardados]\n");
    std::string dir = freshDir("favs");
    FavoriteStore fs(dir);
    CHECK(!fs.load(0x0100ABCD12340000ull));
    Favorite f;
    f.name = "Dinero";
    f.type = VType::S32;
    f.region = RegionKind::Heap;
    f.offset = 0x123450;
    f.buildId = "A0A1A2A3A4A5A6A7";
    fs.items().push_back(f);
    f.name = fs.uniqueName("Dinero");
    CHECK(f.name == "Dinero 2");
    f.name = "Vida\tcon tab";
    f.type = VType::F32;
    f.hasValue = true;
    f.value = 0x42C80000;
    fs.items().push_back(f);
    CHECK(fs.save());
    FavoriteStore fs2(dir);
    CHECK(fs2.load(0x0100ABCD12340000ull));
    CHECK(fs2.items().size() == 2);
    CHECK(fs2.items()[0].name == "Dinero" && fs2.items()[0].offset == 0x123450 && !fs2.items()[0].hasValue);
    CHECK(fs2.items()[1].name == "Vida con tab" && fs2.items()[1].type == VType::F32 && fs2.items()[1].hasValue &&
          fs2.items()[1].value == 0x42C80000);
    CHECK(fs2.find(RegionKind::Heap, 0x123450, VType::S32) == 0);
    CHECK(fs2.find(RegionKind::Main, 0x123450, VType::S32) == -1);
    MockGame g;
    CHECK(FavoriteStore::address(fs2.items()[0], g.info()) == g.moneyAddr);
    CHECK(g.info().describe(g.moneyAddr) == "heap+0x123450");
    CHECK(g.info().describe(g.expAddr) == "main+0xA00100");
}

static void testMultiplier() {
    printf("[multiplicador de ganancias]\n");
    MockGame g;
    const ProcessInfo& pi = g.info();
    ScratchArea sa;
    std::string err;
    CHECK(findScratch(g, sa, err));
    CHECK(sa.slots == 16);
    CHECK(pi.main.base + sa.slotBase >= g.bssEnd + 32);

    MultiplierSpec s;
    s.region = RegionKind::Main;
    s.offset = g.expAddr - pi.main.base;
    s.type = VType::S32;
    s.factor = 2;
    s.slotOffset = sa.slotBase;
    const std::vector<uint32_t> ops = buildMultiplierOps(s);
    CHECK(ops.size() == 35);
    MultiplierSpec p;
    CHECK(parseMultiplierOps(ops, p));
    CHECK(p.region == s.region && p.offset == s.offset && p.type == s.type && p.factor == 2 &&
          p.slotOffset == s.slotOffset);
    std::vector<uint32_t> tampered = ops;
    tampered[20] ^= 1;
    CHECK(!parseMultiplierOps(tampered, p));
    CHECK(!parseMultiplierOps({0x04000000u, 0x100u, 0x5u}, p));

    // Un truco anterior deja los registros r0..r3 con basura (como pasa en dmnt).
    CHECK(g.addCheat("sucio",
                     {0x98600100u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x98611100u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x98622100u,
                      0xFFFFFFFFu, 0xFFFFFFFFu, 0x98633100u, 0xFFFFFFFFu, 0xFFFFFFFFu},
                     true));
    uint32_t id = 0;
    CHECK(g.addCheat(multiplierCheatName("Experiencia", 2), ops, true, &id));
    g.tick();
    CHECK(g.lastVmOk);
    CHECK(g.getS32(g.expAddr) == 12345);   // primera vez: solo recuerda el valor
    g.gainExp(100);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12545);   // +100 -> +200
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12545);   // no se vuelve a aplicar
    g.gainExp(-45);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12500);   // lo que baja no se toca
    g.gainExp(10);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12520);
    // Si el valor pasa por 0 (pantalla de título), lo que se cargue después no se multiplica.
    g.setS32(g.expAddr, 0);
    g.tick();
    g.setS32(g.expAddr, 5000);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 5000);
    g.gainExp(1);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 5002);
    // Tope: nunca pasa del máximo de un entero de 4 bytes.
    g.setS32(g.expAddr, 2147483000);
    g.tick();
    g.gainExp(600);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 2147483647);
    // En pausa no multiplica.
    CHECK(g.toggleCheat(id));
    g.setS32(g.expAddr, 100);
    g.tick();
    g.gainExp(50);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 150);
    // Al reanudar, la app pone el hueco a 0 para que no cuente lo ganado en pausa.
    CHECK(g.toggleCheat(id));
    const uint64_t zero = 0;
    g.write(pi.main.base + s.slotOffset, &zero, 8);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 150);
    g.gainExp(50);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 250);

    // x10 en un entero de 2 bytes del heap, en otro hueco.
    MultiplierSpec s2;
    s2.region = RegionKind::Heap;
    s2.offset = g.levelAddr - pi.heap.base;
    s2.type = VType::U16;
    s2.factor = 10;
    s2.slotOffset = sa.slotBase + 8;
    CHECK(g.addCheat("x10 Nivel", buildMultiplierOps(s2), true));
    g.tick();
    uint16_t lvl = 0;
    g.read(g.levelAddr, &lvl, 2);
    CHECK(lvl == 12);
    lvl = 13;
    g.write(g.levelAddr, &lvl, 2);
    g.tick();
    g.read(g.levelAddr, &lvl, 2);
    CHECK(lvl == 22);
    lvl = 65000;
    g.write(g.levelAddr, &lvl, 2);
    g.write(pi.main.base + s2.slotOffset, &zero, 8);   // empezar de cero: que no cuente el salto
    g.tick();
    g.read(g.levelAddr, &lvl, 2);
    CHECK(lvl == 65000);
    lvl = 65100;
    g.write(g.levelAddr, &lvl, 2);
    g.tick();
    g.read(g.levelAddr, &lvl, 2);
    CHECK(lvl == 65535);
    CHECK(g.getS32(g.expAddr) == 250);   // el otro multiplicador sigue igual

    // Un valor muy lejos en la zona ASLR (más de 64 GB) usa la otra forma de escribir.
    MultiplierSpec s3;
    s3.region = RegionKind::Aslr;
    s3.offset = g.farAddr - pi.aslr.base;
    s3.type = VType::S32;
    s3.factor = 3;
    s3.slotOffset = sa.slotBase + 16;
    const std::vector<uint32_t> ops3 = buildMultiplierOps(s3);
    CHECK(ops3.size() == 40);
    CHECK(parseMultiplierOps(ops3, p) && p.region == RegionKind::Aslr && p.offset == s3.offset && p.factor == 3);
    CHECK(g.addCheat("x3 Lejos", ops3, true));
    g.tick();
    g.setS32(g.farAddr, 1010);
    g.tick();
    CHECK(g.getS32(g.farAddr) == 1030);

    std::vector<CheatInfo> list;
    CHECK(g.listCheats(list));
    int recognized = 0;
    for (const CheatInfo& c : list)
        if (parseMultiplierOps(c.opcodes, p)) recognized++;
    CHECK(recognized == 3);
    CHECK(!multiplierSupported(VType::F32) && !multiplierSupported(VType::F64));
    CHECK(multiplierSupported(VType::S32) && multiplierSupported(VType::U8));
    CHECK(multiplierCheatName("EXP", 3) == "x3 EXP (ValorNX)");

    // Si el hueco tras la .bss del ejecutable principal está ocupado, se usa el de otro módulo.
    const uint32_t junk = 1;
    g.write(((g.bssEnd + 15) & ~15ull) + 8, &junk, 4);
    CHECK(findScratch(g, sa, err));
    CHECK(sa.region == RegionKind::Aslr && pi.aslr.base + sa.slotBase >= g.sdkBssEnd + 32 && sa.slots == 16);
    MultiplierSpec s4 = s;
    s4.factor = 4;
    s4.slotRegion = RegionKind::Aslr;
    s4.slotOffset = sa.slotBase;
    const std::vector<uint32_t> ops4 = buildMultiplierOps(s4);
    CHECK(parseMultiplierOps(ops4, p) && p.slotRegion == RegionKind::Aslr && p.slotOffset == sa.slotBase &&
          p.factor == 4);
    CHECK(g.removeCheat(id));   // quitar el x2 de la experiencia para probar el x4
    CHECK(g.addCheat("x4 Experiencia", ops4, true));
    g.setS32(g.expAddr, 1000);
    g.tick();
    g.gainExp(10);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 1040);
    // Sin sitio en ningún módulo: no se puede.
    g.write(((g.sdkBssEnd + 15) & ~15ull) + 8, &junk, 4);
    CHECK(!findScratch(g, sa, err));
    CHECK(!err.empty());
}

static void testRangeTargets() {
    printf("[rangos: objetivo]\n");
    Target tg = makeRangeTarget(parseNumber("100"), parseNumber("20"), kMaskAuto);   // da igual el orden
    CHECK(tg.range && tg.imin == 20 && tg.imax == 100);
    CHECK(tg.ok[(int)VType::S32] && tg.ok[(int)VType::F32]);
    CHECK(targetMatch(VType::S32, 20, tg) && targetMatch(VType::S32, 100, tg) && targetMatch(VType::S32, 55, tg));
    CHECK(!targetMatch(VType::S32, 19, tg) && !targetMatch(VType::S32, 101, tg));
    CHECK(!targetMatch(VType::S32, 0xFFFFFFFFull, tg));   // -1
    uint64_t raw = 0;
    CHECK(encodeNumber(VType::F32, parseNumber("99.9"), raw) && targetMatch(VType::F32, raw, tg));
    CHECK(encodeNumber(VType::F32, parseNumber("19.5"), raw) && targetMatch(VType::F32, raw, tg));   // redondeo
    CHECK(encodeNumber(VType::F32, parseNumber("18.5"), raw) && !targetMatch(VType::F32, raw, tg));
    CHECK(!targetMatch(VType::F32, 0x7FC00000ull, tg));   // NaN
    // Negativos en enteros con signo.
    tg = makeRangeTarget(parseNumber("-10"), parseNumber("-5"), typeBit(VType::S32));
    CHECK(targetMatch(VType::S32, 0xFFFFFFF9ull, tg) && !targetMatch(VType::S32, 7, tg));
    // Tipos sin signo: se recorta a lo que cabe; si no cabe nada, ese tipo no se busca.
    tg = makeRangeTarget(parseNumber("-5"), parseNumber("3"), typeBit(VType::U8));
    CHECK(tg.ok[(int)VType::U8] && targetMatch(VType::U8, 3, tg) && targetMatch(VType::U8, 0, tg) &&
          !targetMatch(VType::U8, 251, tg));
    tg = makeRangeTarget(parseNumber("300"), parseNumber("400"), typeBit(VType::U8));
    CHECK(!tg.ok[(int)VType::U8]);
    // Decimales en un rango de enteros: se redondea hacia dentro.
    tg = makeRangeTarget(parseNumber("20.5"), parseNumber("30.5"), typeBit(VType::S32));
    CHECK(tg.imin == 21 && tg.imax == 30);
    tg = makeRangeTarget(parseNumber("20.2"), parseNumber("20.8"), typeBit(VType::S32));
    CHECK(!tg.ok[(int)VType::S32]);
    CHECK(rawAsInt(VType::S32, 0xFFFFFFFEull) == -2 && rawAsInt(VType::U16, 0xFFFF) == 65535);
}

static void testRangeSearch() {
    printf("[búsqueda entre dos números]\n");
    MockGame g;
    SearchSession s(freshDir("range"));
    s.open(g.info());
    uint64_t found = 0, exact = 0;
    CHECK(run(s.startKnown(g, kMaskAuto, parseNumber("1500"), "1500", SearchZone::All), &exact) ==
          Task::Status::Done);
    CHECK(run(s.startRange(g, kMaskAuto, parseNumber("1600"), parseNumber("1400"), "entre 1400 y 1600",
                           SearchZone::All),
              &found) == Task::Status::Done);
    printf("  entre 1400 y 1600 -> %llu resultados (exacto 1500: %llu)\n", (unsigned long long)found,
           (unsigned long long)exact);
    CHECK(found > exact);
    CHECK(s.state().range && !s.state().hidden && s.state().cur.lastInput == "entre 1400 y 1600");
    CHECK(containsAddr(s, g.moneyAddr, VType::S32) && containsAddr(s, g.moneyCopyAddr, VType::S32));
    // Gasta: "disminuyó" y otro rango más pequeño.
    g.spendMoney(300);
    g.noise(5000);
    CHECK(run(s.refine(g, Relation::Decreased, nullptr, "Disminuyó"), &found) == Task::Status::Done);
    CHECK(containsAddr(s, g.moneyAddr, VType::S32));
    const Number lo = parseNumber("1150"), hi = parseNumber("1250");
    CHECK(run(s.refine(g, Relation::Between, &lo, "entre 1150 y 1250", &hi), &found) == Task::Status::Done);
    printf("  disminuyó + entre 1150 y 1250 -> %llu resultados\n", (unsigned long long)found);
    CHECK(containsAddr(s, g.moneyAddr, VType::S32));
    CHECK(found < 20 && s.state().range && s.state().cur.steps == 3);
    // Between sin el segundo número no es válido.
    CHECK(run(s.refine(g, Relation::Between, &lo, "x")) == Task::Status::Failed);
    // El modo "rango" se recuerda al volver a abrir la app.
    SearchSession s2(s.dir());
    CHECK(s2.open(g.info()) && s2.state().range);

    // Decimales (vida 87.5) y tipos pequeños.
    CHECK(run(s.startRange(g, typeBit(VType::F32), parseNumber("80"), parseNumber("90"), "80-90", SearchZone::Heap),
              &found) == Task::Status::Done);
    CHECK(containsAddr(s, g.hpAddr, VType::F32));
    g.takeDamage(10.0f);
    const Number lo2 = parseNumber("70"), hi2 = parseNumber("80");
    CHECK(run(s.refine(g, Relation::Between, &lo2, "70-80", &hi2), &found) == Task::Status::Done);
    CHECK(containsAddr(s, g.hpAddr, VType::F32));
    CHECK(run(s.startRange(g, typeBit(VType::U16), parseNumber("10"), parseNumber("15"), "10-15", SearchZone::Heap)) ==
          Task::Status::Done);
    CHECK(containsAddr(s, g.levelAddr, VType::U16));
    CHECK(run(s.startRange(g, typeBit(VType::U8), parseNumber("4"), parseNumber("6"), "4-6", SearchZone::Heap)) ==
          Task::Status::Done);
    CHECK(containsAddr(s, g.itemsAddr, VType::U8));

    // Valor oculto + "está entre dos números" sobre la foto de la memoria.
    CHECK(run(s.startHidden(g, kMaskAuto, SearchZone::Heap)) == Task::Status::Done);
    g.takeDamage(2.5f);   // 75
    const Number lo3 = parseNumber("74"), hi3 = parseNumber("76");
    CHECK(run(s.refine(g, Relation::Between, &lo3, "74-76", &hi3), &found) == Task::Status::Done);
    CHECK(s.state().cur.stage == Stage::Candidates && s.state().hidden && !s.state().range);
    CHECK(containsAddr(s, g.hpAddr, VType::F32));

    // Un rango enorme se rechaza pronto, sin llenar la microSD.
    const uint64_t before = s.state().cur.count;
    SearchSession::setMaxCandidates(1000);
    std::string err;
    CHECK(run(s.startRange(g, typeBit(VType::U8), parseNumber("1"), parseNumber("255"), "1-255", SearchZone::All),
              &found, &err) == Task::Status::Failed);
    CHECK(err.find("rango") != std::string::npos);
    CHECK(s.state().cur.count == before);
    SearchSession::setMaxCandidates(40000000ull);
}

static void testNumberEntry() {
    printf("[teclado numérico]\n");
    NumberEntry e("1500");
    CHECK(e.fresh() && e.text() == "1500");
    CHECK(e.press('7', false).empty() && e.text() == "7" && !e.fresh());
    e.press('<', false);
    CHECK(e.empty());
    e.press('-', false);
    e.press('5', false);
    CHECK(e.text() == "-5");
    e.press('-', false);
    CHECK(e.text() == "5");
    CHECK(!e.press('.', false).empty() && e.text() == "5");   // enteros: sin decimales
    e.press('C', false);
    CHECK(e.press('.', true).empty() && e.text() == "0.");
    e.press('5', true);
    e.press('.', true);   // un solo punto
    CHECK(e.text() == "0.5");
    NumberEntry z("0");
    z.press('<', false);
    z.press('0', false);
    z.press('7', false);
    CHECK(z.text() == "7");
    NumberEntry f("250");
    f.press('-', false);   // sobre el valor inicial: empieza un número negativo nuevo
    CHECK(f.text() == "-");
    f.set("999");
    CHECK(f.text() == "999" && !f.fresh());
    CHECK(numberErrorForType(VType::S32, parseNumber("99999")).empty());
    CHECK(numberErrorForType(VType::U8, parseNumber("256")).find("255") != std::string::npos);
    CHECK(numberErrorForType(VType::S32, parseNumber("1.5")).find("decimales") != std::string::npos);
    CHECK(numberErrorForType(VType::F32, parseNumber("1.5")).empty());
    CHECK(!numberErrorForType(VType::S32, parseNumber("")).empty());
}

static void testGameControl() {
    printf("[congelar y multiplicar (GameControl)]\n");
    MockGame g;
    const ProcessInfo& pi = g.info();
    const std::string root = freshDir("ctl_sd");
    GameControl ctl;
    ctl.attach(&g, root);
    CHECK(ctl.frozenCount() == 0 && ctl.multipliers().empty());

    // Congelar, cambiar estando congelado y descongelar.
    OpResult r = ctl.toggleFreeze(g.moneyAddr, VType::S32);
    CHECK(r.ok && ctl.isFrozen(g.moneyAddr));
    CHECK(ctl.setValue(g.moneyAddr, VType::S32, 7777));
    g.spendMoney(100);
    CHECK(g.getS32(g.moneyAddr) == 7777 && ctl.isFrozen(g.moneyAddr));
    r = ctl.toggleFreeze(g.moneyAddr, VType::S32);
    CHECK(r.ok && !ctl.isFrozen(g.moneyAddr));
    g.spendMoney(100);
    CHECK(g.getS32(g.moneyAddr) == 7677);
    uint64_t raw = 0;
    CHECK(ctl.readValue(g.moneyAddr, VType::S32, raw) && raw == 7677);

    // Multiplicador x3 en la experiencia (zona main).
    Favorite exp;
    exp.name = "EXP";
    exp.type = VType::S32;
    exp.region = pi.classify(g.expAddr, &exp.offset);
    r = ctl.activateMultiplier(exp, 3, false);
    CHECK(r.ok && r.title.empty());
    const ActiveMultiplier* m = ctl.multiplierFor(exp);
    CHECK(m && m->enabled && m->spec.factor == 3 && ctl.activeMultipliers() == 1);
    g.tick();
    g.gainExp(100);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12345 + 300);
    // Pausar y reanudar.
    CHECK(ctl.toggleMultiplier(exp).ok && !ctl.multiplierFor(exp)->enabled && ctl.activeMultipliers() == 0);
    g.gainExp(10);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12645 + 10);
    CHECK(ctl.toggleMultiplier(exp).ok && ctl.multiplierFor(exp)->enabled);
    g.tick();
    g.gainExp(10);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12655 + 30);
    // Cambiarlo a x2 y guardarlo como truco: sustituye al anterior y reutiliza su hueco.
    const uint64_t oldSlot = ctl.multiplierFor(exp)->spec.slotOffset;
    r = ctl.activateMultiplier(exp, 2, true);
    CHECK(r.ok);
    CHECK(ctl.multipliers().size() == 1 && ctl.multiplierFor(exp)->spec.factor == 2 &&
          ctl.multiplierFor(exp)->spec.slotOffset == oldSlot);
    const std::string cheatFile = cheatFilePath(root, pi);
    std::string content;
    CHECK(readFile(cheatFile, content) && content.find("[x2 EXP (ValorNX)]") != std::string::npos);
    g.tick();
    g.gainExp(50);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12685 + 100);
    // Cambiarlo desde el overlay (keepSaved): sigue guardado como truco, ahora como x5.
    r = ctl.activateMultiplier(exp, 5, false, true);
    CHECK(r.ok && ctl.multiplierFor(exp)->spec.factor == 5);
    CHECK(readFile(cheatFile, content) && content.find("[x5 EXP (ValorNX)]") != std::string::npos &&
          content.find("[x2 EXP") == std::string::npos);
    g.tick();
    g.gainExp(10);
    g.tick();
    CHECK(g.getS32(g.expAddr) == 12785 + 50);
    // Quitar: desaparece de Atmosphère y del archivo de trucos.
    CHECK(ctl.removeMultiplier(exp).ok && !ctl.multiplierFor(exp));
    CHECK(!fileExists(cheatFile));
    CHECK(!ctl.removeMultiplier(exp).ok);
    CHECK(!ctl.toggleMultiplier(exp).ok);

    // Decimales: no se puede.
    Favorite hp;
    hp.name = "Vida";
    hp.type = VType::F32;
    hp.region = pi.classify(g.hpAddr, &hp.offset);
    r = ctl.activateMultiplier(hp, 2, false);
    CHECK(!r.ok && !r.title.empty());

    // Solo hay 16 huecos: el 17º multiplicador no cabe.
    int okCount = 0;
    for (int i = 0; i < 17; i++) {
        Favorite f;
        f.name = "V" + std::to_string(i);
        f.type = VType::S32;
        f.region = RegionKind::Heap;
        f.offset = 0x300000 + (uint64_t)i * 16;
        if (ctl.activateMultiplier(f, 2, false).ok) okCount++;
    }
    CHECK(okCount == 16 && ctl.multipliers().size() == 16);
    Favorite extra;
    extra.name = "Extra";
    extra.region = RegionKind::Heap;
    extra.offset = 0x400000;
    r = ctl.activateMultiplier(extra, 2, false);
    CHECK(!r.ok && r.text.find("huecos") != std::string::npos);
    // Sin juego: no hace nada.
    GameControl none;
    none.attach(nullptr, root);
    CHECK(!none.toggleFreeze(g.moneyAddr, VType::S32).ok && !none.readValue(g.moneyAddr, VType::S32, raw));
}

static void testFreeze() {
    printf("[congelar]\n");
    MockGame g;
    CHECK(g.freeze(g.moneyAddr, 4));
    g.spendMoney(500);
    CHECK(g.getS32(g.moneyAddr) == 1500);
    CHECK(g.unfreeze(g.moneyAddr));
    g.spendMoney(500);
    CHECK(g.getS32(g.moneyAddr) == 1000);
}

static std::vector<Candidate> allCands(SearchSession& s) {
    std::vector<Candidate> v;
    s.loadCandidates(v, 100000000);
    return v;
}

static bool sameCands(SearchSession& a, SearchSession& b) {
    const std::vector<Candidate> x = allCands(a), y = allCands(b);
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++)
        if (x[i].addrType != y[i].addrType || x[i].value != y[i].value) return false;
    return !x.empty();
}

// El overlay busca con búferes pequeños: los resultados deben ser exactamente los mismos.
static void testLowMemory() {
    printf("[búsqueda con poca memoria (overlay)]\n");
    MockGame g;
    SearchSession big(freshDir("mem_big")), small(freshDir("mem_small"), true);
    big.open(g.info());
    small.open(g.info());
    CHECK(small.limits().chunk < big.limits().chunk && small.limits().writerBuf < big.limits().writerBuf);
    uint64_t n1 = 0, n2 = 0;
    // Valor conocido (Automático: enteros y decimales ordenados por dirección).
    CHECK(run(big.startKnown(g, kMaskAuto, parseNumber("1500"), "1500", SearchZone::All), &n1) == Task::Status::Done);
    CHECK(run(small.startKnown(g, kMaskAuto, parseNumber("1500"), "1500", SearchZone::All), &n2) == Task::Status::Done);
    CHECK(n1 == n2 && sameCands(big, small));
    g.spendMoney(275);
    g.noise(3000);
    const Number n1225 = parseNumber("1225");
    CHECK(run(big.refine(g, Relation::Equal, &n1225, "1225"), &n1) == Task::Status::Done);
    CHECK(run(small.refine(g, Relation::Equal, &n1225, "1225"), &n2) == Task::Status::Done);
    CHECK(n1 == n2 && sameCands(big, small) && containsAddr(small, g.moneyAddr, VType::S32));
    // Entre dos números con bytes (muchísimos resultados por bloque) y refinar "no cambió".
    CHECK(run(big.startRange(g, typeBit(VType::U8), parseNumber("4"), parseNumber("6"), "4-6", SearchZone::Heap), &n1) ==
          Task::Status::Done);
    CHECK(run(small.startRange(g, typeBit(VType::U8), parseNumber("4"), parseNumber("6"), "4-6", SearchZone::Heap),
              &n2) == Task::Status::Done);
    printf("  bytes entre 4 y 6 -> %llu resultados en los dos\n", (unsigned long long)n2);
    CHECK(n1 == n2 && n2 > 10000 && sameCands(big, small));
    CHECK(run(big.refine(g, Relation::Unchanged, nullptr, "")) == Task::Status::Done);
    CHECK(run(small.refine(g, Relation::Unchanged, nullptr, "")) == Task::Status::Done);
    CHECK(sameCands(big, small) && containsAddr(small, g.itemsAddr, VType::U8));
    // Valor oculto: foto de la memoria y "disminuyó".
    CHECK(run(big.startHidden(g, kMaskAuto, SearchZone::Heap)) == Task::Status::Done);
    CHECK(run(small.startHidden(g, kMaskAuto, SearchZone::Heap)) == Task::Status::Done);
    CHECK(small.state().cur.count == big.state().cur.count);
    g.takeDamage(5.0f);
    CHECK(run(big.refine(g, Relation::Decreased, nullptr, ""), &n1) == Task::Status::Done);
    CHECK(run(small.refine(g, Relation::Decreased, nullptr, ""), &n2) == Task::Status::Done);
    printf("  oculto + disminuyó -> %llu resultados en los dos\n", (unsigned long long)n2);
    CHECK(n1 == n2 && sameCands(big, small) && containsAddr(small, g.hpAddr, VType::F32));
    // Quitar un resultado con los búferes pequeños.
    std::vector<Candidate> list = allCands(small);
    CHECK(small.removeCandidates({list[0]}) && small.state().cur.count == n2 - 1);
}

// La app y el overlay usan la misma búsqueda y los mismos "Mis valores" en la microSD.
static void testSharedSession() {
    printf("[búsqueda compartida entre la app y el overlay]\n");
    MockGame g;
    const std::string dir = freshDir("shared");
    SearchSession app(dir), ovl(dir, true);
    CHECK(!app.open(g.info()));
    CHECK(!app.sync());   // nada nuevo
    // El overlay empieza una búsqueda.
    CHECK(!ovl.open(g.info()));
    uint64_t n = 0;
    CHECK(run(ovl.startKnown(g, kMaskAuto, parseNumber("1500"), "1500", SearchZone::All), &n) == Task::Status::Done);
    // La app se entera al volver.
    CHECK(app.sync() && app.active() && app.state().cur.count == n && app.state().cur.gen == ovl.state().cur.gen);
    CHECK(!app.sync());
    // La app sigue la búsqueda; el overlay continúa desde ahí sin llamar a sync (lo hace solo).
    g.spendMoney(275);
    const Number n1225 = parseNumber("1225");
    CHECK(run(app.refine(g, Relation::Equal, &n1225, "1225"), &n) == Task::Status::Done);
    CHECK(app.state().cur.steps == 2 && n >= 2 && n < 20);
    g.spendMoney(25);
    const Number n1200 = parseNumber("1200");
    CHECK(run(ovl.refine(g, Relation::Equal, &n1200, "1200"), &n) == Task::Status::Done);
    CHECK(ovl.state().cur.steps == 3 && containsAddr(ovl, g.moneyAddr, VType::S32));
    // Los archivos de cada paso no se pisan (números de paso distintos) y deshacer funciona en la app.
    CHECK(app.undo() && app.state().cur.steps == 2);
    CHECK(ovl.sync() && ovl.state().cur.steps == 2);
    // Una búsqueda de otro juego (u otra partida) no se mezcla.
    MockGame other;
    other.setPid(0x77);
    SearchSession third(dir);
    third.open(other.info());   // borra la anterior y empieza otra para el otro proceso
    CHECK(!app.sync() && app.state().cur.steps == 2);

    printf("[mis valores compartidos]\n");
    const std::string fdir = freshDir("shared_favs");
    FavoriteStore a(fdir), b(fdir);
    a.load(g.info().titleId);
    b.load(g.info().titleId);
    CHECK(!b.sync());
    Favorite f;
    f.name = "Dinero";
    f.region = g.info().classify(g.moneyAddr, &f.offset);
    a.items().push_back(f);
    CHECK(a.save());
    CHECK(!a.sync());   // lo que guardó él mismo no cuenta como cambio
    CHECK(b.sync() && b.items().size() == 1 && b.items()[0].name == "Dinero");
    CHECK(!b.sync());
}

int main() {
    testValues();
    testKnownSearch();
    testRegionsAndBadPages();
    testHiddenSearch();
    testTooManyAndCancel();
    testCheats();
    testFavorites();
    testMultiplier();
    testRangeTargets();
    testRangeSearch();
    testNumberEntry();
    testGameControl();
    testFreeze();
    testLowMemory();
    testSharedSession();
    printf("\n%d comprobaciones correctas, %d fallos\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
