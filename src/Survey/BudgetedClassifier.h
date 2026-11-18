#pragma once
#include "Survey.h"

// Decorador que limita o gasto de um classificador remoto: no maximo `maxCalls` chamadas e para de
// chamar apos `maxConsecutiveFailures` falhas seguidas (ex.: chave invalida, limite de uso, rede fora).
// Imagens barradas voltam como falha ("budget"), logo aparecem em images_failed e nunca como "nada achado".
class BudgetedClassifier : public IVisionClassifier {
    IVisionClassifier& inner;
    int maxCalls, maxConsecutiveFailures;
    int calls = 0, failStreak = 0;

public:
    BudgetedClassifier(IVisionClassifier& c, int maxTotalCalls, int maxConsecutiveFails = 3)
        : inner(c), maxCalls(maxTotalCalls), maxConsecutiveFailures(maxConsecutiveFails) {}

    Classification classify(const CapturedImage& img, const PatternCatalog& cat) override {
        if (calls >= maxCalls) return stop("budget: call limit reached");
        if (failStreak >= maxConsecutiveFailures) return stop("budget: too many consecutive failures");
        calls++;
        Classification c = inner.classify(img, cat);
        failStreak = c.ok ? 0 : failStreak + 1;
        return c;
    }

    int callsMade() const { return calls; }

private:
    static Classification stop(const char* why) {
        Classification c;
        c.ok = false;
        c.error = why;
        return c;
    }
};
