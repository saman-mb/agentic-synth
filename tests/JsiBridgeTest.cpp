#include <catch2/catch_test_macros.hpp>

#include "agsynth.h"
#include "engine/PatchStruct.h"
#include "jsi/host/AgsynthHost.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <malloc.h>
#endif

#ifndef GOLDEN_DIR
#define GOLDEN_DIR ""
#endif

using agentic_synth::make_default_patch;
using agentic_synth::PatchStruct;
using agentic_synth::jsi::AGS_JSI_ERR_QUEUE;
using agentic_synth::jsi::AgsynthHost;

namespace {

constexpr uint32_t kGoldenSr = 44100;
constexpr uint32_t kGoldenFrames = 44100;
constexpr double kRtSr = 48000.0;
constexpr uint32_t kRtFrames = 256;
constexpr double kRtPeriodSec = static_cast<double>(kRtFrames) / kRtSr;

PatchStruct default_blob() { return make_default_patch(); }

ags_event note_on(uint8_t note, uint8_t vel, uint32_t offset) {
    ags_event e{};
    e.kind = AGS_EVENT_NOTE_ON;
    e.note = note;
    e.velocity = vel;
    e.sample_offset = offset;
    return e;
}

bool all_finite(const float* x, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i]))
            return false;
    }
    return true;
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

std::vector<std::filesystem::path> list_patch_bins() {
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
        if (entry.path().filename().string().ends_with(".patch.bin"))
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace

TEST_CASE("JSI host create rejects bad sample rate / block", "[jsi]") {
    REQUIRE(AgsynthHost::create(0.0, 8192) == nullptr);
    REQUIRE(AgsynthHost::create(44100.0, 0) == nullptr);
    REQUIRE(AgsynthHost::create(44100.0, 99999) == nullptr);
}

TEST_CASE("JSI host renderOffline memcmp ags_render_offline on 12 goldens", "[jsi]") {
    const auto fixtures = list_patch_bins();
    if (fixtures.empty())
        SKIP("no GOLDEN_DIR/ref/*.patch.bin");
    REQUIRE(fixtures.size() == 12);

    for (const auto& patch_path : fixtures) {
        const std::string name = patch_path.filename().string();
        const std::string id = name.substr(0, name.size() - std::strlen(".patch.bin"));
        SECTION(id) {
            const auto events_path = patch_path.parent_path() / (id + ".events.bin");
            std::vector<std::byte> patch_bytes;
            std::vector<std::byte> event_bytes;
            REQUIRE(read_all(patch_path, patch_bytes));
            REQUIRE(read_all(events_path, event_bytes));
            REQUIRE(event_bytes.size() % sizeof(ags_event) == 0);

            std::vector<ags_event> events(event_bytes.size() / sizeof(ags_event));
            if (!events.empty())
                std::memcpy(events.data(), event_bytes.data(), event_bytes.size());

            std::vector<float> host_buf(static_cast<std::size_t>(kGoldenFrames) * 2u, 0.0f);
            std::vector<float> capi_buf(host_buf.size(), 0.0f);
            const auto* ev = events.empty() ? nullptr : events.data();
            const auto n = static_cast<uint32_t>(events.size());

            REQUIRE(AgsynthHost::renderOffline(patch_bytes.data(), static_cast<uint32_t>(patch_bytes.size()), ev, n,
                                               static_cast<double>(kGoldenSr), kGoldenFrames,
                                               host_buf.data()) == AGS_OK);
            REQUIRE(ags_render_offline(patch_bytes.data(), static_cast<uint32_t>(patch_bytes.size()), ev, n,
                                       static_cast<double>(kGoldenSr), kGoldenFrames, capi_buf.data()) == AGS_OK);
            REQUIRE(std::memcmp(host_buf.data(), capi_buf.data(), host_buf.size() * sizeof(float)) == 0);
            REQUIRE(all_finite(host_buf.data(), host_buf.size()));
        }
    }
}

TEST_CASE("JSI host malformed size / nullptr / NaN keep process alive", "[jsi]") {
    auto* host = AgsynthHost::create(44100.0, 8192);
    REQUIRE(host != nullptr);

    REQUIRE(host->setPatch(nullptr, 0) == AGS_ERR_NULL);
    char tiny[3]{};
    REQUIRE(host->setPatch(tiny, 3) == AGS_ERR_SIZE);
    REQUIRE(host->setParam(nullptr, 1.0f) == AGS_ERR_NULL);
    REQUIRE(host->setParam("", 1.0f) == AGS_ERR_PARAM);
    REQUIRE(host->setParam("filter.cutoff_hz", std::nanf("")) == AGS_ERR_PARAM);
    REQUIRE(host->pushEvents(nullptr, 1) == AGS_ERR_NULL);

    std::vector<float> buf(256 * 2, 0.0f);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_OK);
    REQUIRE(host->alive());

    auto bad_ver = default_blob();
    bad_ver.version = 99;
    REQUIRE(host->setPatch(&bad_ver, static_cast<uint32_t>(sizeof(bad_ver))) == AGS_OK);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_ERR_PARAM);
    REQUIRE(host->alive());

    const auto good = default_blob();
    REQUIRE(host->setPatch(&good, static_cast<uint32_t>(sizeof(good))) == AGS_OK);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_OK);

    auto nan_patch = default_blob();
    nan_patch.filter.cutoff_hz = std::nanf("");
    REQUIRE(host->setPatch(&nan_patch, static_cast<uint32_t>(sizeof(nan_patch))) == AGS_OK);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_ERR_PARAM);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_OK);
    REQUIRE(host->alive());

    REQUIRE(host->setParam("not.a.path", 1.0f) == AGS_OK);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_ERR_PARAM);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_OK);
    REQUIRE(all_finite(buf.data(), buf.size()));

    AgsynthHost::destroy(host);
}

