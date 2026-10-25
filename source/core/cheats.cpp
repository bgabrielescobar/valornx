#include "cheats.hpp"

#include <cstdio>

#include "fsutil.hpp"

namespace vnx {

std::vector<uint32_t> cheatStoreStatic(RegionKind region, uint64_t offset, VType type, uint64_t raw) {
    // 0TMR00AA AAAAAAAA VVVVVVVV (VVVVVVVV)
    const uint32_t width = (uint32_t)vtypeSize(type);
    // Atmosphère escribe el número en el orden normal: a los tipos BE se les da la vuelta antes.
    if (vtypeSwapped(type)) raw = swapBytes(raw, (int)width);
    std::vector<uint32_t> ops;
    ops.push_back((width << 24) | ((uint32_t)region << 20) | (uint32_t)((offset >> 32) & 0xFF));
    ops.push_back((uint32_t)(offset & 0xFFFFFFFFu));
    if (width == 8) {
        ops.push_back((uint32_t)(raw >> 32));
        ops.push_back((uint32_t)(raw & 0xFFFFFFFFu));
    } else {
        uint64_t mask = width == 4 ? 0xFFFFFFFFull : ((1ull << (width * 8)) - 1);
        ops.push_back((uint32_t)(raw & mask));
    }
    return ops;
}

std::string cheatSafeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '[' || c == ']' || c == '{' || c == '}') continue;
        out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    // Atmosphère guarda como máximo 63 bytes; no cortar a mitad de una letra UTF-8.
    if (out.size() > 63) {
        size_t cut = 63;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) cut--;
        out.resize(cut);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) out = "ValorNX";
    return out;
}

std::string cheatBlock(const std::string& name, const std::vector<uint32_t>& ops) {
    std::string s = "[" + cheatSafeName(name) + "]\n";
    char b[16];
    for (size_t i = 0; i < ops.size(); i++) {
        snprintf(b, sizeof(b), "%08X", ops[i]);
        s += b;
        s += (i + 1 == ops.size()) ? "\n" : " ";
    }
    return s;
}

std::string cheatFilePath(const std::string& sdRoot, const ProcessInfo& pi) {
    return joinPath(sdRoot, "atmosphere/contents/" + pi.titleIdHex() + "/cheats/" + pi.buildIdHex() + ".txt");
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

std::string mergeCheatFile(const std::string& existing, const std::string& name, const std::string& block) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < existing.size()) {
        size_t nl = existing.find('\n', pos);
        if (nl == std::string::npos) nl = existing.size();
        std::string line = existing.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        pos = nl + 1;
    }
    const std::string header = "[" + cheatSafeName(name) + "]";
    size_t start = lines.size();
    for (size_t i = 0; i < lines.size(); i++)
        if (trim(lines[i]) == header) {
            start = i;
            break;
        }

    std::string out;
    if (start < lines.size()) {
        size_t end = start + 1;
        while (end < lines.size()) {
            std::string t = trim(lines[end]);
            if (!t.empty() && (t[0] == '[' || t[0] == '{')) break;
            end++;
        }
        for (size_t i = 0; i < start; i++) out += lines[i] + "\n";
        out += block;
        if (end < lines.size()) out += "\n";
        for (size_t i = end; i < lines.size(); i++) out += lines[i] + "\n";
    } else {
        for (const std::string& l : lines) out += l + "\n";
        while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
        if (!trim(out).empty()) out += "\n";
        out += block;
    }
    return out;
}

bool writeCheat(const std::string& sdRoot, const ProcessInfo& pi, const std::string& name,
                const std::vector<uint32_t>& ops, std::string& pathOut) {
    pathOut = cheatFilePath(sdRoot, pi);
    const std::string dir = joinPath(sdRoot, "atmosphere/contents/" + pi.titleIdHex() + "/cheats");
    if (!makeDirs(dir)) return false;
    std::string existing;
    readFile(pathOut, existing);
    return writeFile(pathOut, mergeCheatFile(existing, name, cheatBlock(name, ops)));
}

std::string removeCheatBlock(const std::string& existing, const std::string& name) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < existing.size()) {
        size_t nl = existing.find('\n', pos);
        if (nl == std::string::npos) nl = existing.size();
        std::string line = existing.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        pos = nl + 1;
    }
    const std::string header = "[" + cheatSafeName(name) + "]";
    size_t start = lines.size();
    for (size_t i = 0; i < lines.size(); i++)
        if (trim(lines[i]) == header) {
            start = i;
            break;
        }
    if (start == lines.size()) return existing;
    size_t end = start + 1;
    while (end < lines.size()) {
        std::string t = trim(lines[end]);
        if (!t.empty() && (t[0] == '[' || t[0] == '{')) break;
        end++;
    }
    lines.erase(lines.begin() + (long)start, lines.begin() + (long)end);
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    if (trim(out).empty() || out == "\n") out.clear();
    return out;
}

bool cheatFileHas(const std::string& sdRoot, const ProcessInfo& pi, const std::string& name) {
    std::string existing;
    if (!readFile(cheatFilePath(sdRoot, pi), existing)) return false;
    return removeCheatBlock(existing, name) != existing;
}

bool removeCheatFromFile(const std::string& sdRoot, const ProcessInfo& pi, const std::string& name) {
    const std::string path = cheatFilePath(sdRoot, pi);
    std::string existing;
    if (!readFile(path, existing)) return true;   // no hay archivo: nada que quitar
    const std::string updated = removeCheatBlock(existing, name);
    if (updated == existing) return true;
    if (updated.empty()) return removeFile(path);
    return writeFile(path, updated);
}

}  // namespace vnx
