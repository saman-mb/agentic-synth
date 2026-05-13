#include "mapper/EnvLoader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

} // namespace

std::string loadEnvKey(const std::string& key) {
    if (const char* v = std::getenv(key.c_str())) {
        if (*v)
            return std::string(v);
    }
    // Walk cwd + up to 3 parents looking for a `.env` file.
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path here = fs::current_path(ec);
    if (ec)
        return {};
    for (int depth = 0; depth < 4; ++depth) {
        const auto candidate = here / ".env";
        if (fs::exists(candidate, ec) && !ec) {
            auto v = scan_env_file(candidate, key);
            if (!v.empty())
                return v;
        }
        if (!here.has_parent_path() || here.parent_path() == here)
            break;
        here = here.parent_path();
    }
    return {};
}

} // namespace agentic_synth::mapper
