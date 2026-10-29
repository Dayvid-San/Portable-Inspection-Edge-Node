#include <gtest/gtest.h>
#include <algorithm>
#include "../src/Mocks/ScriptedClassifier.h"
#include "../src/Survey/Survey.h"

namespace {
CapturedImage img(const std::string& id) { CapturedImage c; c.id = id; return c; }
}  // namespace

TEST(Survey, ParsesCatalogIgnoringCommentsAndBadLines) {
    auto cat = PatternCatalog::parse("# c\na|A|desc a\nlinha invalida\nb|B|desc b\n");
    ASSERT_EQ(cat.all().size(), 2u);
    EXPECT_EQ(cat.all()[1].id, "b");
}

TEST(Survey, FlagsOnlyImagesAboveThreshold) {
    auto cat = PatternCatalog::parse("x|X|d\n");
    ScriptedClassifier cls;
    cls.set("i1", {{"x", 0.9f}});
    cls.set("i2", {{"x", 0.3f}});
    SurveyPipeline p(cls, cat, 0.5f);
    auto rep = p.run("s", {img("i1"), img("i2"), img("i3")});
    EXPECT_EQ(rep.images_total, 3);
    ASSERT_EQ(rep.flags.size(), 1u);
    EXPECT_EQ(rep.flags[0].image_ids, std::vector<std::string>{"i1"});
    EXPECT_EQ(rep.flagged_images.size(), 1u);
}

TEST(Survey, GroupsSamePatternAcrossImages) {
    auto cat = PatternCatalog::parse("x|X|d\n");
    ScriptedClassifier cls;
    cls.set("i1", {{"x", 0.6f}});
    cls.set("i2", {{"x", 0.8f}});
    SurveyPipeline p(cls, cat, 0.5f);
    auto rep = p.run("s", {img("i1"), img("i2")});
    ASSERT_EQ(rep.flags.size(), 1u);
    EXPECT_EQ(rep.flags[0].image_ids.size(), 2u);
    EXPECT_FLOAT_EQ(rep.flags[0].max_confidence, 0.8f);
}

TEST(Survey, JsonContainsFlagsAndEscapes) {
    SurveyReport r;
    r.survey_id = "a\"b";
    r.flags.push_back({"x", {"i1"}, 0.9f});
    auto j = SurveyPipeline::toJson(r);
    EXPECT_NE(j.find("a\\\"b"), std::string::npos);
    EXPECT_NE(j.find("\"pattern\": \"x\""), std::string::npos);
}

// ---- Classificador Claude (transporte falso, sem rede) ----
#include "../src/Survey/ClaudeClassifier.h"
#include "../src/Survey/MiniJson.h"

namespace {
struct FakeTransport : IHttpTransport {
    HttpResponse reply;
    std::string lastUrl, lastBody;
    std::vector<std::string> lastHeaders;
    HttpResponse post(const std::string& url, const std::vector<std::string>& h, const std::string& b) override {
        lastUrl = url; lastHeaders = h; lastBody = b;
        return reply;
    }
};

// Resposta da API: bloco "thinking" vazio antes do texto, como no Opus 5.5.
HttpResponse apiReply(const std::string& innerJson, const std::string& stop = "end_turn") {
    HttpResponse r;
    r.status = 200;
    r.body = "{\"stop_reason\":\"" + stop + "\",\"content\":[{\"type\":\"thinking\",\"thinking\":\"\"},"
             "{\"type\":\"text\",\"text\":\"" + minijson::escape(innerJson) + "\"}]}";
    return r;
}

CapturedImage jpegImg(const std::string& id) {
    CapturedImage c; c.id = id; c.encoded = {'M', 'a', 'n'}; return c;
}
}  // namespace

TEST(MiniJson, ParsesNestedAndEscapes) {
    auto v = minijson::parse("{\"a\":[1,2.5,{\"b\":\"x\\\"y\\u00e9\"}],\"c\":null,\"d\":true}");
    EXPECT_EQ(v.get("a")->a.size(), 3u);
    EXPECT_EQ(v.get("a")->a[2].get("b")->s, "x\"y\xc3\xa9");
    EXPECT_TRUE(v.get("d")->b);
    EXPECT_THROW(minijson::parse("{\"a\":"), std::runtime_error);
    EXPECT_THROW(minijson::parse("{} x"), std::runtime_error);
}

