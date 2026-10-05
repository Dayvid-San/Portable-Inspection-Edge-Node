#include "Survey.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {
std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}
}  // namespace

SurveyReport SurveyPipeline::run(const std::string& surveyId,
                                 const std::vector<CapturedImage>& images) const {
    SurveyReport rep;
    rep.survey_id = surveyId;
    rep.images_total = (int)images.size();

    for (const auto& img : images) {
        bool flagged = false;
        Classification c = classifier.classify(img, catalog);
        if (!c.ok) {
            rep.failed_image_ids.push_back(img.id);
            continue;
        }
        for (const auto& d : c.detections) {
            if (d.confidence < threshold) continue;
            flagged = true;
            auto it = std::find_if(rep.flags.begin(), rep.flags.end(),
                                   [&](const Flag& f) { return f.pattern_id == d.pattern_id; });
            if (it == rep.flags.end()) {
                rep.flags.push_back({d.pattern_id, {}, 0});
                it = rep.flags.end() - 1;
            }
            it->image_ids.push_back(img.id);
            it->max_confidence = std::max(it->max_confidence, d.confidence);
        }
        if (flagged) rep.flagged_images.push_back(img);
    }
    return rep;
}

std::string SurveyPipeline::toJson(const SurveyReport& r) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "{\n  \"survey_id\": \"" << esc(r.survey_id) << "\",\n"
      << "  \"images_total\": " << r.images_total << ",\n"
      << "  \"images_flagged\": " << r.flagged_images.size() << ",\n"
      << "  \"images_failed\": [";
    for (size_t i = 0; i < r.failed_image_ids.size(); i++)
        o << (i ? ", " : "") << "\"" << esc(r.failed_image_ids[i]) << "\"";
    o << "],\n  \"flags\": [";
    for (size_t i = 0; i < r.flags.size(); i++) {
        const auto& f = r.flags[i];
        o << (i ? "," : "") << "\n    {\"pattern\": \"" << esc(f.pattern_id)
          << "\", \"max_confidence\": " << f.max_confidence << ", \"images\": [";
        for (size_t j = 0; j < f.image_ids.size(); j++)
            o << (j ? ", " : "") << "\"" << esc(f.image_ids[j]) << "\"";
        o << "]}";
    }
    o << "\n  ]\n}";
    return o.str();
}
