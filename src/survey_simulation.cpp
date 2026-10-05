#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include "Mocks/FileCamera.h"
#include "Mocks/ScriptedClassifier.h"
#include "Survey/ClaudeClassifier.h"
#include "Survey/CurlTransport.h"
#include "Survey/Survey.h"

int main() {
    // Catálogo configurável: trocar o texto troca o domínio, sem mexer no código.
    auto catalog = PatternCatalog::parse(
        "# id|label|description\n"
        "crack|Fissura|linhas finas escuras em superficie de alvenaria\n"
        "moisture|Umidade|manchas escuras difusas ou eflorescencia\n");

    const char* imagePath = "../teste.jpg";
    FileCamera camera(imagePath);
    if (!camera.init()) return 1;
    std::ifstream f(imagePath, std::ios::binary);
    std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // O aparelho fotografa a area de varios angulos.
    std::vector<CapturedImage> images;
    const float yaws[] = {0, 90, 180, 270};
    for (int i = 0; i < 4; i++) {
        CapturedImage img;
        img.id = "img_" + std::to_string(i + 1);
        img.pose.yaw_deg = yaws[i];
        img.pose.pitch_deg = -30;
        img.frame = camera.capture();
        img.encoded = jpeg;  // no hardware real, o JPEG sai direto da câmera
        images.push_back(img);
    }

    // Com ANTHROPIC_API_KEY usa a API real; sem ela, cai no classificador roteirizado.
    ScriptedClassifier scripted;
    scripted.set("img_2", {{"crack", 0.82f}});
    scripted.set("img_3", {{"crack", 0.64f}, {"moisture", 0.35f}});

    CurlTransport transport;
    ClaudeConfig cfg;
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (const char* m = std::getenv("CLASSIFIER_MODEL")) cfg.model = m;
    if (const char* fb = std::getenv("CLASSIFIER_FALLBACK")) cfg.serverSideFallback = std::string(fb) != "0";
    if (const char* u = std::getenv("CLASSIFIER_BASE_URL")) cfg.baseUrl = u;
    if (key) cfg.apiKey = key;
    ClaudeVisionClassifier claude(transport, cfg);

    IVisionClassifier& classifier = key ? static_cast<IVisionClassifier&>(claude) : scripted;
    std::cout << "[CLASSIFICADOR] " << (key ? "Claude (" + cfg.model + ")" : std::string("roteirizado (sem ANTHROPIC_API_KEY)")) << "\n";

    SurveyPipeline pipeline(classifier, catalog, 0.5f);
    SurveyReport report = pipeline.run("survey-demo", images);

    MemoryUplink uplink;
    uplink.send(SurveyPipeline::toJson(report));
    std::cout << uplink.sent.back() << "\n";
    return 0;
}
