// Architect P1 #15: validate the WebUiComponent worker ThreadPool lifecycle.
//
// The `generate` native handler used to be backed by `juce::Thread::launch`,
// which captured `this` and `bridge_` without any cancellation tied to
// component teardown. If the editor closed mid-submitPrompt the worker would
// run on past the AudioProcessor's destruction and UAF the bridge.
//
// This test asserts the new contract:
//   1. Destroying a WebUiComponent with a long-running worker outstanding
//      does not crash and returns within a bounded time (the pool dtor /
//      removeAllJobs blocks with a 5 s ceiling and the cooperative
//      shouldExit() check unblocks the worker promptly).
//   2. Multiple concurrent worker jobs can be submitted and all complete
//      cleanly when the component outlives them.
//   3. Existing functionality (subscriber wiring, fallback message) is not
//      regressed — covered by neighbouring tests, sanity-pinged here too.

#include "agent/AgentBridge.h"
#include "ui/WebUiComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using agentic_synth::agent::AgentBridge;
using agentic_synth::ui::WebUiComponent;

namespace {
struct GuiFixture {
    juce::ScopedJuceInitialiser_GUI gui;
};
} // namespace

TEST_CASE("WebUiComponent dtor drains a long-running worker without UAF",
          "[WebUiComponent][WorkerPool][Lifecycle]") {
    GuiFixture fix;
    AgentBridge bridge;

    std::atomic<bool> workerEntered{false};
    std::atomic<bool> workerObservedCancel{false};
    std::atomic<bool> workerFinished{false};

    {
        auto component = std::make_unique<WebUiComponent>(bridge);

        // Submit a worker that mimics the `generate` handler's pattern:
        // spin in a cooperative loop checking shouldExit(). This stands in
        // for the LLM heuristic call: bounded but slow, and cancellation-
        // aware.
        component->submitWorkerForTesting([&]() {
            workerEntered.store(true);
            auto* self = juce::ThreadPoolJob::getCurrentThreadPoolJob();
            // Loop up to 30 s of 1 ms ticks — far longer than the test
            // tolerates. Cancellation MUST short-circuit this; if it does
            // not, the dtor's 5 s timeout fires and the assertion below
            // will catch the bug.
            for (int i = 0; i < 30'000; ++i) {
                if (self != nullptr && self->shouldExit()) {
                    workerObservedCancel.store(true);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            workerFinished.store(true);
        });

        // Give the pool a moment to actually pick the job up — otherwise
        // we might destroy before it even runs, which is a different
        // (also-OK) code path.
        for (int i = 0; i < 200 && !workerEntered.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(workerEntered.load());

        const auto destroyStart = std::chrono::steady_clock::now();
        component.reset();
        const auto destroyMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - destroyStart).count();

        // The dtor must NOT block for the worker's natural duration (30 s);
        // cancellation should kick in within tens of ms. Allow a generous
        // ceiling well under the 5 s removeAllJobs timeout to catch a
        // regression where the shouldExit() check is dropped.
        CHECK(destroyMs < 1000);
    }

    // Either the cooperative cancel fired, or the worker naturally
    // completed before we destroyed — both are sound. The bug we're
    // guarding against is "neither, plus a crash".
    CHECK((workerObservedCancel.load() || workerFinished.load()));
    SUCCEED("WebUiComponent dtor drained worker without crashing");
}

TEST_CASE("WebUiComponent worker pool runs multiple concurrent jobs cleanly",
          "[WebUiComponent][WorkerPool][Concurrency]") {
    GuiFixture fix;
    AgentBridge bridge;
    auto component = std::make_unique<WebUiComponent>(bridge);

    constexpr int kNumJobs = 6;
    std::atomic<int> completed{0};

    for (int i = 0; i < kNumJobs; ++i) {
        component->submitWorkerForTesting([&]() {
            // Small bit of work — sleeps so the pool actually has to
            // schedule rather than complete inline.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            completed.fetch_add(1, std::memory_order_acq_rel);
        });
    }

    // Wait up to 5 s for all jobs to finish. With a 2-thread pool and
    // 20 ms/job, kNumJobs=6 finishes in ~60 ms; the ceiling is generous
    // for CI variance.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (completed.load() < kNumJobs && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CHECK(completed.load() == kNumJobs);
    CHECK(component->pendingWorkerJobsForTesting() == 0);

    component.reset();
}

TEST_CASE("WebUiComponent worker pool does not regress subscriber wiring",
          "[WebUiComponent][WorkerPool][Regression]") {
    // Cheap sanity ping that the worker-pool refactor did not perturb the
    // existing subscriber-count contract pinned by WebUiComponentEmissionTest.
    GuiFixture fix;
    AgentBridge bridge;
    auto component = std::make_unique<WebUiComponent>(bridge);
    // 9 = 8 legacy + 1 enhancement (2-step LLM flow).
    CHECK(component->subscriberCountForTesting() == 9u);
    component.reset();
}
