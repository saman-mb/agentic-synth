#include "agent/Telemetry.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <process.h>
static int current_pid() noexcept { return static_cast<int>(::_getpid()); }
#else
#include <unistd.h>
static int current_pid() noexcept { return static_cast<int>(::getpid()); }
#endif

namespace agentic_synth::agent {

std::string Telemetry::defaultLogPath() {
    std::string dir;
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    dir = appdata ? (std::string(appdata) + "\\agentic-synth\\telemetry") : "agentic-synth-telemetry";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    dir = home ? (std::string(home) + "/Library/Application Support/agentic-synth/telemetry")
               : "/tmp/agentic-synth-telemetry";
#else
    // Linux: prefer XDG_DATA_HOME, fall back to ~/.local/share
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        dir = std::string(xdg) + "/agentic-synth/telemetry";
    } else {
        const char* home = std::getenv("HOME");
        dir = home ? (std::string(home) + "/.local/share/agentic-synth/telemetry") : "/tmp/agentic-synth-telemetry";
    }
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/telemetry_" + std::to_string(current_pid()) + ".json";
}

Telemetry::Telemetry(std::string log_path) : log_path_(std::move(log_path)) {}

void Telemetry::recordGeneration(double latency_ms, int tokens, double elapsed_s, bool success,
                                 std::string error_type) {
    if (!enabled_)
        return;
    GenerationRecord r;
    r.timestamp_ms = nowMs();
    r.latency_ms = latency_ms;
    r.token_count = tokens;
    r.tokens_per_second = elapsed_s > 0.0 ? static_cast<double>(tokens) / elapsed_s : 0.0;
    r.success = success;
    r.error_type = std::move(error_type);
    records_.push_back(std::move(r));
}

void Telemetry::recordUiEvent(std::string kind, std::string detail) {
    if (!enabled_)
        return;
    UiEvent e;
    e.ts_ms = nowMs();
    e.kind = std::move(kind);
    e.detail = std::move(detail);

    std::lock_guard<std::mutex> lock(ui_mutex_);
    if (ui_events_.size() >= kUiEventCap) {
        // Drop oldest. Vector-erase at front is O(n) but n<=256 and these events
        // are infrequent (page loads), so a deque is unnecessary complexity.
        ui_events_.erase(ui_events_.begin());
    }
    ui_events_.push_back(std::move(e));
}

std::vector<UiEvent> Telemetry::uiEvents() const {
    std::lock_guard<std::mutex> lock(ui_mutex_);
    return ui_events_;
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string Telemetry::toJson() const {
    const int total = static_cast<int>(records_.size());
    int errors = 0;
    double sum_lat = 0.0, sum_tps = 0.0;

    std::vector<double> lats;
    lats.reserve(records_.size());

    for (const auto& r : records_) {
        if (!r.success)
            ++errors;
        sum_lat += r.latency_ms;
        sum_tps += r.tokens_per_second;
        lats.push_back(r.latency_ms);
    }
    std::sort(lats.begin(), lats.end());

    auto pct = [&](double p) -> double {
        if (lats.empty())
            return 0.0;
        const size_t idx = std::min(static_cast<size_t>(p * static_cast<double>(lats.size())), lats.size() - 1);
        return lats[idx];
    };

    const double avg_lat = total > 0 ? sum_lat / total : 0.0;
    const double avg_tps = total > 0 ? sum_tps / total : 0.0;
    const double err_rate = total > 0 ? static_cast<double>(errors) / total : 0.0;

    std::ostringstream ss;
    ss << std::fixed;
    ss << "{\"enabled\":" << (enabled_ ? "true" : "false") << ",\"summary\":{" << "\"total_generations\":" << total
       << ",\"error_count\":" << errors << ",\"error_rate\":" << err_rate << ",\"avg_latency_ms\":" << avg_lat
       << ",\"p50_latency_ms\":" << pct(0.5) << ",\"p95_latency_ms\":" << pct(0.95)
       << ",\"avg_tokens_per_second\":" << avg_tps << "},\"records\":[";

    for (size_t i = 0; i < records_.size(); ++i) {
        const auto& r = records_[i];
        if (i > 0)
            ss << ',';
        ss << "{\"ts\":" << r.timestamp_ms << ",\"latency_ms\":" << r.latency_ms << ",\"tokens\":" << r.token_count
           << ",\"tps\":" << r.tokens_per_second << ",\"ok\":" << (r.success ? "true" : "false");
        if (!r.error_type.empty())
            ss << ",\"error\":\"" << jsonEscape(r.error_type) << '"';
        ss << '}';
    }
    ss << "],\"ui_events\":[";

    // Snapshot under lock so we don't race a concurrent recordUiEvent.
    std::vector<UiEvent> ui_snapshot;
    {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        ui_snapshot = ui_events_;
    }
    for (size_t i = 0; i < ui_snapshot.size(); ++i) {
        const auto& e = ui_snapshot[i];
        if (i > 0)
            ss << ',';
        ss << "{\"ts\":" << e.ts_ms << ",\"kind\":\"" << jsonEscape(e.kind) << "\",\"detail\":\""
           << jsonEscape(e.detail) << "\"}";
    }
    ss << "]}";
    return ss.str();
}

void Telemetry::flush() {
    if (!enabled_ || log_path_.empty())
        return;
    std::ofstream f(log_path_, std::ios::out | std::ios::trunc);
    if (f)
        f << toJson();
}

int64_t Telemetry::nowMs() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace agentic_synth::agent
