#pragma once
#include <map>
#include "../Survey/Survey.h"

// Classificador determinístico para testes/simulação: devolve detecções pré-definidas por id de imagem.
class ScriptedClassifier : public IVisionClassifier {
    std::map<std::string, std::vector<Detection>> script;

public:
    void set(const std::string& imageId, std::vector<Detection> d) { script[imageId] = std::move(d); }
    std::vector<Detection> classify(const CapturedImage& img, const PatternCatalog&) override {
        auto it = script.find(img.id);
        return it == script.end() ? std::vector<Detection>{} : it->second;
    }
};

// Envio "web" simulado: guarda o que seria enviado.
class MemoryUplink : public IUplink {
public:
    std::vector<std::string> sent;
    bool send(const std::string& json) override { sent.push_back(json); return true; }
};
