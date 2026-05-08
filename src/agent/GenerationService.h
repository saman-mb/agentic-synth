#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace agentic_synth::agent {

// gRPC service: GeneratePatch and RefinePatch endpoints.
// Uses a simple JSON-over-TCP protocol (lightweight alternative to full gRPC).

class GenerationService {
public:
    GenerationService();
    ~GenerationService();

    // Start the service on the given port
    bool start(int port = 50051);

    // Stop the service
    void stop();

    // Check if running
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

    // Request types
    struct GenerateRequest {
        std::string descriptor; // NL description
        int numVariations = 1;
        bool useHeuristicFirst = true;
    };

    struct RefineRequest {
        std::string descriptor; // Refinement description
        float currentFilterCutoffHz;
        float currentFilterResonance;
        float currentAmpAttackMs;
    };

    struct Response {
        bool success = false;
        std::string patchJson;
        std::string errorMessage;
        float latencyMs = 0;
    };

    // Process requests (blocking calls)
    Response generate(const GenerateRequest& req);
    Response refine(const RefineRequest& req);

private:
    std::atomic<bool> running_{false};
    int port_ = 50051;
    void* serverThread_ = nullptr; // pthread handle

    static void* serverLoop(void* arg);
};

} // namespace agentic_synth::agent
