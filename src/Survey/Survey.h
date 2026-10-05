#pragma once
#include <string>
#include <vector>
#include "../HAL/ICamera.h"
#include "Pattern.h"

// Posição/orientação do aparelho no momento da foto (opcional, agnóstico de veículo).
struct Pose {
    double lat = 0, lon = 0, alt_m = 0;
    float yaw_deg = 0, pitch_deg = 0;
};

struct CapturedImage {
    std::string id;
    Pose pose;
    ImageFrame frame;
};

struct Detection {
    std::string pattern_id;
    float confidence;  // 0..1, estimada pelo classificador
};

// Fronteira com o modelo de visão (LLM leve). Implementações: mock, HTTP, local...
class IVisionClassifier {
public:
    virtual ~IVisionClassifier() = default;
    virtual std::vector<Detection> classify(const CapturedImage& img,
                                            const PatternCatalog& catalog) = 0;
};

// Fronteira com o envio web para a máquina do usuário.
class IUplink {
public:
    virtual ~IUplink() = default;
    virtual bool send(const std::string& json) = 0;
};

struct Flag {
    std::string pattern_id;
    std::vector<std::string> image_ids;  // imagens em que o padrão foi sinalizado
    float max_confidence = 0;
};

struct SurveyReport {
    std::string survey_id;
    int images_total = 0;
    std::vector<Flag> flags;
    std::vector<CapturedImage> flagged_images;  // somente as imagens sinalizadas
};

class SurveyPipeline {
    IVisionClassifier& classifier;
    const PatternCatalog& catalog;
    float threshold;

public:
    SurveyPipeline(IVisionClassifier& c, const PatternCatalog& cat, float minConfidence)
        : classifier(c), catalog(cat), threshold(minConfidence) {}

    SurveyReport run(const std::string& surveyId, const std::vector<CapturedImage>& images) const;
    static std::string toJson(const SurveyReport& r);
};
