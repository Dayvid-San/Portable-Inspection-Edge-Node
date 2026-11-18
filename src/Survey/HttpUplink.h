#pragma once
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include "ClaudeClassifier.h"  // IHttpTransport

// Envia o relatório (JSON) para a máquina do usuário via HTTP POST com Bearer token.
// Repete em falha de rede, 408, 429 e 5xx com backoff exponencial; os demais 4xx não são repetidos.
class HttpUplink : public IUplink {
    IHttpTransport& http;
    std::string url, token;
    bool permanent = false;

public:
    int maxAttempts = 4;
    int baseDelayMs = 1000;
    std::function<void(int)> sleepMs = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };

    HttpUplink(IHttpTransport& t, std::string endpoint, std::string bearerToken)
        : http(t), url(std::move(endpoint)), token(std::move(bearerToken)) {}

    bool lastFailureWasPermanent() const override { return permanent; }

    bool send(const std::string& json) override {
        permanent = false;
        const std::vector<std::string> headers = {"content-type: application/json",
                                                  "authorization: Bearer " + token};
        for (int attempt = 1; attempt <= maxAttempts; attempt++) {
            HttpResponse r = http.post(url, headers, json);
            if (r.status >= 200 && r.status < 300) return true;

            bool retryable = r.status == 0 || r.status == 408 || r.status == 429 || r.status >= 500;
            std::cerr << "[UPLINK] tentativa " << attempt << "/" << maxAttempts << " falhou (status " << r.status
                      << (r.error.empty() ? "" : ", " + r.error) << ")\n";
            if (!retryable) {
                // 4xx de conteudo (400, 413, 422...) nunca vai passar; 401/403 pedem correcao humana de credencial.
                permanent = r.status >= 400 && r.status < 500 && r.status != 401 && r.status != 403;
                return false;
            }
            if (attempt == maxAttempts) return false;
            sleepMs(baseDelayMs << (attempt - 1));
        }
        return false;
    }
};
