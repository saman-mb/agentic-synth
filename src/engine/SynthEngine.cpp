#include "engine/SynthEngine.h"

namespace agentic_synth::engine {

std::string SynthEngine::name() const { return "agentic-synth-engine-placeholder"; }

std::vector<float> SynthEngine::renderSilence(const std::size_t frameCount) const {
    return std::vector<float>(frameCount, 0.0F);
}

} // namespace agentic_synth::engine
