#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "engine/PatchStruct.h"
#include "mapper/ArchetypeLibrary.h"
#include "mapper/ArchetypeRetriever.h"
#include "mapper/DeltaNudger.h"

using agentic_synth::PatchStruct;
using agentic_synth::make_default_patch;
using agentic_synth::mapper::Archetype;
using agentic_synth::mapper::ArchetypeLibrary;
using agentic_synth::mapper::ArchetypeRetriever;
using agentic_synth::mapper::applyNudges;
using agentic_synth::mapper::DeltaNudger;
using agentic_synth::mapper::DeltaNudgerConfig;
using agentic_synth::mapper::Nudge;
using agentic_synth::mapper::NudgeRequest;
using agentic_synth::mapper::NudgeResult;

// ── enabled() / no-key behaviour ─────────────────────────────────────────────

TEST_CASE("DeltaNudger: enabled() reflects API-key presence") {
    DeltaNudger d;
    CHECK_FALSE(d.enabled());
    d.setApiKey("AIzaTestKey");
    CHECK(d.enabled());
}

TEST_CASE("DeltaNudger: nudge() without API key returns selected_index=-1 and top-1 patch") {
    DeltaNudger d;
    auto top3 = ArchetypeRetriever::retrieveTopN("cinematic pad blade runner", 3);
    REQUIRE(top3.size() >= 1);

    NudgeRequest req{"cinematic pad blade runner", top3};
    NudgeResult r = d.nudge(req);

    CHECK(r.selected_index == -1);
    // Fallback patch == top3[0] verbatim (filter cutoff carried through).
    CHECK(r.patch.filter.cutoff_hz == top3[0]->patch.filter.cutoff_hz);
}

TEST_CASE("DeltaNudger: nudge() with empty top3 returns -1 even with key set") {
    DeltaNudger d;
    d.setApiKey("AIzaTestKey");
    NudgeRequest req{"whatever", {}};
    NudgeResult r = d.nudge(req);
    CHECK(r.selected_index == -1);
}

// ── applyNudges: percentage math + clamping ──────────────────────────────────

TEST_CASE("applyNudges: positive percentage scales the field") {
    PatchStruct base = make_default_patch();
    base.filter.cutoff_hz = 1000.0f;
    std::vector<Nudge> ns{{"filter.cutoff_hz", 20.0f}};
    PatchStruct out = applyNudges(base, ns);
    // 1000 * 1.20 = 1200 — within engine range, no clamping kicks in.
    CHECK(out.filter.cutoff_hz == 1200.0f);
}

TEST_CASE("applyNudges: negative percentage scales the field down") {
    PatchStruct base = make_default_patch();
    base.amp_env.attack_s = 1.0f;
    std::vector<Nudge> ns{{"amp_env.attack_s", -50.0f}};
    PatchStruct out = applyNudges(base, ns);
    CHECK(out.amp_env.attack_s == 0.5f);
}

TEST_CASE("applyNudges: exceeds per-axis cap, gets clamped to cap") {
    PatchStruct base = make_default_patch();
    base.filter.cutoff_hz = 1000.0f;
    // Caller asks for +200% on filter.cutoff_hz (capped to +30%).
    std::vector<Nudge> ns{{"filter.cutoff_hz", 200.0f}};
    PatchStruct out = applyNudges(base, ns);
    // 1000 * 1.30 = 1300
    CHECK(out.filter.cutoff_hz == 1300.0f);
}

TEST_CASE("applyNudges: hits engine ceiling — clamped to 18000 Hz") {
    PatchStruct base = make_default_patch();
    base.filter.cutoff_hz = 17000.0f;
    // +30% would land at 22100 — engine ceiling is 18000.
    std::vector<Nudge> ns{{"filter.cutoff_hz", 30.0f}};
    PatchStruct out = applyNudges(base, ns);
    CHECK(out.filter.cutoff_hz == 18000.0f);
}

TEST_CASE("applyNudges: unknown path is silently dropped") {
    PatchStruct base = make_default_patch();
    base.filter.cutoff_hz = 1000.0f;
    base.osc[0].volume = 0.5f;
    std::vector<Nudge> ns{
        {"definitely.not.a.real.path", 99.0f},
        {"filter.cutoff_hz", 10.0f},
    };
    PatchStruct out = applyNudges(base, ns);
    CHECK(out.filter.cutoff_hz == 1100.0f);
    CHECK(out.osc[0].volume == 0.5f);  // untouched
}

