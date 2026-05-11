#include "agent/KnobBridge.h"

#include "agent/ParamMap.h"
#include "mapper/descriptor_dataset.h"

namespace agentic_synth::agent {

void KnobBridge::handleKnobTweak(const std::string& param, float value) {
    // Copy current patch, apply the single-parameter delta, inject immediately.
    PatchStruct patch = pipeline_.currentPatch();
    mapper::apply_delta(patch, paramToDelta(param, value));
    pipeline_.injectPatch(patch);
    // Record so the session memory can bias future generations towards user tweaks.
    memory_.recordFeedback(FeedbackKind::Tweak, param, patch);
}

void KnobBridge::onMidiCC(int controller, int value) noexcept {
    // Track CC74 (brightness/filter cutoff) and CC71 (resonance) so the
    // system prompt can reflect the user's current timbral preference.
    switch (controller) {
    case 71:
        midiResonanceNorm_.store(static_cast<float>(value) / 127.0f, std::memory_order_relaxed);
        break;
    case 74:
        midiCutoffNorm_.store(static_cast<float>(value) / 127.0f, std::memory_order_relaxed);
        break;
    default:
        break;
    }
}

} // namespace agentic_synth::agent
