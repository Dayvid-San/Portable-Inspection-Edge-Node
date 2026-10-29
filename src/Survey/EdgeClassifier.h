#pragma once
#include <algorithm>
#include "../Core/EdgeProcessor.h"
#include "Survey.h"

// Linha de base SEM LLM: usa a densidade de bordas (Sobel) do frame como "confiança" do primeiro
// padrão do catálogo. Serve de piso de comparação e como possível pré-filtro barato no dispositivo.
// Observação: a densidade não é uma probabilidade calibrada; só faz sentido comparar por limiar/AUC.
class EdgeClassifier : public IVisionClassifier {
    EdgeProcessor processor;

public:
    Classification classify(const CapturedImage& img, const PatternCatalog& catalog) override {
        Classification c;
        if (!img.frame.valid || img.frame.data.empty()) {
            c.ok = false;
            c.error = "frame has no pixels";
            return c;
        }
        if (catalog.all().empty()) return c;
        float density = processor.analyze(img.frame).edge_density;
        c.detections.push_back({catalog.all().front().id, std::min(1.0f, density)});
        return c;
    }
};