TEST_CASE("applyNudges: empty nudge list returns base unchanged on whitelisted axes") {
    PatchStruct base = make_default_patch();
    base.filter.cutoff_hz = 1234.0f;
    base.amp_env.attack_s = 0.42f;
    base.reverb.mix = 0.33f;
    PatchStruct out = applyNudges(base, {});
    CHECK(out.filter.cutoff_hz == 1234.0f);
    CHECK(out.amp_env.attack_s == 0.42f);
    CHECK(out.reverb.mix == 0.33f);
}

// ── parseResponse: JSON envelope round-trip ──────────────────────────────────

TEST_CASE("DeltaNudger::parseResponse — bare inner JSON (test seam)") {
    const std::string raw =
        "{\"selected_index\":1,\"nudges\":["
        "{\"path\":\"filter.cutoff_hz\",\"delta_percent\":-20},"
        "{\"path\":\"reverb.mix\",\"delta_percent\":15}"
        "],\"rationale\":\"darker and wetter\"}";
    NudgeResult r = DeltaNudger::parseResponse(raw, 3);
    CHECK(r.selected_index == 1);
    REQUIRE(r.nudges.size() == 2);
    CHECK(r.nudges[0].path == "filter.cutoff_hz");
    CHECK(r.nudges[0].delta_percent == -20.0f);
    CHECK(r.nudges[1].path == "reverb.mix");
    CHECK(r.nudges[1].delta_percent == 15.0f);
    CHECK(r.rationale == "darker and wetter");
}

TEST_CASE("DeltaNudger::parseResponse — full Gemini envelope (escaped inner text)") {
    // Same JSON wrapped in candidates[0].content.parts[0].text the way
    // Gemini actually returns it (string-escaped).
    const std::string envelope =
        "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":"
        "\"{\\\"selected_index\\\":0,\\\"nudges\\\":[{\\\"path\\\":\\\"chorus.mix\\\",\\\"delta_percent\\\":40}],\\\"rationale\\\":\\\"lusher\\\"}\""
        "}]},\"finishReason\":\"STOP\"}]}";
    NudgeResult r = DeltaNudger::parseResponse(envelope, 3);
    CHECK(r.selected_index == 0);
    REQUIRE(r.nudges.size() == 1);
    CHECK(r.nudges[0].path == "chorus.mix");
    CHECK(r.nudges[0].delta_percent == 40.0f);
    CHECK(r.rationale == "lusher");
}

TEST_CASE("DeltaNudger::parseResponse — empty nudges array still success") {
    const std::string raw = "{\"selected_index\":2,\"nudges\":[],\"rationale\":\"\"}";
    NudgeResult r = DeltaNudger::parseResponse(raw, 3);
    CHECK(r.selected_index == 2);
    CHECK(r.nudges.empty());
}

TEST_CASE("DeltaNudger::parseResponse — malformed body returns selected_index=-1") {
    const std::string raw = "not json at all {{{";
    NudgeResult r = DeltaNudger::parseResponse(raw, 3);
    CHECK(r.selected_index == -1);
}

TEST_CASE("DeltaNudger::parseResponse — selected_index out of range returns -1") {
    const std::string raw = "{\"selected_index\":5,\"nudges\":[]}";
    NudgeResult r = DeltaNudger::parseResponse(raw, 3);
    CHECK(r.selected_index == -1);
}

TEST_CASE("DeltaNudger::parseResponse — caps applied nudges to max 4") {
    const std::string raw =
        "{\"selected_index\":0,\"nudges\":["
        "{\"path\":\"a\",\"delta_percent\":1},"
        "{\"path\":\"b\",\"delta_percent\":2},"
        "{\"path\":\"c\",\"delta_percent\":3},"
        "{\"path\":\"d\",\"delta_percent\":4},"
        "{\"path\":\"e\",\"delta_percent\":5},"
        "{\"path\":\"f\",\"delta_percent\":6}"
        "]}";
    NudgeResult r = DeltaNudger::parseResponse(raw, 3);
    CHECK(r.selected_index == 0);
    CHECK(r.nudges.size() == 4);
}

// ── End-to-end with mocked http_post ─────────────────────────────────────────

