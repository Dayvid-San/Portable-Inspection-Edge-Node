#include <gtest/gtest.h>
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
