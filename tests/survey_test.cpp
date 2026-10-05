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