// Subclass exposing the http_post seam. Forces a canned response body so the
// retry / parse / applyNudges path can be exercised without network I/O.
class MockNudger : public DeltaNudger {
public:
    explicit MockNudger(DeltaNudgerConfig cfg, std::string canned) noexcept
        : DeltaNudger(std::move(cfg)), canned_(std::move(canned)) {}

protected:
    std::string http_post(const std::string& /*url*/, const std::string& /*body*/) const override {
        return canned_;
    }

private:
    std::string canned_;
};

TEST_CASE("DeltaNudger end-to-end: mocked Gemini success picks archetype 1 and applies nudges") {
    DeltaNudgerConfig cfg;
    cfg.api_key = "test-key";
    const std::string canned =
        "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":"
        "\"{\\\"selected_index\\\":1,\\\"nudges\\\":[{\\\"path\\\":\\\"filter.cutoff_hz\\\",\\\"delta_percent\\\":-20}],\\\"rationale\\\":\\\"darker\\\"}\""
        "}]},\"finishReason\":\"STOP\"}]}";
    MockNudger d(cfg, canned);

    // Use any 3 archetypes from the library.
    const auto& lib = ArchetypeLibrary::all();
    REQUIRE(lib.size() >= 3);
    std::vector<const Archetype*> top3 = {&lib[0], &lib[1], &lib[2]};

    NudgeRequest req{"darker pad", top3};
    NudgeResult r = d.nudge(req);

    CHECK(r.selected_index == 1);
    REQUIRE(r.nudges.size() == 1);
    CHECK(r.nudges[0].path == "filter.cutoff_hz");
    CHECK(r.nudges[0].delta_percent == -20.0f);

    // Patch should be lib[1]'s patch * 0.8 on cutoff (post-clamp).
    const float expected = lib[1].patch.filter.cutoff_hz * 0.8f;
    CHECK(std::abs(r.patch.filter.cutoff_hz - expected) < 1e-3f);
}

TEST_CASE("DeltaNudger end-to-end: mocked Gemini malformed JSON falls back to top-1") {
    DeltaNudgerConfig cfg;
    cfg.api_key = "test-key";
    MockNudger d(cfg, "garbage{{{");

    const auto& lib = ArchetypeLibrary::all();
    REQUIRE(lib.size() >= 3);
    std::vector<const Archetype*> top3 = {&lib[0], &lib[1], &lib[2]};

    NudgeRequest req{"darker pad", top3};
    NudgeResult r = d.nudge(req);
    CHECK(r.selected_index == -1);
    // Fallback carries top-1's patch through.
    CHECK(r.patch.filter.cutoff_hz == lib[0].patch.filter.cutoff_hz);
}

TEST_CASE("DeltaNudger end-to-end: empty nudges list leaves the archetype's patch verbatim") {
    DeltaNudgerConfig cfg;
    cfg.api_key = "test-key";
    const std::string canned =
        "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":"
        "\"{\\\"selected_index\\\":2,\\\"nudges\\\":[],\\\"rationale\\\":\\\"already matches\\\"}\""
        "}]},\"finishReason\":\"STOP\"}]}";
    MockNudger d(cfg, canned);

    const auto& lib = ArchetypeLibrary::all();
    REQUIRE(lib.size() >= 3);
    std::vector<const Archetype*> top3 = {&lib[0], &lib[1], &lib[2]};

    NudgeRequest req{"a perfect match", top3};
    NudgeResult r = d.nudge(req);

    CHECK(r.selected_index == 2);
    CHECK(r.nudges.empty());
    // applyNudges with no entries still runs the clamp pass; the archetype's
    // values are already in-range, so they round-trip bit-equal.
    CHECK(r.patch.filter.cutoff_hz == lib[2].patch.filter.cutoff_hz);
    CHECK(r.patch.amp_env.attack_s == lib[2].patch.amp_env.attack_s);
    CHECK(r.patch.reverb.mix == lib[2].patch.reverb.mix);
}

TEST_CASE("DeltaNudger::buildUserMessage includes prompt and archetype names + tags") {
    const auto& lib = ArchetypeLibrary::all();
    REQUIRE(lib.size() >= 3);
    std::vector<const Archetype*> top3 = {&lib[0], &lib[1], &lib[2]};
    NudgeRequest req{"dark cinematic pad", top3};
    const std::string msg = DeltaNudger::buildUserMessage(req);
    CHECK(msg.find("dark cinematic pad") != std::string::npos);
    CHECK(msg.find(lib[0].name) != std::string::npos);
    CHECK(msg.find(lib[1].name) != std::string::npos);
    CHECK(msg.find(lib[2].name) != std::string::npos);
}
