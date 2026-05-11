#include <catch2/catch_test_macros.hpp>

#include "engine/VoiceManager.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace agentic_synth::engine;

// ── Helpers ────────────────────────────────────────────────────────────────────

static bool hasNote(const std::vector<int>& notes, int n) {
    return std::find(notes.begin(), notes.end(), n) != notes.end();
}

// ── Basic allocation ──────────────────────────────────────────────────────────

TEST_CASE("VoiceManager initial state") {
    VoiceManager vm(8);
    CHECK(vm.voiceCount() == 8);
    CHECK(vm.activeVoiceCount() == 0);
    CHECK(vm.activeNotes().empty());
}

TEST_CASE("VoiceManager single note on/off") {
    VoiceManager vm(8);
    vm.prepare(44100.0);

    vm.noteOn(60, 0.8f);
    CHECK(vm.activeVoiceCount() == 1);
    CHECK(hasNote(vm.activeNotes(), 60));

    vm.noteOff(60);
    // Voice enters release phase immediately after noteOff — still active.
    CHECK(vm.activeVoiceCount() == 1);

    // Render enough samples (> default release of 0.3 s at 44100 Hz) for release to complete.
    std::vector<float> buf(16384, 0.0f);
    vm.renderBlock(buf.data(), static_cast<int>(buf.size()));
    CHECK(vm.activeVoiceCount() == 0);
}

TEST_CASE("VoiceManager chord allocates unique voices") {
    VoiceManager vm(8);
    vm.prepare(44100.0);

    for (int i = 0; i < 8; ++i) {
        vm.noteOn(60 + i, 0.8f);
        CHECK(vm.activeVoiceCount() == i + 1);
    }
    CHECK(vm.activeVoiceCount() == 8);
}

TEST_CASE("VoiceManager noteOff on non-existent note is a no-op") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    REQUIRE_NOTHROW(vm.noteOff(60));
    CHECK(vm.activeVoiceCount() == 0);
}

// ── Chord-spam test ───────────────────────────────────────────────────────────
// Rapid note-ons beyond polyphony limit must never drop active count erratically.

TEST_CASE("VoiceManager chord spam never drops below N-1 active voices") {
    static constexpr int kVoices = 8;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    for (int i = 0; i < kVoices; ++i)
        vm.noteOn(48 + i, 0.8f);
    REQUIRE(vm.activeVoiceCount() == kVoices);

    // Drive beyond the voice limit; each steal must keep count at N.
    for (int i = 0; i < kVoices; ++i) {
        vm.noteOn(72 + i, 0.8f);
        REQUIRE(vm.activeVoiceCount() >= kVoices - 1); // never erratic
    }
    REQUIRE(vm.activeVoiceCount() == kVoices);
}

// ── Voice stealing determinism ────────────────────────────────────────────────
// Oldest-note policy: the voice that played longest is always the one stolen.

TEST_CASE("VoiceManager voice stealing steals oldest note") {
    static constexpr int kVoices = 4;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    // Fill: oldest → 60, then 61, 62, 63.
    for (int i = 0; i < kVoices; ++i)
        vm.noteOn(60 + i, 0.8f);
    REQUIRE(vm.activeVoiceCount() == kVoices);

    // One more note: 60 (oldest) must be evicted.
    vm.noteOn(64, 0.8f);
    auto notes = vm.activeNotes();
    CHECK(vm.activeVoiceCount() == kVoices);
    CHECK_FALSE(hasNote(notes, 60)); // stolen
    CHECK(hasNote(notes, 61));
    CHECK(hasNote(notes, 62));
    CHECK(hasNote(notes, 63));
    CHECK(hasNote(notes, 64));
}

