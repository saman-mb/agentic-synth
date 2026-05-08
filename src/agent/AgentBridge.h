#pragma once

#include <string>

#include "agent/SessionMemory.h"
#include "engine/PatchStruct.h"

namespace agentic_synth::agent {

class AgentBridge {
public:
    [[nodiscard]] std::string status() const;

    // Record user feedback for a patch that was generated from prompt.
    void recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch);

    // Build a system prompt context string that includes recent session memory.
    [[nodiscard]] std::string buildSystemPrompt(const std::string& userPrompt) const;

    // Return per-dimension parameter bias hints based on session history.
    [[nodiscard]] PatchVector getParameterBias(const std::string& userPrompt) const;

    [[nodiscard]] const SessionMemory& sessionMemory() const noexcept { return memory_; }

private:
    SessionMemory memory_;
};

} // namespace agentic_synth::agent