TEST(MiniJson, Base64MatchesKnownVectors) {
    EXPECT_EQ(base64Encode({'M', 'a', 'n'}), "TWFu");
    EXPECT_EQ(base64Encode({'M', 'a'}), "TWE=");
    EXPECT_EQ(base64Encode({'M'}), "TQ==");
}

TEST(ClaudeClassifier, BuildsVisionRequestWithSchemaFromCatalog) {
    FakeTransport t;
    t.reply = apiReply("{\"detections\":[]}");
    ClaudeConfig cfg; cfg.apiKey = "sk-test";
    ClaudeVisionClassifier cls(t, cfg);
    auto cat = PatternCatalog::parse("crack|Fissura|linhas \"finas\"\nmoisture|Umidade|manchas\n");
    cls.classify(jpegImg("i1"), cat);

    EXPECT_EQ(t.lastUrl, "https://api.anthropic.com/v1/messages");
    EXPECT_NE(std::find(t.lastHeaders.begin(), t.lastHeaders.end(), "x-api-key: sk-test"), t.lastHeaders.end());
    auto req = minijson::parse(t.lastBody);  // o corpo precisa ser JSON válido mesmo com aspas no catálogo
    EXPECT_EQ(req.get("model")->s, "claude-opus-5-5");
    EXPECT_EQ(req.get("output_config")->get("effort")->s, "low");
    const auto& content = req.get("messages")->a[0].get("content")->a;
    EXPECT_EQ(content[0].get("type")->s, "image");  // imagem antes do texto
    EXPECT_EQ(content[0].get("source")->get("data")->s, "TWFu");
    const auto& en = req.get("output_config")->get("format")->get("schema")->get("properties")
                         ->get("detections")->get("items")->get("properties")->get("pattern_id")->get("enum")->a;
    ASSERT_EQ(en.size(), 2u);
    EXPECT_EQ(en[1].s, "moisture");
}

TEST(ClaudeClassifier, ParsesDetectionsSkippingThinkingBlock) {
    FakeTransport t;
    t.reply = apiReply("{\"detections\":[{\"pattern_id\":\"x\",\"confidence\":0.8},"
                       "{\"pattern_id\":\"fora_do_catalogo\",\"confidence\":0.9},"
                       "{\"pattern_id\":\"x\",\"confidence\":1.7}]}");
    ClaudeVisionClassifier cls(t, ClaudeConfig{});
    auto r = cls.classify(jpegImg("i1"), PatternCatalog::parse("x|X|d\n"));
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.detections.size(), 2u);  // id desconhecido descartado
    EXPECT_FLOAT_EQ(r.detections[1].confidence, 1.0f);  // clamp
}

TEST(ClaudeClassifier, FailuresAreNotSilentNegatives) {
    auto cat = PatternCatalog::parse("x|X|d\n");
    FakeTransport t;
    ClaudeVisionClassifier cls(t, ClaudeConfig{});

    t.reply.status = 429; t.reply.body = "rate limited";
    EXPECT_FALSE(cls.classify(jpegImg("i"), cat).ok);
    t.reply = apiReply("{\"detections\":[]}", "refusal");
    EXPECT_FALSE(cls.classify(jpegImg("i"), cat).ok);
    t.reply = apiReply("não é json");
    EXPECT_FALSE(cls.classify(jpegImg("i"), cat).ok);
    t.reply = HttpResponse{}; t.reply.error = "timeout";
    EXPECT_FALSE(cls.classify(jpegImg("i"), cat).ok);
    EXPECT_FALSE(cls.classify(CapturedImage{}, cat).ok);  // sem bytes codificados
}

TEST(Survey, ReportsFailedImagesInsteadOfDroppingThem) {
    FakeTransport t;
    t.reply.status = 500;
    ClaudeVisionClassifier cls(t, ClaudeConfig{});
    auto cat = PatternCatalog::parse("x|X|d\n");
    SurveyPipeline p(cls, cat, 0.5f);
    auto rep = p.run("s", {jpegImg("i1")});
    EXPECT_EQ(rep.failed_image_ids, std::vector<std::string>{"i1"});
    EXPECT_NE(SurveyPipeline::toJson(rep).find("\"images_failed\": [\"i1\"]"), std::string::npos);
}

