#pragma once

#include <string>

namespace agentic_synth::agent {

class AgentBridge {
public:
    [[nodiscard]] std::string status() const;
};

} // namespace agentic_synth::agent
