#include <catch2/catch_test_macros.hpp>

#include "agsynth.h"
#include "engine/PatchStruct.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef GOLDEN_DIR
#define GOLDEN_DIR ""
#endif

using agentic_synth::make_default_patch;
using agentic_synth::PatchStruct;

namespace {

// U1 a-priori bounds (tests/golden/README.md). Do not tighten after seeing diffs.
constexpr double kRmsRatioLo = 0.25;
constexpr double kRmsRatioHi = 4.0;
constexpr double kBucketCosineMin = 0.85;
constexpr int kBuckets = 10;
constexpr double kRelErrRmsMax = 1.6;
constexpr double kPeakErrMax = 2.0;

constexpr uint32_t kSr = 44100;
constexpr uint32_t kFrames = 44100;
constexpr uint32_t kNoteOff = 22050;

ags_event make_event(uint32_t kind, uint8_t note, uint8_t vel, uint32_t offset) {
    ags_event e{};
    e.kind = kind;
    e.note = note;
    e.velocity = vel;
    e.sample_offset = offset;
    return e;
}

double rms_all(const float* x, std::size_t n) {
    if (n == 0)
        return 0.0;
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        s += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return std::sqrt(s / static_cast<double>(n));
}

double rms_frames(const std::vector<float>& interleaved, uint32_t start, uint32_t end) {
    if (end <= start)
        return 0.0;
    const std::size_t n = static_cast<std::size_t>(end - start) * 2u;
    return rms_all(interleaved.data() + static_cast<std::size_t>(start) * 2u, n);
}

bool all_finite(const float* x, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i]))
            return false;
    }
    return true;
}

double bucket_cosine(const float* x, const float* ref, std::size_t n) {
    if (n == 0)
        return 0.0;
    double bx[kBuckets]{};
    double br[kBuckets]{};
    const std::size_t base = n / static_cast<std::size_t>(kBuckets);
    for (int b = 0; b < kBuckets; ++b) {
        const std::size_t start = static_cast<std::size_t>(b) * base;
        const std::size_t end = (b + 1 == kBuckets) ? n : start + base;
        bx[b] = rms_all(x + start, end - start);
        br[b] = rms_all(ref + start, end - start);
    }
    double dot = 0.0;
    double nx = 0.0;
    double nr = 0.0;
    for (int b = 0; b < kBuckets; ++b) {
        dot += bx[b] * br[b];
        nx += bx[b] * bx[b];
        nr += br[b] * br[b];
    }
    if (nx == 0.0 && nr == 0.0)
        return 1.0;
    if (nx == 0.0 || nr == 0.0)
        return 0.0;
    return dot / (std::sqrt(nx) * std::sqrt(nr));
}

struct U1Metrics {
    bool same_length{false};
    bool finite{false};
    double rms_ratio{0.0};
    double cosine{0.0};
    double rel_err_rms{0.0};
    double peak_err{0.0};
};

U1Metrics measure_u1(const std::vector<float>& x, const std::vector<float>& ref) {
    U1Metrics m;
    m.same_length = x.size() == ref.size();
    if (!m.same_length || x.empty())
        return m;
    m.finite = all_finite(x.data(), x.size()) && all_finite(ref.data(), ref.size());
    const double rms_x = rms_all(x.data(), x.size());
    const double rms_r = rms_all(ref.data(), ref.size());
    if (rms_r == 0.0)
        m.rms_ratio = (rms_x == 0.0) ? 1.0 : 0.0;
    else
        m.rms_ratio = rms_x / rms_r;
    m.cosine = bucket_cosine(x.data(), ref.data(), x.size());
    double err_sq = 0.0;
    double peak = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double d = static_cast<double>(x[i]) - static_cast<double>(ref[i]);
        err_sq += d * d;
        peak = std::max(peak, std::abs(d));
    }
    const double rms_err = std::sqrt(err_sq / static_cast<double>(x.size()));
    m.rel_err_rms = (rms_r == 0.0) ? ((rms_err == 0.0) ? 0.0 : 1.0e9) : rms_err / rms_r;
    m.peak_err = peak;
    return m;
}

bool passes_u1(const U1Metrics& m, double rel_err_max = kRelErrRmsMax) {
    return m.same_length && m.finite && m.rms_ratio >= kRmsRatioLo && m.rms_ratio <= kRmsRatioHi &&
           m.cosine >= kBucketCosineMin && m.rel_err_rms <= rel_err_max && m.peak_err <= kPeakErrMax;
}

void skip_ws(const std::string& s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0)
        ++i;
}

bool parse_json_string(const std::string& s, std::size_t& i, std::string& out) {
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '"')
        return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\') {
            ++i;
            if (i >= s.size())
                return false;
        }
        out.push_back(s[i]);
        ++i;
    }
    if (i >= s.size())
        return false;
    ++i;
    return true;
}

