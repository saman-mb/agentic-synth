#include "agent/AgentBridge.h"
#include "engine/SynthEngine.h"

#include <iostream>

int main() {
    const agentic_synth::engine::SynthEngine engine;
    const agentic_synth::agent::AgentBridge bridge;

    std::cout << engine.name() << "\n";
    std::cout << bridge.status() << "\n";

    return 0;
}
