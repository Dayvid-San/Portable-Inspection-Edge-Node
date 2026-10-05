#pragma once
#include <sstream>
#include <string>
#include <vector>

// Um padrão a ser procurado nas imagens. Nada aqui é específico de domínio:
// o conteúdo vem de configuração (casa, zona rural, etc.).
struct Pattern {
    std::string id;           // identificador estável, ex.: "crack"
    std::string label;        // nome legível
    std::string description;  // texto usado para instruir o classificador
};

class PatternCatalog {
    std::vector<Pattern> patterns;

public:
    void add(Pattern p) { patterns.push_back(std::move(p)); }
    const std::vector<Pattern>& all() const { return patterns; }

    // Formato: uma linha por padrão, "id|label|description". '#' inicia comentário.
    static PatternCatalog parse(const std::string& text) {
        PatternCatalog cat;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f;
            std::istringstream ls(line);
            std::string tok;
            while (std::getline(ls, tok, '|')) f.push_back(tok);
            if (f.size() == 3) cat.add({f[0], f[1], f[2]});
        }
        return cat;
    }
};
