// Phase 10C verification — DictionaryService extracted from AgentBridge.
//
// Confirms that:
//   * getDictionaryJson() returns a well-formed dictionary_data frame and
//     contains the expected entries array.
//   * saveDictionary() round-trips: parsing a save_dictionary frame stores
//     the entry on the underlying SemanticMapper, so a follow-up
//     getDictionaryJson() reflects it.
//
// Uses a real SemanticMapper with an offline (no embedding server) config to
// avoid any network dependency. saveDictionary writes to
// descriptor_dataset_custom.json (DictionaryService's hard-coded path); the
// test removes that file before and after to keep the suite hermetic.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "agent/DictionaryService.h"
#include "mapper/SemanticMapper.h"

using agentic_synth::agent::DictionaryService;
using agentic_synth::mapper::SemanticMapper;
using agentic_synth::mapper::SemanticMapperConfig;

namespace {

SemanticMapperConfig offline_cfg() {
    SemanticMapperConfig cfg;
    cfg.server_url = ""; // word-overlap fallback; never touches the network
    return cfg;
}

// Sentinel path written by DictionaryService::saveDictionary. Tests scrub it
// on entry + exit so concurrent runs / repeated invocations stay clean.
constexpr const char* kCustomPath = "descriptor_dataset_custom.json";

struct ScopedCleanup {
    ScopedCleanup() { std::filesystem::remove(kCustomPath); }
    ~ScopedCleanup() { std::filesystem::remove(kCustomPath); }
};

} // namespace

TEST_CASE("DictionaryService: getDictionaryJson returns a dictionary_data frame") {
    ScopedCleanup cleanup;
    SemanticMapper mapper{offline_cfg()};
    DictionaryService svc{mapper};

    const std::string json = svc.getDictionaryJson();

    // Framing: must start with {"type":"dictionary_data","entries":[ ... ]}.
    REQUIRE(json.find("\"type\":\"dictionary_data\"") != std::string::npos);
    REQUIRE(json.find("\"entries\":") != std::string::npos);
    // entries is an array — dumpAllToJson always emits at least `[]`.
    REQUIRE(json.find("\"entries\":[") != std::string::npos);
}

TEST_CASE("DictionaryService: save + get round-trips a custom entry") {
    ScopedCleanup cleanup;
    SemanticMapper mapper{offline_cfg()};
    DictionaryService svc{mapper};

    // Frame shape matches what the WebView sends on save_dictionary. Only the
    // "entries" array is required; SemanticMapper::parseAndSaveCustomEntries
    // ignores the surrounding fields.
    const std::string saveFrame =
        R"({"type":"save_dictionary","entries":[
            {"keyword":"sparklybell","context":"lead",
             "delta":{"filter":{"cutoff_hz":4200.0}}}
           ]})";

    svc.saveDictionary(saveFrame);

    // The custom entry must be visible to the mapper and surface in the next
    // getDictionaryJson() dump.
    REQUIRE(mapper.customEntries().size() == 1);
    CHECK(mapper.customEntries().front().keyword == "sparklybell");

    const std::string dumped = svc.getDictionaryJson();
    CHECK(dumped.find("sparklybell") != std::string::npos);
}
