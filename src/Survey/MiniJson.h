#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// JSON mínimo (sem dependências) para ler respostas da API e montar requisições.
struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JsonValue> a;
    std::vector<std::pair<std::string, JsonValue>> o;

    const JsonValue* get(const std::string& key) const {
        for (const auto& kv : o)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

namespace minijson {

inline std::string escape(const std::string& in) {
    std::string out;
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

class Parser {
    const std::string& t;
    size_t i = 0;

    void ws() { while (i < t.size() && isspace((unsigned char)t[i])) i++; }
    [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("json: ") + m); }
    bool lit(const char* w) {
        size_t n = strlen(w);
        if (t.compare(i, n, w) == 0) { i += n; return true; }
        return false;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | cp >> 18); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    unsigned hex4() {
        if (i + 4 > t.size()) fail("bad \\u escape");
        unsigned v = (unsigned)strtoul(t.substr(i, 4).c_str(), nullptr, 16);
        i += 4;
        return v;
    }
    std::string str() {
        std::string o;
        i++;  // aspas de abertura
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') { o += c; continue; }
            if (i >= t.size()) fail("bad escape");
            char e = t[i++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && t.compare(i, 2, "\\u") == 0) {
                        i += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += e;  // \" \\ \/
            }
        }
        if (i >= t.size()) fail("unterminated string");
        i++;
        return o;
    }
    JsonValue val(int depth) {
        if (depth > 64) fail("too deep");
        ws();
        if (i >= t.size()) fail("unexpected end");
        JsonValue v;
        char c = t[i];
        if (c == '{') {
            v.type = JsonValue::Object;
            i++; ws();
            if (t[i] == '}') { i++; return v; }
            for (;;) {
                ws();
                if (i >= t.size() || t[i] != '"') fail("expected key");
                std::string k = str();
                ws();
                if (i >= t.size() || t[i++] != ':') fail("expected ':'");
                v.o.emplace_back(std::move(k), val(depth + 1));
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == '}') { i++; return v; }
                fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = JsonValue::Array;
            i++; ws();
            if (t[i] == ']') { i++; return v; }
            for (;;) {
                v.a.push_back(val(depth + 1));
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == ']') { i++; return v; }
                fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = JsonValue::String; v.s = str(); return v; }
        if (lit("true")) { v.type = JsonValue::Bool; v.b = true; return v; }
        if (lit("false")) { v.type = JsonValue::Bool; return v; }
        if (lit("null")) return v;
        char* end = nullptr;
        v.n = strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) fail("unexpected token");
        i = end - t.c_str();
        v.type = JsonValue::Number;
        return v;
    }

public:
    explicit Parser(const std::string& text) : t(text) {}
    JsonValue parse() {
        JsonValue v = val(0);
        ws();
        if (i != t.size()) fail("trailing data");
        return v;
    }
};

inline JsonValue parse(const std::string& text) { return Parser(text).parse(); }

}  // namespace minijson

inline std::string base64Encode(const std::vector<uint8_t>& d) {
    static const char* tb = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((d.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < d.size(); i += 3) {
        unsigned v = d[i] << 16 | d[i + 1] << 8 | d[i + 2];
        o += tb[v >> 18]; o += tb[(v >> 12) & 63]; o += tb[(v >> 6) & 63]; o += tb[v & 63];
    }
    if (i + 1 == d.size()) {
        unsigned v = d[i] << 16;
        o += tb[v >> 18]; o += tb[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == d.size()) {
        unsigned v = d[i] << 16 | d[i + 1] << 8;
        o += tb[v >> 18]; o += tb[(v >> 12) & 63]; o += tb[(v >> 6) & 63]; o += '=';
    }
    return o;
}
