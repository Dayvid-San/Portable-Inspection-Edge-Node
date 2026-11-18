#pragma once
#include <string>
#include <vector>
#include "../HAL/ICamera.h"
#include "Pattern.h"

// Posição/orientação do aparelho no momento da foto (opcional, agnóstico de veículo).
struct Pose {
    bool has_position = false;  // false: lat/lon nao sao validos (sem GPS)
    double lat = 0, lon = 0, alt_m = 0;
    float yaw_deg = 0, pitch_deg = 0;
};

struct CapturedImage {
    std::string id;
    Pose pose;
    ImageFrame frame;                // pixels crus (processamento local)
    std::vector<uint8_t> encoded;    // bytes JPEG/PNG/WebP, usados pelo classificador remoto
    std::string mime = "image/jpeg";
};

struct Detection {
    std::string pattern_id;
    float confidence;  // 0..1, estimada pelo classificador
};

// Resultado de classificar UMA imagem. Falha nunca pode ser confundida com "nada encontrado".
struct Classification {
    bool ok = true;
    std::string error;
    std::vector<Detection> detections;
};

// Fronteira com o modelo de visão (LLM leve). Implementações: mock, HTTP, local...
class IVisionClassifier {
public:
    virtual ~IVisionClassifier() = default;
    virtual Classification classify(const CapturedImage& img, const PatternCatalog& catalog) = 0;
};

// Fronteira com o envio web para a máquina do usuário.
class IUplink {
public:
    virtual ~IUplink() = default;
    virtual bool send(const std::string& json) = 0;
    // true se a ULTIMA falha nao adianta repetir (ex.: o servidor recusou o conteudo). Falhas de rede,
    // 5xx, 429 e erros de credencial (401/403, que um humano corrige) NAO sao permanentes.
    virtual bool lastFailureWasPermanent() const { return false; }
};

struct Flag {
    std::string pattern_id;
    std::vector<std::string> image_ids;  // imagens em que o padrão foi sinalizado
    float max_confidence = 0;
    std::vector<float> confidences;  // paralelo a image_ids
};

// Imagens sinalizadas para o mesmo padrao e proximas entre si (mesma regiao fisica).
struct Zone {
    std::string pattern_id;
    double lat = 0, lon = 0;  // centroide
    std::vector<std::string> image_ids;
    float max_confidence = 0;
};

struct SurveyReport {
    std::string survey_id;
    int images_total = 0;
    std::vector<std::string> failed_image_ids;  // classificação falhou: imagem NÃO foi avaliada
    std::vector<Flag> flags;
    std::vector<Zone> zones;                    // so imagens com posicao; vazio sem GPS
    std::vector<CapturedImage> flagged_images;  // somente as imagens sinalizadas
};

class SurveyPipeline {
    IVisionClassifier& classifier;
    const PatternCatalog& catalog;
    float threshold;

public:
    double zoneRadiusM = 25;  // imagens do mesmo padrao a ate esta distancia viram uma zona

    SurveyPipeline(IVisionClassifier& c, const PatternCatalog& cat, float minConfidence)
        : classifier(c), catalog(cat), threshold(minConfidence) {}

    SurveyReport run(const std::string& surveyId, const std::vector<CapturedImage>& images) const;
    // includeImages: anexa (base64) apenas as imagens sinalizadas.
    static std::string toJson(const SurveyReport& r, bool includeImages = false);
    static std::vector<Zone> groupZones(const SurveyReport& r, double radiusM);
};