TEST_CASE("JSI host control-thread burst does not deadlock processBlock", "[jsi]") {
    auto* host = AgsynthHost::create(kRtSr, 8192);
    REQUIRE(host != nullptr);
    const auto patch = default_blob();
    REQUIRE(host->setPatch(&patch, static_cast<uint32_t>(sizeof(patch))) == AGS_OK);

    std::atomic<bool> done{false};
    std::atomic<int> rt_ok{0};
    std::atomic<int> bad{0};
    std::thread rt([&]() {
        std::vector<float> buf(kRtFrames * 2u, 0.0f);
        while (!done.load(std::memory_order_acquire)) {
            const int rc = host->processBlock(buf.data(), kRtFrames, 2);
            if (rc == AGS_OK)
                rt_ok.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread ctrl([&]() {
        for (int i = 0; i < 4000; ++i) {
            const int prc = host->setParam("filter.cutoff_hz", 200.0f + static_cast<float>(i % 8000));
            if (prc != AGS_OK && prc != AGS_JSI_ERR_QUEUE)
                bad.fetch_add(1, std::memory_order_relaxed);
            const ags_event on = note_on(60, 100, 0);
            const int erc = host->pushEvents(&on, 1);
            if (erc != AGS_OK && erc != AGS_JSI_ERR_QUEUE)
                bad.fetch_add(1, std::memory_order_relaxed);
            if ((i % 64) == 0) {
                ags_event off{};
                off.kind = AGS_EVENT_NOTE_OFF;
                off.note = 60;
                (void)host->pushEvents(&off, 1);
            }
        }
    });

    ctrl.join();
    done.store(true, std::memory_order_release);
    rt.join();
    REQUIRE(bad.load() == 0);
    REQUIRE(rt_ok.load() > 0);
    REQUIRE(host->alive());

    std::vector<float> buf(kRtFrames * 2u, 0.0f);
    REQUIRE(host->processBlock(buf.data(), kRtFrames, 2) == AGS_OK);
    AgsynthHost::destroy(host);
}

TEST_CASE("JSI host SR 44100 to 48000 recreate pairs destroy", "[jsi]") {
    auto* host = AgsynthHost::create(44100.0, 8192);
    REQUIRE(host != nullptr);
    REQUIRE(host->alive());
    REQUIRE(host->maxBlock() == 8192);

    const auto patch = default_blob();
    REQUIRE(host->setPatch(&patch, static_cast<uint32_t>(sizeof(patch))) == AGS_OK);
    const ags_event on = note_on(60, 100, 0);
    REQUIRE(host->pushEvents(&on, 1) == AGS_OK);

    std::vector<float> buf(256 * 2, 0.0f);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_OK);
    REQUIRE(all_finite(buf.data(), buf.size()));

    REQUIRE(host->recreate(48000.0, 8192) == AGS_OK);
    REQUIRE(host->alive());
    REQUIRE(host->sampleRate() == 48000.0);
    REQUIRE(host->maxBlock() == 8192);

    const ags_event on2 = note_on(64, 100, 0);
    REQUIRE(host->pushEvents(&on2, 1) == AGS_OK);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_OK);
    REQUIRE(all_finite(buf.data(), buf.size()));

    AgsynthHost::destroy(host);
}

TEST_CASE("JSI host processBlock does not grow heap (linux mallinfo2)", "[jsi]") {
#if defined(__linux__)
    auto* host = AgsynthHost::create(44100.0, 8192);
    REQUIRE(host != nullptr);
    const auto patch = default_blob();
    REQUIRE(host->setPatch(&patch, static_cast<uint32_t>(sizeof(patch))) == AGS_OK);
    const ags_event on = note_on(60, 100, 0);
    REQUIRE(host->pushEvents(&on, 1) == AGS_OK);

    std::vector<float> buf(256 * 2, 0.0f);
    REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_OK);

    struct mallinfo2 before = mallinfo2();
    for (int i = 0; i < 1000; ++i)
        REQUIRE(host->processBlock(buf.data(), 256, 2) == AGS_OK);
    struct mallinfo2 after = mallinfo2();
    REQUIRE(after.uordblks == before.uordblks);

    AgsynthHost::destroy(host);
#else
    SUCCEED("heap-growth assert is linux mallinfo2 only");
#endif
}