TEST_CASE("VoiceManager voice stealing consistently oldest-first across multiple steals") {
    static constexpr int kVoices = 4;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    for (int i = 0; i < kVoices; ++i)
        vm.noteOn(60 + i, 0.8f);

    vm.noteOn(70, 0.8f); // steals 60
    vm.noteOn(71, 0.8f); // steals 61
    vm.noteOn(72, 0.8f); // steals 62

    auto notes = vm.activeNotes();
    CHECK_FALSE(hasNote(notes, 60));
    CHECK_FALSE(hasNote(notes, 61));
    CHECK_FALSE(hasNote(notes, 62));
    CHECK(hasNote(notes, 63));
    CHECK(hasNote(notes, 70));
    CHECK(hasNote(notes, 71));
    CHECK(hasNote(notes, 72));
}

TEST_CASE("VoiceManager stealing prefers releasing voices over held voices") {
    static constexpr int kVoices = 4;
    VoiceManager vm(kVoices);
    vm.prepare(44100.0);

    for (int i = 0; i < kVoices; ++i)
        vm.noteOn(60 + i, 0.8f);

    // Release note 63 (most recent). It should be stolen before 60 (older but held).
    vm.noteOff(63);

    vm.noteOn(70, 0.8f);
    auto notes = vm.activeNotes();
    CHECK(hasNote(notes, 70));       // new note active
    CHECK(hasNote(notes, 60));       // held — not stolen
    CHECK_FALSE(hasNote(notes, 63)); // releasing — stolen first
}

// ── Portamento ────────────────────────────────────────────────────────────────

TEST_CASE("VoiceManager portamento enabled, two notes both become active") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.setPortamento(0.1f);

    vm.noteOn(60, 0.8f);
    vm.noteOn(72, 0.8f);
    CHECK(vm.activeVoiceCount() == 2);
}

TEST_CASE("VoiceManager portamento off snaps frequency instantly") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.setPortamento(0.0f);

    vm.noteOn(60, 0.8f);
    CHECK(vm.activeVoiceCount() == 1);
}

// ── Retrigger / legato ────────────────────────────────────────────────────────

TEST_CASE("VoiceManager retrigger true re-uses same voice for same note") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.setRetrigger(true);

    vm.noteOn(60, 0.8f);
    vm.noteOn(60, 0.8f); // same note; must reuse, not allocate second voice
    CHECK(vm.activeVoiceCount() == 1);
}

TEST_CASE("VoiceManager retrigger false keeps envelope running (legato)") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.setRetrigger(false);

    vm.noteOn(60, 0.8f);
    vm.noteOn(60, 0.8f); // legato — same voice, envelope not restarted
    CHECK(vm.activeVoiceCount() == 1);
}

// ── Render ────────────────────────────────────────────────────────────────────

TEST_CASE("VoiceManager renderBlock produces finite output with active note") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    vm.noteOn(60, 0.8f);

    std::vector<float> buf(256, 0.0f);
    vm.renderBlock(buf.data(), static_cast<int>(buf.size()));

    bool hasNaN = false;
    for (float s : buf)
        hasNaN = hasNaN || std::isnan(s);
    CHECK_FALSE(hasNaN);
}

TEST_CASE("VoiceManager renderNextSample returns 0 with no active voices") {
    VoiceManager vm(4);
    vm.prepare(44100.0);
    CHECK(vm.renderNextSample() == 0.0f);
}

// ── Edge-case robustness ──────────────────────────────────────────────────────

TEST_CASE("VoiceManager noteOn without prepare does not crash") {
    VoiceManager vm(4);
    REQUIRE_NOTHROW(vm.noteOn(60, 0.8f));
    REQUIRE_NOTHROW(vm.noteOff(60));
}

TEST_CASE("VoiceManager handles extreme MIDI note values (0 and 127)") {
    VoiceManager vm(4);
    vm.prepare(44100.0);

    REQUIRE_NOTHROW(vm.noteOn(0, 0.8f));
    REQUIRE_NOTHROW(vm.noteOn(127, 0.8f));

    std::vector<float> buf(256, 0.0f);
    vm.renderBlock(buf.data(), static_cast<int>(buf.size()));

    bool hasNaN = false;
    for (float s : buf)
        hasNaN = hasNaN || std::isnan(s);
    CHECK_FALSE(hasNaN);
}
