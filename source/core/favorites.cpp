#include "favorites.hpp"

#include <cstdio>
#include <cstdlib>

#include "fsutil.hpp"

namespace vnx {

namespace {

std::string clean(const std::string& s) {
    std::string out;
    for (char c : s) out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    return out;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (true) {
        size_t t = line.find('\t', pos);
        if (t == std::string::npos) {
            parts.push_back(line.substr(pos));
            break;
        }
        parts.push_back(line.substr(pos, t - pos));
        pos = t + 1;
    }
    return parts;
}

}  // namespace

FavoriteStore::FavoriteStore(const std::string& dir) : m_dir(dir) {}

bool FavoriteStore::load(uint64_t titleId) {
    m_items.clear();
    m_text.clear();
    makeDirs(m_dir);
    m_path = joinPath(m_dir, formatHex(titleId, 16) + ".txt");
    std::string text;
    if (!readFile(m_path, text)) return false;
    parse(text);
    return true;
}

bool FavoriteStore::sync() {
    if (m_path.empty()) return false;
    std::string text;
    if (!readFile(m_path, text)) text.clear();
    if (text == m_text) return false;
    parse(text);
    return true;
}

void FavoriteStore::parse(const std::string& text) {
    m_items.clear();
    m_text = text;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> p = splitTabs(line);
        if (p.size() < 7) continue;
        Favorite f;
        f.name = p[0];
        unsigned t = (unsigned)strtoul(p[1].c_str(), nullptr, 10);
        unsigned r = (unsigned)strtoul(p[2].c_str(), nullptr, 10);
        if (t >= (unsigned)kVTypeCount || r > 3) continue;
        f.type = (VType)t;
        f.region = (RegionKind)r;
        f.offset = strtoull(p[3].c_str(), nullptr, 16);
        f.buildId = p[4];
        f.hasValue = p[5] == "1";
        f.value = strtoull(p[6].c_str(), nullptr, 16);
        m_items.push_back(f);
    }
}

bool FavoriteStore::save() const {
    if (m_path.empty()) return false;
    makeDirs(m_dir);
    std::string out = "# ValorNX - valores guardados\n# nombre\ttipo\tzona\toffset\tbuild\ttiene_valor\tvalor\n";
    char b[160];
    for (const Favorite& f : m_items) {
        snprintf(b, sizeof(b), "\t%d\t%d\t%llX\t", (int)f.type, (int)f.region, (unsigned long long)f.offset);
        out += clean(f.name) + b + clean(f.buildId);
        snprintf(b, sizeof(b), "\t%d\t%llX\n", f.hasValue ? 1 : 0, (unsigned long long)f.value);
        out += b;
    }
    if (!writeFile(m_path, out)) return false;
    m_text = out;
    return true;
}

std::string FavoriteStore::uniqueName(const std::string& base) const {
    auto exists = [&](const std::string& n) {
        for (const Favorite& f : m_items)
            if (f.name == n) return true;
        return false;
    };
    if (!exists(base)) return base;
    for (int i = 2; i < 1000; i++) {
        std::string n = base + " " + std::to_string(i);
        if (!exists(n)) return n;
    }
    return base;
}

int FavoriteStore::find(RegionKind region, uint64_t offset, VType type) const {
    for (size_t i = 0; i < m_items.size(); i++)
        if (m_items[i].region == region && m_items[i].offset == offset && m_items[i].type == type) return (int)i;
    return -1;
}

}  // namespace vnx
