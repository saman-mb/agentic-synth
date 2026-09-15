#include "mapper/EnvLoader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace agentic_synth::mapper {

namespace {

std::string strip(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    if (b - a >= 2 && ((s[a] == '"' && s[b - 1] == '"') || (s[a] == '\'' && s[b - 1] == '\'')))
        return s.substr(a + 1, b - a - 2);
    return s.substr(a, b - a);
}

std::string scan_env_file(const std::filesystem::path& p, const std::string& key) {
    std::ifstream f(p);
    if (!f)
        return {};
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(f, line)) {
        // Skip comments / blanks
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        if (line.compare(first, prefix.size(), prefix) == 0)
            return strip(line.substr(first + prefix.size()));
    }
    return {};
}

// Standalone/plugin hosts often launch with cwd=/ (or the DAW's cwd), so
// walking from getcwd alone misses a project `.env`. Also search from the
// executable — and for macOS .app bundles, Contents/Resources.
std::filesystem::path executable_dir() {
    namespace fs = std::filesystem;
    std::error_code ec;
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return {};
    std::vector<char> buf(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0)
        return {};
    auto p = fs::weakly_canonical(fs::path(buf.data()), ec);
    if (ec)
        p = fs::path(buf.data());
    return p.parent_path();
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
    auto p = fs::weakly_canonical(fs::path(buf), ec);
    if (ec)
        p = fs::path(buf);
    return p.parent_path();
#else
    (void)ec;
    return {};
#endif
}

std::string search_from(std::filesystem::path here, const std::string& key) {
    namespace fs = std::filesystem;
    if (here.empty())
        return {};
    std::error_code ec;
    for (int depth = 0; depth < 4; ++depth) {
        const auto candidate = here / ".env";
        if (fs::exists(candidate, ec) && !ec) {
            auto v = scan_env_file(candidate, key);
            if (!v.empty())
                return v;
        }
        // macOS bundle: TIMBRE.app/Contents/MacOS → sibling Resources/.env
        if (here.filename() == "MacOS") {
            const auto resourcesEnv = here.parent_path() / "Resources" / ".env";
            if (fs::exists(resourcesEnv, ec) && !ec) {
                auto v = scan_env_file(resourcesEnv, key);
                if (!v.empty())
                    return v;
            }
        }
        if (!here.has_parent_path() || here.parent_path() == here)
            break;
        here = here.parent_path();
    }
    return {};
}

} // namespace

std::string loadEnvKey(const std::string& key) {
    if (const char* v = std::getenv(key.c_str())) {
        if (*v)
            return std::string(v);
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    // 1) cwd walk (dev launches from the repo)
    if (auto v = search_from(fs::current_path(ec), key); !v.empty())
        return v;
    // 2) executable walk (Finder / `open` / DAW hosts with cwd=/)
    if (auto v = search_from(executable_dir(), key); !v.empty())
        return v;
    return {};
}

} // namespace agentic_synth::mapper
