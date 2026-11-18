#pragma once
#include <sstream>
#include <string>
#include <vector>
#include "MiniJson.h"

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

    // Aceita JSON ([{"id","label","description"}, ...]) ou o formato de linhas.
    // Entradas JSON sem id são ignoradas; JSON malformado lança std::runtime_error.
    static PatternCatalog parse(const std::string& text) {
        size_t first = text.find_first_not_of(" \t\r\n");
        if (first != std::string::npos && text[first] == '[') return parseJson(text);
        return parseLines(text);
    }

    static PatternCatalog parseJson(const std::string& text) {
        PatternCatalog cat;
        JsonValue root = minijson::parse(text);
        if (root.type != JsonValue::Array) throw std::runtime_error("catalog: expected a JSON array");
        for (const auto& e : root.a) {
            const JsonValue* id = e.get("id");
            if (!id || id->type != JsonValue::String || id->s.empty()) continue;
            const JsonValue* label = e.get("label");
            const JsonValue* desc = e.get("description");
            cat.add({id->s, label ? label->s : id->s, desc ? desc->s : ""});
        }
        return cat;
    }

    // Formato: uma linha por padrão, "id|label|description". '#' inicia comentário.
    static PatternCatalog parseLines(const std::string& text) {
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
