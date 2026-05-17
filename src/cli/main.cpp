// Phase G / #245 — agentic_synth_cli: headless patch generation.
//
// Reads a prompt from argv[1..N] (or stdin when no args), runs it through
// AgentBridge::generateLlmPatch, and prints the resulting PatchStruct as
// JSON to stdout. Exits 0 on success, 2 when the LLM call returns nullopt
// (no GEMINI_KEY, network failure, or invalid JSON), 1 on argument error.
//
// Intended uses: CI patch regression tests, batch generation, integration
// tests that need a deterministic patch-shape sanity check without
// spinning up the full plugin host. Honors GEMINI_KEY from the environment
// the same way the WebUI does (via mapper::loadEnvKey).

#include "agent/AgentBridge.h"
#include "engine/PatchStruct.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

std::string readAllStdin() {
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    return oss.str();
}

std::string joinArgv(int argc, char** argv, int from) {
    std::string out;
    for (int i = from; i < argc; ++i) {
        if (!out.empty())
            out += ' ';
        out += argv[i];
    }
    return out;
}

// Trim leading/trailing ASCII whitespace.
std::string trim(std::string s) {
    auto isWs = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!s.empty() && isWs(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && isWs(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

} // namespace

int main(int argc, char** argv) {
    // JUCE singletons require a manual scoped initializer in headless code so
    // juce::JSON serialization + MessageManager don't blow up on the first
    // dispatch.
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::string prompt;
    if (argc >= 2) {
        prompt = trim(joinArgv(argc, argv, 1));
    } else {
        prompt = trim(readAllStdin());
    }

    if (prompt.empty()) {
        std::cerr << "agentic_synth_cli: empty prompt — supply via argv or stdin\n";
        return 1;
    }

    agentic_synth::agent::AgentBridge bridge;

    // generateLlmPatch with patch_id=0, no refinement context.
    auto patch = bridge.generateLlmPatch(prompt, /*patch_id=*/0, std::nullopt, std::nullopt);
    if (!patch) {
        std::cerr << "agentic_synth_cli: LLM patch generation returned no result "
                     "(check GEMINI_KEY and network)\n";
        return 2;
    }

    auto var = agentic_synth::agent::AgentBridge::patchToVar(*patch);
    const auto json = juce::JSON::toString(var, /*allOnOneLine=*/false);
    std::cout << json.toStdString() << "\n";
    return 0;
}
