#pragma once
#include <string>
#include <vector>
#include "Survey.h"

// Transporte HTTP injetável: permite testar sem rede e trocar a implementação no dispositivo.
struct HttpResponse {
    int status = 0;  // 0 = falha de transporte
    std::string body;
    std::string error;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse post(const std::string& url, const std::vector<std::string>& headers,
                              const std::string& body) = 0;
};

struct ClaudeConfig {
    std::string apiKey;
    std::string model = "claude-opus-5-5";
    std::string effort = "low";  // profundidade de raciocínio; classificação simples não precisa de mais
    std::string baseUrl = "https://api.anthropic.com";
    int maxTokens = 4096;        // o raciocínio adaptativo também conta aqui
    bool serverSideFallback = true;  // reroteia recusas dos classificadores de segurança
};

// Classifica imagens contra um catálogo de padrões chamando a Messages API (visão + saída estruturada).
class ClaudeVisionClassifier : public IVisionClassifier {
    IHttpTransport& http;
    ClaudeConfig cfg;

public:
    ClaudeVisionClassifier(IHttpTransport& t, ClaudeConfig c) : http(t), cfg(std::move(c)) {}

    Classification classify(const CapturedImage& img, const PatternCatalog& catalog) override;

    // Expostos para teste.
    std::string buildRequest(const CapturedImage& img, const PatternCatalog& catalog) const;
    static Classification parseResponse(const HttpResponse& r, const PatternCatalog& catalog);
};
