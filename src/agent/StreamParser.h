#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "engine/PatchStruct.h"

namespace agentic_synth::agent {

// Incremental JSON token parser for streaming LLM patch responses.
// Fires callback each time a top-level field value is complete,
// providing first audible change well within 500 ms of stream start.
class StreamParser {
public:
    using PatchCallback = std::function<void(const PatchStruct&)>;

    void setCallback(PatchCallback cb) { callback_ = std::move(cb); }

    // Feed a streaming JSON chunk. May call callback_ zero or more times.
    void feedChunk(std::string_view chunk);

    // Reset state for a new stream; retains callback.
    void reset();

    [[nodiscard]] const PatchStruct& partialPatch() const noexcept { return partial_; }
    [[nodiscard]] bool isComplete() const noexcept { return done_; }

private:
    enum class State : uint8_t { Idle, TopLevel, InKey, AfterColon, InValue };

    void processChar(char c);
    void onFieldComplete();
    void applyField(const std::string& key, const std::string& value);

    State state_{State::Idle};
    int depth_{0};       // nesting depth inside the current field value
    bool inString_{false};
    bool escape_{false};

    std::string currentKey_;
    std::string currentValue_;

    PatchStruct partial_{make_default_patch()};
    PatchCallback callback_;
    bool done_{false};
};

} // namespace agentic_synth::agent