bool parse_json_number(const std::string& s, std::size_t& i, double& out) {
    skip_ws(s, i);
    char* end = nullptr;
    out = std::strtod(s.c_str() + i, &end);
    if (end == s.c_str() + i)
        return false;
    i = static_cast<std::size_t>(end - s.c_str());
    return true;
}

std::unordered_map<std::string, double> parse_object_string_numbers(const std::string& s, std::size_t& i) {
    std::unordered_map<std::string, double> out;
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '{')
        return out;
    ++i;
    while (i < s.size()) {
        skip_ws(s, i);
        if (i < s.size() && s[i] == '}') {
            ++i;
            break;
        }
        std::string key;
        if (!parse_json_string(s, i, key))
            break;
        skip_ws(s, i);
        if (i >= s.size() || s[i] != ':')
            break;
        ++i;
        double v = 0.0;
        if (!parse_json_number(s, i, v))
            break;
        out.emplace(std::move(key), v);
        skip_ws(s, i);
        if (i < s.size() && s[i] == ',')
            ++i;
    }
    return out;
}

std::unordered_map<std::string, double> load_err_rms_by_id() {
    const auto path = std::filesystem::path(GOLDEN_DIR) / "manifest.json";
    std::ifstream f(path);
    if (!f)
        return {};
    const std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto key = std::string("\"err_rms_ratio_by_id\"");
    auto pos = json.find(key);
    if (pos == std::string::npos)
        return {};
    pos += key.size();
    std::size_t i = pos;
    skip_ws(json, i);
    if (i < json.size() && json[i] == ':')
        ++i;
    return parse_object_string_numbers(json, i);
}

double rel_err_max_for(const std::string& id) {
    static const auto by_id = load_err_rms_by_id();
    const auto it = by_id.find(id);
    if (it != by_id.end() && it->second > 0.0)
        return it->second;
    return kRelErrRmsMax;
}

bool read_all(const std::filesystem::path& p, std::vector<std::byte>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n < 0)
        return false;
    f.seekg(0);
    out.resize(static_cast<std::size_t>(n));
    if (out.empty())
        return true;
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}

bool read_f32le(const std::filesystem::path& p, std::vector<float>& out) {
    std::vector<std::byte> raw;
    if (!read_all(p, raw) || raw.size() % sizeof(float) != 0)
        return false;
    out.resize(raw.size() / sizeof(float));
    std::memcpy(out.data(), raw.data(), raw.size());
    return true;
}

std::vector<std::filesystem::path> list_f32le() {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path dir = std::filesystem::path(GOLDEN_DIR) / "ref";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() == ".f32le")
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

int render_offline(const void* patch, uint32_t patch_len, const ags_event* events, uint32_t event_count,
                   std::vector<float>& buf) {
    buf.assign(static_cast<std::size_t>(kFrames) * 2u, 0.0f);
    return ags_render_offline(patch, patch_len, events, event_count, static_cast<double>(kSr), kFrames, buf.data());
}

void maybe_dump_native(const std::string& id, const std::vector<float>& native) {
    const char* dir = std::getenv("AGS_GOLDEN_DUMP_DIR");
    if (dir == nullptr || dir[0] == '\0')
        return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    INFO("AGS_GOLDEN_DUMP_DIR=" << dir << " id=" << id);
    REQUIRE_FALSE(ec);
    const auto path = std::filesystem::path(dir) / (id + ".f32le");
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out);
    out.write(reinterpret_cast<const char*>(native.data()),
              static_cast<std::streamsize>(native.size() * sizeof(float)));
    REQUIRE(static_cast<bool>(out));
}

} // namespace

TEST_CASE("offline render fires note-off past 8192-sample chunks") {
    const auto patch = make_default_patch();
    const ags_event evs[] = {
        make_event(AGS_EVENT_NOTE_ON, 60, 100, 0),
        make_event(AGS_EVENT_NOTE_OFF, 60, 0, kNoteOff),
    };
    std::vector<float> buf;
    REQUIRE(render_offline(&patch, static_cast<uint32_t>(sizeof(patch)), evs, 2, buf) == AGS_OK);

    const double hold = rms_frames(buf, kNoteOff / 2, kNoteOff);
    const uint32_t tail_start = kNoteOff + 8820; // 200 ms after off; release is 100 ms
    const double tail = rms_frames(buf, tail_start, kFrames);

    REQUIRE(hold > 0.01);
    REQUIRE(tail < hold * 0.1);
    REQUIRE(tail < 0.01);
}

