#include "GenerationService.h"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace agentic_synth::agent {

GenerationService::GenerationService() = default;
GenerationService::~GenerationService() { stop(); }

bool GenerationService::start(int port) {
    port_ = port;
    running_ = true;

    std::thread([this]() {
        int serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0) {
            running_ = false;
            return;
        }

        int opt = 1;
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(serverFd);
            running_ = false;
            return;
        }

        listen(serverFd, 5);

        while (running_) {
            struct pollfd pfd{};
            pfd.fd = serverFd;
            pfd.events = POLLIN;

            int ret = poll(&pfd, 1, 500); // 500ms timeout
            if (ret < 0 || !running_)
                break;
            if (ret == 0)
                continue;

            struct sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientFd < 0)
                continue;

            // Read request
            char buf[4096]{};
            ssize_t n = read(clientFd, buf, sizeof(buf) - 1);
            if (n > 0) {
                // Parse JSON request, dispatch, respond
                std::string request(buf, n);
                std::string response = R"({"status":"ok","message":"Received"})";
                write(clientFd, response.data(), response.size());
            }
            close(clientFd);
        }

        close(serverFd);
    }).detach();

    return true;
}

void GenerationService::stop() { running_ = false; }

GenerationService::Response GenerationService::generate(const GenerateRequest& req) {
    // TODO: delegate to HeuristicParser or LLM
    Response resp;
    resp.success = true;
    resp.patchJson = R"({"oscillatorMix":[1,0,0,0,0],"filterCutoffHz":500})";
    return resp;
}

GenerationService::Response GenerationService::refine(const RefineRequest& req) {
    Response resp;
    resp.success = true;
    resp.patchJson = R"({"filterCutoffHz":)" + std::to_string(req.currentFilterCutoffHz * 0.5f) + R"(})";
    return resp;
}

} // namespace agentic_synth::agent
