#pragma once
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include "ClaudeClassifier.h"

// Transporte via binário `curl` (sem dependências de build). Segue HTTPS_PROXY do ambiente.
// Cabeçalhos (incluem a chave) e corpo vão por arquivos temporários 0600, nunca pela linha de comando.
class CurlTransport : public IHttpTransport {
    static bool writeTemp(char* tmpl, const std::string& content) {
        int fd = mkstemp(tmpl);  // cria com modo 0600
        if (fd < 0) return false;
        ssize_t n = ::write(fd, content.data(), content.size());
        ::close(fd);
        return n == (ssize_t)content.size();
    }

public:
    int timeoutSec = 120;

    HttpResponse post(const std::string& url, const std::vector<std::string>& headers,
                      const std::string& body) override {
        HttpResponse r;
        char hf[] = "/tmp/edge_hdr_XXXXXX";
        char bf[] = "/tmp/edge_body_XXXXXX";
        std::string hdrs;
        for (const auto& h : headers) hdrs += h + "\n";
        if (!writeTemp(hf, hdrs) || !writeTemp(bf, body)) {
            r.error = "cannot create temp files";
            std::remove(hf); std::remove(bf);
            return r;
        }
        std::string cmd = "curl -sS -m " + std::to_string(timeoutSec) + " -X POST -H @" + hf +
                          " --data-binary @" + bf + " -w '\\n%{http_code}' '" + url + "' 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) {
            r.error = "popen failed";
        } else {
            std::string out;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
            pclose(p);
            size_t nl = out.rfind('\n');
            if (nl == std::string::npos) {
                r.error = out;
            } else {
                r.status = atoi(out.c_str() + nl + 1);
                r.body = out.substr(0, nl);
                if (r.status == 0) r.error = r.body;
            }
        }
        std::remove(hf);
        std::remove(bf);
        return r;
    }
};
