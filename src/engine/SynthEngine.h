#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace agentic_synth::engine {

class SynthEngine {
public:
    [[nodiscard]] std::string name() const;
    [[nodiscard]] std::vector<float> renderSilence(std::size_t frameCount) const;
};

} // namespace agentic_synth::engine
