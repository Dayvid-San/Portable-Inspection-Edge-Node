// Avalia um classificador num conjunto rotulado:  <dataset>/positive/*  e  <dataset>/negative/*
//
//   ./EvalClassifier <dataset> [--classifier edge|claude] [--pattern id] [--max-side N]
//
// "claude" usa ANTHROPIC_API_KEY (e CLASSIFIER_MODEL / CLASSIFIER_FALLBACK, como a simulação).
// Falhas de classificação são contadas à parte e ficam FORA das métricas.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>

#include "Survey/ClaudeClassifier.h"
#include "Survey/CurlTransport.h"
#include "Survey/EdgeClassifier.h"
#include "Survey/Metrics.h"

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Tipos aceitos pela API de visão; os demais só servem ao classificador local.
std::string mimeFor(const std::string& ext) {
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif") return "image/gif";
    return "";
}

bool isImage(const std::string& ext) { return !mimeFor(ext).empty() || ext == ".pgm" || ext == ".ppm" || ext == ".bmp"; }

// Reduz por média de blocos até que o maior lado seja <= maxSide.
void downscale(ImageFrame& f, int maxSide) {
    if (maxSide <= 0) return;
    int factor = (std::max(f.width, f.height) + maxSide - 1) / maxSide;
    if (factor <= 1) return;
    int nw = f.width / factor, nh = f.height / factor;
    std::vector<uint8_t> out((size_t)nw * nh);
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            int sum = 0;
            for (int dy = 0; dy < factor; dy++)
                for (int dx = 0; dx < factor; dx++) sum += f.data[(size_t)(y * factor + dy) * f.width + x * factor + dx];
            out[(size_t)y * nw + x] = (uint8_t)(sum / (factor * factor));
        }
    f.data = std::move(out);
    f.width = nw;
    f.height = nh;
}

bool load(const fs::path& p, int maxSide, CapturedImage& img) {
    img.id = p.filename().string();
    std::ifstream in(p, std::ios::binary);
    img.encoded.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    img.mime = mimeFor(lower(p.extension().string()));
    int w, h, ch;
    unsigned char* px = stbi_load(p.string().c_str(), &w, &h, &ch, 1);
    if (!px) return false;
    img.frame.width = w;
    img.frame.height = h;
    img.frame.data.assign(px, px + (size_t)w * h);
    img.frame.valid = true;
    stbi_image_free(px);
    downscale(img.frame, maxSide);
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: " << argv[0] << " <dataset> [--classifier edge|claude] [--pattern id] [--max-side N]\n";
        return 2;
    }
    fs::path root = argv[1];
    std::string which = "edge", patternId = "target";
    int maxSide = 0;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string a = argv[i];
        if (a == "--classifier") which = argv[i + 1];
        else if (a == "--pattern") patternId = argv[i + 1];
        else if (a == "--max-side") maxSide = std::atoi(argv[i + 1]);
        else { std::cerr << "opcao desconhecida: " << a << "\n"; return 2; }
    }

    PatternCatalog catalog;
    catalog.add({patternId, patternId, "the pattern being evaluated (e.g. a defect or element of interest)"});

    CurlTransport transport;
    EdgeClassifier edge;
    ClaudeConfig cfg;
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (const char* m = std::getenv("CLASSIFIER_MODEL")) cfg.model = m;
    if (const char* fb = std::getenv("CLASSIFIER_FALLBACK")) cfg.serverSideFallback = std::string(fb) != "0";
    if (const char* u = std::getenv("CLASSIFIER_BASE_URL")) cfg.baseUrl = u;
    if (key) cfg.apiKey = key;
    ClaudeVisionClassifier claude(transport, cfg);

    IVisionClassifier* cls = nullptr;
    if (which == "edge") cls = &edge;
    else if (which == "claude") {
        if (!key) { std::cerr << "ANTHROPIC_API_KEY nao definida\n"; return 2; }
        cls = &claude;
    } else { std::cerr << "classificador desconhecido: " << which << "\n"; return 2; }

    std::vector<Sample> samples;
    int failed = 0;
    for (const char* label : {"positive", "negative"}) {
        fs::path dir = root / label;
        if (!fs::is_directory(dir)) { std::cerr << "pasta ausente: " << dir << "\n"; return 2; }
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir))
            if (e.is_regular_file() && isImage(lower(e.path().extension().string()))) files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& p : files) {
            CapturedImage img;
            if (!load(p, maxSide, img)) { std::cerr << "nao decodificou: " << p << "\n"; failed++; continue; }
            Classification c = cls->classify(img, catalog);
            if (!c.ok) { std::cerr << "falhou " << img.id << ": " << c.error << "\n"; failed++; continue; }
            double score = 0;
            for (const auto& d : c.detections) if (d.pattern_id == patternId) score = std::max(score, (double)d.confidence);
            samples.push_back({score, std::string(label) == "positive"});
        }
    }

    int np = (int)std::count_if(samples.begin(), samples.end(), [](const Sample& s) { return s.positive; });
    int nn = (int)samples.size() - np;
    std::cout << "classificador: " << which << (which == "claude" ? " (" + cfg.model + ")" : "") << "\n"
              << "avaliadas: " << samples.size() << " (positivas " << np << ", negativas " << nn << "), falhas: " << failed << "\n";
    if (np == 0 || nn == 0) { std::cout << "precisa de positivas e negativas para calcular metricas\n"; return 1; }

    std::cout << std::fixed << std::setprecision(3) << "AUC: " << metrics::auc(samples) << "\n\n"
              << std::setw(22) << "limiar" << std::setw(5) << "TP" << std::setw(5) << "FP" << std::setw(5) << "FN"
              << std::setw(5) << "TN" << std::setw(8) << "prec" << std::setw(8) << "recall" << std::setw(8) << "F1" << "\n";
    auto row = [&](const std::string& name, double t) {
        Confusion c = metrics::confusionAt(samples, t);
        std::cout << std::setw(22) << name << std::setw(5) << c.tp << std::setw(5) << c.fp << std::setw(5) << c.fn << std::setw(5)
                  << c.tn << std::setw(8) << c.precision() << std::setw(8) << c.recall() << std::setw(8) << c.f1() << "\n";
    };
    row("0.50 (padrao do LLM)", 0.5);
    double best = metrics::bestF1Threshold(samples);
    row("melhor F1 = " + std::to_string(best).substr(0, 5), best);
    return 0;
}