TEST_CASE("JSI host processBlock p50/p99 vs 256/48000 period", "[jsi]") {
    auto* host = AgsynthHost::create(kRtSr, 8192);
    REQUIRE(host != nullptr);
    const auto patch = default_blob();
    REQUIRE(host->setPatch(&patch, static_cast<uint32_t>(sizeof(patch))) == AGS_OK);
    const ags_event on = note_on(60, 100, 0);
    REQUIRE(host->pushEvents(&on, 1) == AGS_OK);

    std::vector<float> buf(kRtFrames * 2u, 0.0f);
    for (int i = 0; i < 32; ++i)
        REQUIRE(host->processBlock(buf.data(), kRtFrames, 2) == AGS_OK);

    std::vector<double> times(1000, 0.0);
    for (int i = 0; i < 1000; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        REQUIRE(host->processBlock(buf.data(), kRtFrames, 2) == AGS_OK);
        const auto t1 = std::chrono::steady_clock::now();
        times[static_cast<std::size_t>(i)] = std::chrono::duration<double>(t1 - t0).count();
        REQUIRE(all_finite(buf.data(), buf.size()));
    }

    std::sort(times.begin(), times.end());
    const double p50 = times[500];
    const double p99 = times[990];
    UNSCOPED_INFO("processBlock p50=" << (p50 * 1.0e3) << " ms p99=" << (p99 * 1.0e3)
                                      << " ms period=" << (kRtPeriodSec * 1.0e3) << " ms");
    if (p99 >= 0.5 * kRtPeriodSec)
        WARN("p99 exceeds 50% of period on this host; not failing CI");
    REQUIRE(std::isfinite(p50));
    REQUIRE(std::isfinite(p99));
    REQUIRE(p99 < kRtPeriodSec);

    AgsynthHost::destroy(host);
}

TEST_CASE("JSI host Linux AudioStream start/stop pairs", "[jsi]") {
    auto* host = AgsynthHost::create(kRtSr, 8192);
    REQUIRE(host != nullptr);
    const auto patch = default_blob();
    REQUIRE(host->setPatch(&patch, static_cast<uint32_t>(sizeof(patch))) == AGS_OK);

    uint8_t state[2048];
    REQUIRE(host->start() == AGS_OK);
    REQUIRE(host->streamRunning());
    REQUIRE(host->saveState(state, sizeof(state)) == AGS_ERR_STATE);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(host->stop() == AGS_OK);
    REQUIRE_FALSE(host->streamRunning());

    std::vector<float> buf(kRtFrames * 2u, 0.0f);
    REQUIRE(host->processBlock(buf.data(), kRtFrames, 2) == AGS_OK);
    REQUIRE(all_finite(buf.data(), buf.size()));
    AgsynthHost::destroy(host);
}
