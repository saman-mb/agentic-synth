#include "mapper/LlmTelemetry.h"

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace agentic_synth::mapper {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

// Format `now` as `YYYY-MM-DDTHH:MM:SSZ` (UTC, no sub-second precision).
std::string iso8601_now() {
    using namespace std::chrono;
    const auto tp = system_clock::now();
    const std::time_t t = system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

} // namespace

LlmTelemetry& LlmTelemetry::instance() {
    static LlmTelemetry inst;
    return inst;
}

std::string LlmTelemetry::defaultPath() {
    std::string dir;
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    dir = appdata ? (std::string(appdata) + "\\AgenticSynth") : "AgenticSynth";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    dir = home ? (std::string(home) + "/Library/Application Support/AgenticSynth")
               : "/tmp/AgenticSynth";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        dir = std::string(xdg) + "/AgenticSynth";
    } else {
        const char* home = std::getenv("HOME");
        dir = home ? (std::string(home) + "/.local/share/AgenticSynth")
                   : "/tmp/AgenticSynth";
    }
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // Swallow ec — log() is no-throw and will simply skip the write if the
    // dir didn't materialise.
    return dir + "/llm_telemetry.jsonl";
}

void LlmTelemetry::setPathForTest(std::string p) noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    path_ = std::move(p);
    resolved_ = !path_.empty();
}

void LlmTelemetry::log(const LlmCall& call) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mu_);
        if (!resolved_) {
            path_ = defaultPath();
            resolved_ = true;
        }
        if (path_.empty())
            return;

        std::ofstream f(path_, std::ios::out | std::ios::app);
        if (!f)
            return;

        const std::string ts = call.ts.empty() ? iso8601_now() : call.ts;

        std::ostringstream ss;
        ss << "{"
           << "\"ts\":\""               << jsonEscape(ts)               << "\","
           << "\"caller\":\""           << jsonEscape(call.caller)      << "\","
           << "\"model\":\""            << jsonEscape(call.model)       << "\","
           << "\"attempts\":"           << call.attempts                << ","
           << "\"latency_ms\":"         << call.latency_ms              << ","
           << "\"body_size_bytes\":"    << call.body_size_bytes         << ","
           << "\"prompt_size_bytes\":"  << call.prompt_size_bytes       << ","
           << "\"finish_reason\":\""    << jsonEscape(call.finish_reason) << "\","
           << "\"block_reason\":\""     << jsonEscape(call.block_reason)  << "\","
           << "\"outcome\":\""          << jsonEscape(call.outcome)       << "\""
           << "}\n";

        f << ss.str();
        f.flush();
    } catch (...) {
        // Telemetry never throws.
    }
}

} // namespace agentic_synth::mapper