TEST_CASE("cutoff mutation exceeds U1 golden-parity bounds") {
    auto patch = make_default_patch();
    const ags_event evs[] = {
        make_event(AGS_EVENT_NOTE_ON, 60, 100, 0),
        make_event(AGS_EVENT_NOTE_OFF, 60, 0, kNoteOff),
    };
    std::vector<float> ref;
    REQUIRE(render_offline(&patch, static_cast<uint32_t>(sizeof(patch)), evs, 2, ref) == AGS_OK);

    patch.filter.cutoff_hz = 20.0f;
    std::vector<float> mutated;
    REQUIRE(render_offline(&patch, static_cast<uint32_t>(sizeof(patch)), evs, 2, mutated) == AGS_OK);

    const U1Metrics m = measure_u1(mutated, ref);
    INFO("rms_ratio=" << m.rms_ratio << " cosine=" << m.cosine << " rel_err=" << m.rel_err_rms
                      << " peak=" << m.peak_err);
    REQUIRE_FALSE(passes_u1(m));

    const auto refs = list_f32le();
    if (!refs.empty()) {
        const auto& f32 = refs.front();
        const std::string id = f32.stem().string();
        std::vector<float> golden;
        REQUIRE(read_f32le(f32, golden));
        std::vector<std::byte> patch_bytes;
        REQUIRE(read_all(f32.parent_path() / (id + ".patch.bin"), patch_bytes));
        std::vector<std::byte> event_bytes;
        REQUIRE(read_all(f32.parent_path() / (id + ".events.bin"), event_bytes));
        REQUIRE(patch_bytes.size() == sizeof(PatchStruct));
        REQUIRE(event_bytes.size() % sizeof(ags_event) == 0);

        PatchStruct drifted{};
        std::memcpy(&drifted, patch_bytes.data(), sizeof(drifted));
        drifted.filter.cutoff_hz = 20.0f;

        std::vector<ags_event> events(event_bytes.size() / sizeof(ags_event));
        if (!events.empty())
            std::memcpy(events.data(), event_bytes.data(), event_bytes.size());

        const uint32_t frames = static_cast<uint32_t>(golden.size() / 2);
        std::vector<float> native(golden.size(), 0.0f);
        REQUIRE(ags_render_offline(&drifted, static_cast<uint32_t>(sizeof(drifted)),
                                   events.empty() ? nullptr : events.data(), static_cast<uint32_t>(events.size()),
                                   static_cast<double>(kSr), frames, native.data()) == AGS_OK);
        const U1Metrics vs_golden = measure_u1(native, golden);
        INFO("vs_golden rms_ratio=" << vs_golden.rms_ratio);
        REQUIRE_FALSE(passes_u1(vs_golden));
    }
}

TEST_CASE("native offline matches WebAudio goldens within U1 bounds") {
    const auto refs = list_f32le();
    if (refs.empty())
        SKIP("no GOLDEN_DIR/ref/*.f32le; synthetic tests still cover the timeline");

    for (const auto& f32 : refs) {
        const std::string id = f32.stem().string();
        SECTION(id) {
            const auto patch_path = f32.parent_path() / (id + ".patch.bin");
            const auto events_path = f32.parent_path() / (id + ".events.bin");

            std::vector<float> golden;
            REQUIRE(read_f32le(f32, golden));
            REQUIRE(golden.size() % 2 == 0);

            std::vector<std::byte> patch_bytes;
            REQUIRE(read_all(patch_path, patch_bytes));
            std::vector<std::byte> event_bytes;
            REQUIRE(read_all(events_path, event_bytes));
            REQUIRE(event_bytes.size() % sizeof(ags_event) == 0);

            std::vector<ags_event> events(event_bytes.size() / sizeof(ags_event));
            if (!events.empty())
                std::memcpy(events.data(), event_bytes.data(), event_bytes.size());

            const uint32_t frames = static_cast<uint32_t>(golden.size() / 2);
            std::vector<float> native(golden.size(), 0.0f);
            REQUIRE(ags_render_offline(patch_bytes.data(), static_cast<uint32_t>(patch_bytes.size()),
                                       events.empty() ? nullptr : events.data(), static_cast<uint32_t>(events.size()),
                                       static_cast<double>(kSr), frames, native.data()) == AGS_OK);
            maybe_dump_native(id, native);

            const U1Metrics m = measure_u1(native, golden);
            const double rel_max = rel_err_max_for(id);
            INFO("rms_ratio=" << m.rms_ratio << " cosine=" << m.cosine << " rel_err=" << m.rel_err_rms
                              << " rel_err_max=" << rel_max << " peak=" << m.peak_err);
            CHECK(m.same_length);
            CHECK(m.finite);
            CHECK(m.rms_ratio >= kRmsRatioLo);
            CHECK(m.rms_ratio <= kRmsRatioHi);
            CHECK(m.cosine >= kBucketCosineMin);
            CHECK(m.peak_err <= kPeakErrMax);
            CHECK(m.rel_err_rms <= rel_max);
        }
    }
}

TEST_CASE("manifest err_rms_ratio is per-id with 1.6 default") {
    CHECK(rel_err_max_for("pulse") == 2.5);
    CHECK(rel_err_max_for("tri") == 2.5);
    CHECK(rel_err_max_for("wavetable") == 2.5);
    CHECK(rel_err_max_for("fm") == 2.5);
    CHECK(rel_err_max_for("sine") == 1.6);
    CHECK(rel_err_max_for("saw") == 1.6);
    CHECK(rel_err_max_for("square") == 1.6);
    CHECK(rel_err_max_for("no-such-fixture") == 1.6);
}
