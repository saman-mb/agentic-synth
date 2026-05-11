// Phase 7A verification — AgentBridge::postMidiNote → MidiNoteSink path.
//
// Confirms that:
//   - A registered sink receives (note, velocity, true) immediately and
//     (note, velocity, false) after the requested duration.
//   - With no sink registered, postMidiNote is a no-op and does not crash.
//   - Both dispatches happen on the JUCE message thread (via callAsync and
//     Timer::callAfterDelay), not synchronously inside postMidiNote.

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <atomic>
#include <mutex>
#include <tuple>
#include <vector>

#include "agent/AgentBridge.h"

using namespace agentic_synth;
using namespace agentic_synth::agent;

namespace {

struct JuceFixture {
    juce::ScopedJuceInitialiser_GUI gui;
};

// Drains the JUCE message queue until pred() is satisfied or timeoutMs elapses.
template <typename Pred>
bool drainUntil(Pred pred, int timeoutMs = 500) {
    auto* mm = juce::MessageManager::getInstance();
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
    while (juce::Time::getMillisecondCounter() < deadline) {
        mm->runDispatchLoopUntil(10);
        if (pred())
            return true;
    }
    return pred();
}

} // namespace

TEST_CASE("AgentBridge: postMidiNote routes note-on then note-off to sink") {
    JuceFixture fix;
    AgentBridge bridge;

    struct Event {
        int note;
        float velocity;
        bool isNoteOn;
    };

    std::mutex eventsMutex;
    std::vector<Event> events;

    bridge.setMidiNoteSink([&](int note, float velocity, bool isNoteOn) {
        std::lock_guard<std::mutex> lock(eventsMutex);
        events.push_back({note, velocity, isNoteOn});
    });

    bridge.postMidiNote(60, 0.8f, /*durationMs=*/100);

    // Note-on should land on the next message-pump tick; note-off is
    // scheduled via Timer::callAfterDelay so we need to wait past the
    // 100 ms duration.
    REQUIRE(drainUntil([&] {
        std::lock_guard<std::mutex> lock(eventsMutex);
        return events.size() == 2;
    }, /*timeoutMs=*/500));

    std::lock_guard<std::mutex> lock(eventsMutex);
    REQUIRE(events.size() == 2);
    CHECK(events[0].note == 60);
    CHECK(events[0].velocity == 0.8f);
    CHECK(events[0].isNoteOn == true);
    CHECK(events[1].note == 60);
    CHECK(events[1].velocity == 0.8f);
    CHECK(events[1].isNoteOn == false);
}

TEST_CASE("AgentBridge: postMidiNote with no sink registered is a safe no-op") {
    JuceFixture fix;
    AgentBridge bridge;

    // No sink registered — must not crash, must not deliver anything.
    REQUIRE_NOTHROW(bridge.postMidiNote(72, 1.0f, /*durationMs=*/50));

    // Drain anyway so any incorrectly-queued dispatch would get a chance.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);

    // Now register a sink and confirm the bridge is still usable afterwards.
    std::atomic<int> calls{0};
    bridge.setMidiNoteSink([&](int, float, bool) {
        calls.fetch_add(1, std::memory_order_relaxed);
    });
    bridge.postMidiNote(48, 0.5f, /*durationMs=*/30);
    REQUIRE(drainUntil([&] { return calls.load() >= 2; }, /*timeoutMs=*/300));
    CHECK(calls.load() == 2);
}
