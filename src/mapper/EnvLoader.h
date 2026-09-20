#pragma once

#include <string>

namespace agentic_synth::mapper {

// Look up an environment variable; if absent, search for a `.env` file
// (KEY=value, quotes/whitespace stripped) by walking:
//   1. cwd + up to 3 parents (dev launches from the repo)
//   2. the executable directory + parents (and macOS .app Contents/Resources)
// Returns empty when nothing matches.
//
// Used to read GEMINI_KEY at AgentBridge construction so the cloud fallback
// is available when Finder/`open`/DAW hosts launch with cwd=/ and no shell env.
[[nodiscard]] std::string loadEnvKey(const std::string& key);

} // namespace agentic_synth::mapper
