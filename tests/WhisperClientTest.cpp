#include "agent/WhisperClient.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

using namespace agentic_synth::agent;

TEST_CASE("WhisperClient: default state is not initialized", "[whisper]") {
    WhisperClient client;
    REQUIRE_FALSE(client.isInitialized());
    REQUIRE_FALSE(client.isStreaming());
}

TEST_CASE("WhisperClient: init with invalid path returns false", "[whisper]") {
    WhisperClient client;
    bool ok = client.init("/nonexistent/model.bin");
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(client.isInitialized());
}

TEST_CASE("WhisperClient: transcribe while uninitialized returns empty string", "[whisper]") {
    WhisperClient client;
    int16_t samples[16] = {};
    std::string result = client.transcribe(samples, 16, 16000);
    REQUIRE(result.empty());
}

TEST_CASE("WhisperClient: transcribeFile with non-existent path returns empty string", "[whisper]") {
    WhisperClient client;
    std::string result = client.transcribeFile("/nonexistent/audio.wav");
    REQUIRE(result.empty());
}

TEST_CASE("WhisperClient: streaming not active after construction", "[whisper]") {
    WhisperClient client;
    REQUIRE_FALSE(client.isStreaming());
    // stopStreaming on idle client does not crash
    client.stopStreaming();
    REQUIRE_FALSE(client.isStreaming());
}