// ---- Metricas e linha de base ----
#include "../src/Survey/EdgeClassifier.h"
#include "../src/Survey/Metrics.h"

TEST(Metrics, ConfusionPrecisionRecallF1) {
    std::vector<Sample> s = {{0.9, true}, {0.8, true}, {0.7, false}, {0.2, true}, {0.1, false}};
    Confusion c = metrics::confusionAt(s, 0.5);
    EXPECT_EQ(c.tp, 2); EXPECT_EQ(c.fp, 1); EXPECT_EQ(c.fn, 1); EXPECT_EQ(c.tn, 1);
    EXPECT_NEAR(c.precision(), 2.0 / 3, 1e-9);
    EXPECT_NEAR(c.recall(), 2.0 / 3, 1e-9);
    EXPECT_NEAR(c.f1(), 2.0 / 3, 1e-9);
}

TEST(Metrics, AucPerfectRandomAndTies) {
    EXPECT_DOUBLE_EQ(metrics::auc({{0.9, true}, {0.1, false}}), 1.0);
    EXPECT_DOUBLE_EQ(metrics::auc({{0.1, true}, {0.9, false}}), 0.0);
    EXPECT_DOUBLE_EQ(metrics::auc({{0.5, true}, {0.5, false}}), 0.5);
    EXPECT_DOUBLE_EQ(metrics::auc({{0.9, true}}), 0.0);  // sem negativos: indefinido -> 0
}

TEST(Metrics, BestF1ThresholdSeparatesClasses) {
    std::vector<Sample> s = {{0.9, true}, {0.8, true}, {0.3, false}, {0.2, false}};
    double t = metrics::bestF1Threshold(s);
    EXPECT_GT(t, 0.3);
    EXPECT_LE(t, 0.8);
}

namespace {
CapturedImage grayImg(int w, int h, uint8_t v, bool vline) {
    CapturedImage c;
    c.frame.width = w; c.frame.height = h; c.frame.valid = true;
    c.frame.data.assign((size_t)w * h, v);
    if (vline) for (int y = 0; y < h; y++) c.frame.data[(size_t)y * w + w / 2] = 255;
    return c;
}
}  // namespace

TEST(EdgeBaseline, LineScoresAboveFlatSurface) {
    EdgeClassifier cls;
    auto cat = PatternCatalog::parse("p|P|d\n");
    auto flat = cls.classify(grayImg(64, 64, 100, false), cat);
    auto line = cls.classify(grayImg(64, 64, 0, true), cat);
    ASSERT_TRUE(flat.ok && line.ok);
    EXPECT_GT(line.detections[0].confidence, flat.detections[0].confidence);
    EXPECT_FALSE(cls.classify(CapturedImage{}, cat).ok);  // sem pixels: falha, nao "nada"
}

// Regressao: getPixel limitava y pela LARGURA; em imagem retrato (h > w) as linhas abaixo de y=w
// eram lidas como a linha w-1 e o gradiente vertical sumia. Imagens transpostas devem dar a mesma densidade.
TEST(EdgeBaseline, PortraitAndLandscapeGiveSameDensityForTransposedImage) {
    EdgeProcessor p;
    CapturedImage portrait = grayImg(40, 120, 0, false);
    CapturedImage landscape = grayImg(120, 40, 0, false);
    for (int y = 80; y < 84; y++) for (int x = 0; x < 40; x++) portrait.frame.data[(size_t)y * 40 + x] = 255;   // faixa horizontal em y=80 (> largura)
    for (int x = 80; x < 84; x++) for (int y = 0; y < 40; y++) landscape.frame.data[(size_t)y * 120 + x] = 255;  // faixa vertical em x=80
    float dp = p.analyze(portrait.frame).edge_density, dl = p.analyze(landscape.frame).edge_density;
    EXPECT_GT(dp, 0.0f);
    EXPECT_NEAR(dp, dl, 0.005);
}
