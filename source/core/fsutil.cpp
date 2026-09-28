#include "fsutil.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdio>

namespace vnx {

bool makeDirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        cur += path[i];
        const bool last = i + 1 == path.size();
        if (path[i] != '/' && !last) continue;
        std::string dir = cur;
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        if (dir.empty() || dir.back() == ':') continue;   // "sdmc:" no es una carpeta
        mkdir(dir.c_str(), 0777);
    }
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool removeFile(const std::string& path) { return unlink(path.c_str()) == 0; }

bool readFile(const std::string& path, std::string& out) {
    out.clear();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool writeFile(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    if (fclose(f) != 0) ok = false;
    return ok;
}

uint64_t freeSpace(const std::string& path) {
    struct statvfs st;
    if (statvfs(path.c_str(), &st) != 0) return 0;
    uint64_t unit = st.f_frsize ? st.f_frsize : st.f_bsize;
    return (uint64_t)st.f_bavail * unit;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        out.push_back(name);
    }
    closedir(d);
    return out;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

}  // namespace vnx
