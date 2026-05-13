#pragma once

#include <string>

namespace agentic_synth::mapper {

// Look up an environment variable; if absent, walk the current working
// directory up to two parent levels looking for a `.env` file and parse
// it for a `KEY=value` line. Strips surrounding quotes/whitespace. Returns
// an empty string when nothing matches.
//
// Used to read GEMINI_KEY at AgentBridge construction so the cloud fallback
// is available even when the plugin is launched from a host that doesn't
// inherit the shell environment.
[[nodiscard]] std::string loadEnvKey(const std::string& key);

} // namespace agentic_synth::mapper
