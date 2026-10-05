#pragma once
#include <map>
#include "../Survey/Survey.h"

// Classificador determinístico para testes/simulação: devolve detecções pré-definidas por id de imagem.
class ScriptedClassifier : public IVisionClassifier {
    std::map<std::string, std::vector<Detection>> script;

public:
    void set(const std::string& imageId, std::vector<Detection> d) { script[imageId] = std::move(d); }
    Classification classify(const CapturedImage& img, const PatternCatalog&) override {
        Classification c;
        auto it = script.find(img.id);
        if (it != script.end()) c.detections = it->second;
        return c;
    }
};

// Envio "web" simulado: guarda o que seria enviado.
class MemoryUplink : public IUplink {
public:
    std::vector<std::string> sent;
    bool send(const std::string& json) override { sent.push_back(json); return true; }
};
