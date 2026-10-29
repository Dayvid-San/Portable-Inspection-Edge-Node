#pragma once
#include <algorithm>
#include <vector>

// Métricas de avaliação de um detector binário (padrão presente / ausente) a partir de pontuações.
struct Sample {
    double score;   // confiança/pontuação do detector para o padrão
    bool positive;  // rótulo verdadeiro
};

struct Confusion {
    int tp = 0, fp = 0, fn = 0, tn = 0;
    double precision() const { return tp + fp ? (double)tp / (tp + fp) : 0; }
    double recall() const { return tp + fn ? (double)tp / (tp + fn) : 0; }
    double f1() const { double p = precision(), r = recall(); return p + r ? 2 * p * r / (p + r) : 0; }
    double accuracy() const { int n = tp + fp + fn + tn; return n ? (double)(tp + tn) / n : 0; }
};

namespace metrics {

// Prediz "positivo" quando score >= t.
inline Confusion confusionAt(const std::vector<Sample>& s, double t) {
    Confusion c;
    for (const auto& x : s) {
        bool pred = x.score >= t;
        if (pred && x.positive) c.tp++;
        else if (pred) c.fp++;
        else if (x.positive) c.fn++;
        else c.tn++;
    }
    return c;
}

// Área sob a curva ROC (probabilidade de um positivo pontuar acima de um negativo; empate conta 0,5).
inline double auc(const std::vector<Sample>& s) {
    double wins = 0;
    long long pairs = 0;
    for (const auto& p : s) {
        if (!p.positive) continue;
        for (const auto& n : s) {
            if (n.positive) continue;
            pairs++;
            wins += p.score > n.score ? 1.0 : (p.score == n.score ? 0.5 : 0.0);
        }
    }
    return pairs ? wins / pairs : 0;
}

// Limiar (entre os scores observados) que maximiza F1; empate -> o maior limiar.
inline double bestF1Threshold(const std::vector<Sample>& s) {
    std::vector<double> ts;
    for (const auto& x : s) ts.push_back(x.score);
    std::sort(ts.begin(), ts.end());
    ts.erase(std::unique(ts.begin(), ts.end()), ts.end());
    double best = ts.empty() ? 0 : ts.front(), bestF = -1;
    for (double t : ts) {
        double f = confusionAt(s, t).f1();
        if (f >= bestF) { bestF = f; best = t; }
    }
    return best;
}

}  // namespace metrics
