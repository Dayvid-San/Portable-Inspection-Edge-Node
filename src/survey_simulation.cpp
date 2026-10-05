#include <iostream>
#include "Mocks/FileCamera.h"
#include "Mocks/ScriptedClassifier.h"
#include "Survey/Survey.h"

int main() {
    // Catálogo configurável: trocar o texto troca o domínio, sem mexer no código.
    auto catalog = PatternCatalog::parse(
        "# id|label|description\n"
        "crack|Fissura|linhas finas escuras em superficie de alvenaria\n"
        "moisture|Umidade|manchas escuras difusas ou eflorescencia\n");

    FileCamera camera("../teste.jpg");
    if (!camera.init()) return 1;

    // O aparelho fotografa a area de varios angulos.
    std::vector<CapturedImage> images;
    const float yaws[] = {0, 90, 180, 270};
    for (int i = 0; i < 4; i++) {
        CapturedImage img;
        img.id = "img_" + std::to_string(i + 1);
        img.pose.yaw_deg = yaws[i];
        img.pose.pitch_deg = -30;
        img.frame = camera.capture();
        images.push_back(img);
    }

    ScriptedClassifier classifier;  // substituir por classificador LLM real
    classifier.set("img_2", {{"crack", 0.82f}});
    classifier.set("img_3", {{"crack", 0.64f}, {"moisture", 0.35f}});

    SurveyPipeline pipeline(classifier, catalog, 0.5f);
    SurveyReport report = pipeline.run("survey-demo", images);

    MemoryUplink uplink;
    uplink.send(SurveyPipeline::toJson(report));
    std::cout << uplink.sent.back() << "\n";
    return 0;
}
