#include "Survey.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include "MiniJson.h"

namespace {
std::string esc(const std::string& s) { return minijson::escape(s); }

// Distancia aproximada (equirretangular) em metros: suficiente para raios de dezenas de metros.
double distanceM(double lat1, double lon1, double lat2, double lon2) {
    const double kPi = 3.14159265358979323846;
    double dy = (lat2 - lat1) * 110540.0;
    double dx = (lon2 - lon1) * 111320.0 * std::cos((lat1 + lat2) / 2 * kPi / 180.0);
    return std::sqrt(dx * dx + dy * dy);
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
                rep.flags.push_back({d.pattern_id, {}, 0, {}});
                it = rep.flags.end() - 1;
            }
            it->image_ids.push_back(img.id);
            it->confidences.push_back(d.confidence);
            it->max_confidence = std::max(it->max_confidence, d.confidence);
        }
        if (flagged) rep.flagged_images.push_back(img);
    }
    if (zoneRadiusM > 0) rep.zones = groupZones(rep, zoneRadiusM);
    return rep;
}

std::vector<Zone> SurveyPipeline::groupZones(const SurveyReport& r, double radiusM) {
    std::vector<Zone> zones;
    for (const auto& f : r.flags) {
        std::vector<Zone> mine;
        for (size_t i = 0; i < f.image_ids.size(); i++) {
            auto img = std::find_if(r.flagged_images.begin(), r.flagged_images.end(),
                                    [&](const CapturedImage& c) { return c.id == f.image_ids[i]; });
            if (img == r.flagged_images.end() || !img->pose.has_position) continue;
            float conf = i < f.confidences.size() ? f.confidences[i] : f.max_confidence;

            Zone* z = nullptr;
            for (auto& cand : mine)
                if (distanceM(cand.lat, cand.lon, img->pose.lat, img->pose.lon) <= radiusM) { z = &cand; break; }
            if (!z) {
                mine.push_back({f.pattern_id, img->pose.lat, img->pose.lon, {}, 0});
                z = &mine.back();
            }
            double n = (double)z->image_ids.size();  // centroide como media corrente
            z->lat = (z->lat * n + img->pose.lat) / (n + 1);
            z->lon = (z->lon * n + img->pose.lon) / (n + 1);
            z->image_ids.push_back(img->id);
            z->max_confidence = std::max(z->max_confidence, conf);
        }
        zones.insert(zones.end(), mine.begin(), mine.end());
    }
    return zones;
}

std::string SurveyPipeline::toJson(const SurveyReport& r, bool includeImages) {
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
    o << "\n  ],\n  \"zones\": [";
    for (size_t i = 0; i < r.zones.size(); i++) {
        const auto& z = r.zones[i];
        o << (i ? "," : "") << "\n    {\"pattern\": \"" << esc(z.pattern_id) << "\", \"lat\": " << std::setprecision(6)
          << z.lat << ", \"lon\": " << z.lon << ", \"max_confidence\": " << std::setprecision(2) << z.max_confidence
          << ", \"images\": [";
        for (size_t j = 0; j < z.image_ids.size(); j++)
            o << (j ? ", " : "") << "\"" << esc(z.image_ids[j]) << "\"";
        o << "]}";
    }
    o << "\n  ]";
    if (includeImages) {
        o << ",\n  \"images\": [";
        for (size_t i = 0; i < r.flagged_images.size(); i++) {
            const auto& im = r.flagged_images[i];
            o << (i ? "," : "") << "\n    {\"id\": \"" << esc(im.id) << "\", \"mime\": \"" << esc(im.mime)
              << "\", \"pose\": {\"has_position\": " << (im.pose.has_position ? "true" : "false")
              << ", \"lat\": " << std::setprecision(6) << im.pose.lat << ", \"lon\": " << im.pose.lon
              << ", \"alt_m\": " << std::setprecision(2) << im.pose.alt_m << ", \"yaw_deg\": " << im.pose.yaw_deg
              << ", \"pitch_deg\": " << im.pose.pitch_deg << "}, \"data_base64\": \"" << base64Encode(im.encoded) << "\"}";
        }
        o << "\n  ]";
    }
    o << "\n}";
    return o.str();
}
