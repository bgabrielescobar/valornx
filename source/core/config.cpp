#include "config.hpp"

#include <cstdio>
#include <cstdlib>

#include "fsutil.hpp"
#include "value.hpp"

namespace vnx {

bool loadConfig(const std::string& path, Config& cfg) {
    std::string text;
    if (!readFile(path, text)) return false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (k == "zona" && v >= 0 && v <= 2) cfg.zone = (SearchZone)v;
        if (k == "tipo" && v >= 0 && v < typeChoiceCount()) cfg.typeChoice = v;
    }
    return true;
}

bool saveConfig(const std::string& path, const Config& cfg) {
    char b[96];
    snprintf(b, sizeof(b), "zona=%d\ntipo=%d\n", (int)cfg.zone, cfg.typeChoice);
    return writeFile(path, b);
}

}  // namespace vnx
