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
    r.flags.push_back({"x", {"i1"}, 0.9f, {0.9f}});
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

// ---- Uplink HTTP, fila offline, orçamento, zonas, catálogo JSON ----
#include <filesystem>
#include "../src/Survey/BudgetedClassifier.h"
#include "../src/Survey/HttpUplink.h"
#include "../src/Survey/ReportQueue.h"

namespace {
struct SeqTransport : IHttpTransport {
    std::vector<int> statuses;
    int calls = 0;
    std::vector<std::string> lastHeaders;
    HttpResponse post(const std::string&, const std::vector<std::string>& h, const std::string&) override {
        lastHeaders = h;
        HttpResponse r;
        r.status = statuses[std::min<size_t>(calls++, statuses.size() - 1)];
        return r;
    }
};

// Uplink controlável: online/offline e registro do que chegou, em ordem.
struct FakeUplink : IUplink {
    bool online = true;
    bool permanent = false;  // quando offline, a falha e definitiva?
    std::vector<std::string> got;
    bool lastFailureWasPermanent() const override { return permanent; }
    bool send(const std::string& j) override {
        if (!online) return false;
        got.push_back(j);
        return true;
    }
};

struct TmpDir {
    std::filesystem::path p;
    TmpDir() {
        p = std::filesystem::temp_directory_path() / ("edge_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(p);
    }
    ~TmpDir() { std::error_code ec; std::filesystem::remove_all(p, ec); }
    static inline int counter = 0;
};
}  // namespace

TEST(HttpUplink, RetriesTransientFailuresWithBackoff) {
    SeqTransport t; t.statuses = {0, 503, 201};
    HttpUplink up(t, "http://x/api", "tok");
    std::vector<int> sleeps;
    up.sleepMs = [&](int ms) { sleeps.push_back(ms); };
    EXPECT_TRUE(up.send("{}"));
    EXPECT_EQ(t.calls, 3);
    EXPECT_EQ(sleeps, (std::vector<int>{1000, 2000}));
    EXPECT_NE(std::find(t.lastHeaders.begin(), t.lastHeaders.end(), "authorization: Bearer tok"), t.lastHeaders.end());
}

TEST(HttpUplink, DoesNotRetryClientErrorsAndGivesUp) {
    SeqTransport a; a.statuses = {401};
    HttpUplink ua(a, "u", "t"); ua.sleepMs = [](int) {};
    EXPECT_FALSE(ua.send("{}"));
    EXPECT_EQ(a.calls, 1);

    SeqTransport b; b.statuses = {500};
    HttpUplink ub(b, "u", "t"); ub.sleepMs = [](int) {};
    EXPECT_FALSE(ub.send("{}"));
    EXPECT_EQ(b.calls, ub.maxAttempts);

    SeqTransport e; e.statuses = {400};
    HttpUplink ue(e, "u", "t"); ue.sleepMs = [](int) {};
    EXPECT_FALSE(ue.send("{}"));
    EXPECT_TRUE(ue.lastFailureWasPermanent());   // conteudo recusado: nao adianta repetir
    SeqTransport f; f.statuses = {401};
    HttpUplink uf(f, "u", "t"); uf.sleepMs = [](int) {};
    EXPECT_FALSE(uf.send("{}"));
    EXPECT_FALSE(uf.lastFailureWasPermanent());  // credencial: um humano corrige, entao fica na fila
    SeqTransport g; g.statuses = {408, 200};
    HttpUplink ug(g, "u", "t"); ug.sleepMs = [](int) {};
    EXPECT_TRUE(ug.send("{}"));

    SeqTransport c; c.statuses = {429, 200};
    HttpUplink uc(c, "u", "t"); uc.sleepMs = [](int) {};
    EXPECT_TRUE(uc.send("{}"));  // 429 e transitório
}

TEST(Survey, JsonAttachesOnlyFlaggedImagesWhenAsked) {
    SurveyReport r;
    r.survey_id = "s";
    CapturedImage c; c.id = "i1"; c.encoded = {'M', 'a', 'n'}; c.pose.yaw_deg = 90;
    r.flagged_images.push_back(c);
    EXPECT_EQ(SurveyPipeline::toJson(r).find("data_base64"), std::string::npos);
    auto v = minijson::parse(SurveyPipeline::toJson(r, true));  // continua JSON válido
    EXPECT_EQ(v.get("images")->a[0].get("data_base64")->s, "TWFu");
    EXPECT_EQ(v.get("images")->a[0].get("pose")->get("yaw_deg")->n, 90);
}

TEST(Survey, JsonEscapesControlCharactersInIds) {
    SurveyReport r;
    r.survey_id = "a\nb\"c";
    auto v = minijson::parse(SurveyPipeline::toJson(r));
    EXPECT_EQ(v.get("survey_id")->s, "a\nb\"c");
}

TEST(ReportQueue, KeepsOrderSurvivesRestartAndStopsAtFirstFailure) {
    TmpDir d;
    FakeUplink up;
    {
        ReportQueue q(d.p);
        EXPECT_TRUE(q.enqueue("r1")); EXPECT_TRUE(q.enqueue("r2")); EXPECT_TRUE(q.enqueue("r3"));
    }
    ReportQueue q(d.p);  // "reinício": relê do disco
    ASSERT_EQ(q.size(), 3u);
    up.online = false;
    EXPECT_EQ(q.flush(up), 0u);
    EXPECT_EQ(q.size(), 3u);  // nada perdido
    up.online = true;
    EXPECT_EQ(q.flush(up), 3u);
    EXPECT_EQ(up.got, (std::vector<std::string>{"r1", "r2", "r3"}));
    EXPECT_EQ(q.size(), 0u);
    EXPECT_TRUE(q.enqueue("r4"));  // numeração continua sem colidir
    EXPECT_EQ(q.size(), 1u);
}

TEST(ReportQueue, RefusesInsteadOfDroppingWhenFullAndCleansPartialWrites) {
    TmpDir d;
    ReportQueue q(d.p, 10);
    EXPECT_TRUE(q.enqueue("123456"));
    EXPECT_FALSE(q.enqueue("123456"));  // estouraria 10 bytes: recusa, não descarta o antigo
    EXPECT_EQ(q.size(), 1u);
    { std::ofstream(d.p / "000000000099.tmp") << "meio-escrito"; }
    ReportQueue again(d.p, 10);
    EXPECT_FALSE(std::filesystem::exists(d.p / "000000000099.tmp"));
}

TEST(ResilientUplink, QueuesWhenOfflineAndDeliversInOrderWhenBack) {
    TmpDir d;
    ReportQueue q(d.p);
    FakeUplink net;
    ResilientUplink up(net, q);
    net.online = false;
    EXPECT_FALSE(up.send("a")); EXPECT_TRUE(up.queued_ok());
    EXPECT_FALSE(up.send("b")); EXPECT_TRUE(up.queued_ok());
    EXPECT_EQ(up.pending(), 2u);
    net.online = true;
    EXPECT_TRUE(up.send("c"));  // esvazia a fila antes do novo
    EXPECT_EQ(net.got, (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(up.pending(), 0u);
}

TEST(ResilientUplink, NewReportGoesBehindAStuckQueue) {
    TmpDir d;
    ReportQueue q(d.p);
    q.enqueue("old");
    FakeUplink net;
    net.online = false;
    ResilientUplink up(net, q);
    EXPECT_FALSE(up.send("new"));
    EXPECT_EQ(up.pending(), 2u);  // "new" não passa na frente de "old"
    net.online = true;
    EXPECT_TRUE(up.send("newer"));
    EXPECT_EQ(net.got, (std::vector<std::string>{"old", "new", "newer"}));
}

TEST(ResilientUplink, PermanentRejectionGoesToRejectedAndDoesNotBlockQueue) {
    TmpDir d;
    ReportQueue q(d.p);
    FakeUplink net;
    ResilientUplink up(net, q);
    net.online = false; net.permanent = true;
    EXPECT_FALSE(up.send("ruim"));
    EXPECT_FALSE(up.queued_ok());
    EXPECT_EQ(up.pending(), 0u);   // nao fica na fila
    EXPECT_EQ(q.rejected(), 1u);   // mas tambem nao se perde
    net.online = true; net.permanent = false;
    EXPECT_TRUE(up.send("bom"));   // a fila nao ficou travada
    EXPECT_EQ(net.got, std::vector<std::string>{"bom"});
}

TEST(ReportQueue, FlushMovesPermanentlyRejectedItemAndContinues) {
    TmpDir d;
    ReportQueue q(d.p);
    q.enqueue("A"); q.enqueue("B");
    struct Picky : IUplink {
        std::vector<std::string> got; bool perm = false;
        bool lastFailureWasPermanent() const override { return perm; }
        bool send(const std::string& j) override { perm = (j == "A"); if (perm) return false; got.push_back(j); return true; }
    } net;
    EXPECT_EQ(q.flush(net), 1u);   // A recusado, B entregue
    EXPECT_EQ(net.got, std::vector<std::string>{"B"});
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.rejected(), 1u);
}

TEST(ResilientUplink, CredentialErrorStaysQueuedForLater) {
    TmpDir d;
    ReportQueue q(d.p);
    FakeUplink net;
    ResilientUplink up(net, q);
    net.online = false; net.permanent = false;  // ex.: 401
    EXPECT_FALSE(up.send("r"));
    EXPECT_TRUE(up.queued_ok());
    EXPECT_EQ(q.rejected(), 0u);
}

TEST(BudgetedClassifier, StopsAtCallLimitAndAfterFailureStreak) {
    ScriptedClassifier ok;
    BudgetedClassifier a(ok, 2, 3);
    auto cat = PatternCatalog::parse("x|X|d\n");
    EXPECT_TRUE(a.classify(jpegImg("1"), cat).ok);
    EXPECT_TRUE(a.classify(jpegImg("2"), cat).ok);
    auto over = a.classify(jpegImg("3"), cat);
    EXPECT_FALSE(over.ok);
    EXPECT_EQ(a.callsMade(), 2);

    FakeTransport t; t.reply.status = 500;
    ClaudeVisionClassifier bad(t, ClaudeConfig{});
    BudgetedClassifier b(bad, 100, 3);
    for (int i = 0; i < 3; i++) EXPECT_FALSE(b.classify(jpegImg("x"), cat).ok);
    int before = b.callsMade();
    EXPECT_FALSE(b.classify(jpegImg("x"), cat).ok);
    EXPECT_EQ(b.callsMade(), before);  // parou de chamar a API
}

TEST(Zones, GroupsNearbyImagesPerPatternAndIgnoresUnlocated) {
    auto cat = PatternCatalog::parse("x|X|d\ny|Y|d\n");
    ScriptedClassifier cls;
    cls.set("a", {{"x", 0.9f}}); cls.set("b", {{"x", 0.6f}}); cls.set("c", {{"x", 0.7f}});
    cls.set("d", {{"x", 0.8f}}); cls.set("e", {{"y", 0.8f}});
    auto at = [](const std::string& id, double lat, double lon, bool has = true) {
        CapturedImage c; c.id = id; c.pose.has_position = has; c.pose.lat = lat; c.pose.lon = lon; return c;
    };
    // a,b a ~11 m; c a ~1,1 km; d sem posição; e: outro padrão no mesmo lugar de a
    std::vector<CapturedImage> imgs = {at("a", -8.0, -34.0), at("b", -8.0001, -34.0), at("c", -8.01, -34.0),
                                       at("d", 0, 0, false), at("e", -8.0, -34.0)};
    SurveyPipeline p(cls, cat, 0.5f);
    auto rep = p.run("s", imgs);
    ASSERT_EQ(rep.zones.size(), 3u);  // x: {a,b}, x: {c}, y: {e}; d (sem posição) fora
    EXPECT_EQ(rep.zones[0].pattern_id, "x");
    EXPECT_EQ(rep.zones[0].image_ids, (std::vector<std::string>{"a", "b"}));
    EXPECT_FLOAT_EQ(rep.zones[0].max_confidence, 0.9f);
    EXPECT_NEAR(rep.zones[0].lat, -8.00005, 1e-6);
    EXPECT_EQ(rep.zones[1].image_ids, std::vector<std::string>{"c"});
    EXPECT_EQ(rep.zones[2].pattern_id, "y");
    p.zoneRadiusM = 0;
    EXPECT_TRUE(p.run("s", imgs).zones.empty());
}

TEST(PatternCatalog, ParsesJsonAndAutoDetectsFormat) {
    auto j = PatternCatalog::parse("  [{\"id\":\"a|b\",\"label\":\"A\",\"description\":\"tem | barra\"},{\"label\":\"sem id\"},{\"id\":\"c\"}]");
    ASSERT_EQ(j.all().size(), 2u);  // entrada sem id ignorada
    EXPECT_EQ(j.all()[0].id, "a|b");
    EXPECT_EQ(j.all()[0].description, "tem | barra");
    EXPECT_EQ(j.all()[1].label, "c");  // label cai para o id
    EXPECT_EQ(PatternCatalog::parse("x|X|d\n").all().size(), 1u);  // formato antigo continua valendo
    EXPECT_THROW(PatternCatalog::parse("[{\"id\":"), std::runtime_error);
    EXPECT_THROW(PatternCatalog::parseJson("{\"id\":\"a\"}"), std::runtime_error);
}
