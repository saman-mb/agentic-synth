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
    running_.store(true, std::memory_order_relaxed);

    std::thread([this]() {
        int serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0) {
            running_.store(false, std::memory_order_relaxed);
            return;
        }

        int opt = 1;
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(serverFd);
            running_.store(false, std::memory_order_relaxed);
            return;
        }

        listen(serverFd, 5);

        while (running_.load(std::memory_order_relaxed)) {
            struct pollfd pfd {};
            pfd.fd = serverFd;
            pfd.events = POLLIN;

            int ret = poll(&pfd, 1, 500); // 500ms timeout
            if (ret < 0 || !running_.load(std::memory_order_relaxed))
                break;
            if (ret == 0)
                continue;

            struct sockaddr_in clientAddr {};
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientFd < 0)
                continue;

            // Read request
            char buf[65536]{};
            ssize_t n = read(clientFd, buf, sizeof(buf) - 1);
            if (n > 0) {
                std::string reqStr(buf, static_cast<size_t>(n));

                // Extract "descriptor" field from JSON request.
                auto extractStr = [](const std::string& json, const std::string& key) -> std::string {
                    const std::string needle = "\"" + key + "\"";
                    auto pos = json.find(needle);
                    if (pos == std::string::npos)
                        return {};
                    pos = json.find(':', pos + needle.size());
                    if (pos == std::string::npos)
                        return {};
                    pos = json.find('"', pos + 1);
                    if (pos == std::string::npos)
                        return {};
                    const auto end = json.find('"', pos + 1);
                    if (end == std::string::npos)
                        return {};
                    return json.substr(pos + 1, end - pos - 1);
                };

                GenerateRequest req;
                req.descriptor = extractStr(reqStr, "descriptor");
                if (req.descriptor.empty())
                    req.descriptor = "default patch";

                Response resp = generate(req);
                std::string response;
                if (resp.success)
                    response = "{\"success\":true,\"patchJson\":" + resp.patchJson + "}";
                else
                    response = "{\"success\":false,\"error\":\"" + resp.errorMessage + "\"}";
                write(clientFd, response.data(), response.size());
            }
            close(clientFd);
        }

        close(serverFd);
    }).detach();

    return true;
}

void GenerationService::stop() { running_.store(false, std::memory_order_relaxed); }

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
