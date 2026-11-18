#pragma once
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "Survey.h"

// Fila de relatórios pendentes em disco: sobrevive a reinício e queda de energia (escrita atômica
// via arquivo temporário + rename). Nunca descarta em silêncio: se o limite de bytes estoura,
// enqueue() devolve false e o chamador decide o que fazer.
class ReportQueue {
    std::filesystem::path dir;
    size_t maxBytes;

    std::vector<std::filesystem::path> files() const {
        std::vector<std::filesystem::path> v;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.is_regular_file() && e.path().extension() == ".json") v.push_back(e.path());
        std::sort(v.begin(), v.end());  // nomes com zero-padding: ordem lexicográfica = ordem de chegada
        return v;
    }

public:
    explicit ReportQueue(std::filesystem::path directory, size_t maxTotalBytes = 256u * 1024 * 1024)
        : dir(std::move(directory)), maxBytes(maxTotalBytes) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))  // restos de escrita interrompida
            if (e.path().extension() == ".tmp") std::filesystem::remove(e.path(), ec);
    }

    size_t size() const { return files().size(); }

    // Relatorios que o servidor recusou de vez ficam em <dir>/rejected para inspecao: nao se perdem
    // e nao bloqueiam a fila.
    size_t rejected() const {
        size_t n = 0;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir / "rejected", ec)) n += e.is_regular_file();
        return n;
    }

    bool reject(const std::string& json) {
        std::error_code ec;
        std::filesystem::create_directories(dir / "rejected", ec);
        auto path = dir / "rejected" / (std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".json");
        std::ofstream out(path, std::ios::binary);
        out.write(json.data(), (std::streamsize)json.size());
        return out.good();
    }

    size_t bytes() const {
        size_t n = 0;
        std::error_code ec;
        for (const auto& f : files()) n += std::filesystem::file_size(f, ec);
        return n;
    }

    bool enqueue(const std::string& json) {
        if (bytes() + json.size() > maxBytes) return false;
        auto all = files();
        unsigned long long next = 1;
        if (!all.empty()) next = std::strtoull(all.back().stem().string().c_str(), nullptr, 10) + 1;
        char name[32];
        std::snprintf(name, sizeof name, "%012llu", next);
        auto tmp = dir / (std::string(name) + ".tmp");
        auto fin = dir / (std::string(name) + ".json");
        {
            std::ofstream out(tmp, std::ios::binary);
            out.write(json.data(), (std::streamsize)json.size());
            if (!out.good()) { out.close(); std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, fin, ec);
        return !ec;
    }

    // Envia do mais antigo ao mais novo; para no primeiro que falhar (preserva a ordem). Devolve quantos enviou.
    size_t flush(IUplink& uplink) {
        size_t sent = 0;
        for (const auto& f : files()) {
            std::ifstream in(f, std::ios::binary);
            std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            std::error_code ec;
            if (uplink.send(body)) {
                std::filesystem::remove(f, ec);
                sent++;
            } else if (uplink.lastFailureWasPermanent()) {
                std::cerr << "[QUEUE] relatorio recusado de vez, movido para rejected/: " << f.filename() << "\n";
                if (reject(body)) std::filesystem::remove(f, ec);
                else break;  // nao conseguiu preservar: nao apaga, nao avanca
            } else {
                break;  // falha transitoria/credencial: preserva a ordem e tenta depois
            }
        }
        return sent;
    }
};

// Uplink resiliente: tenta entregar; se não der, guarda na fila. Antes de enviar um relatório novo,
// esvazia a fila (ordem preservada). send() devolve true somente se este relatório foi ENTREGUE agora;
// false com queued_ok()==true significa "guardado para reenvio".
class ResilientUplink : public IUplink {
    IUplink& inner;
    ReportQueue& queue;
    bool lastQueued = false;

public:
    ResilientUplink(IUplink& u, ReportQueue& q) : inner(u), queue(q) {}

    bool send(const std::string& json) override {
        lastQueued = false;
        queue.flush(inner);
        if (queue.size() == 0) {  // fila nao vazia: o novo vai atras, para manter a ordem
            if (inner.send(json)) return true;
            if (inner.lastFailureWasPermanent()) {
                std::cerr << "[UPLINK] relatorio recusado de vez pelo servidor; guardado em rejected/\n";
                queue.reject(json);
                return false;
            }
        }
        lastQueued = queue.enqueue(json);
        if (!lastQueued) std::cerr << "[UPLINK] fila cheia: relatorio NAO guardado\n";
        return false;
    }

    bool queued_ok() const { return lastQueued; }
    size_t pending() const { return queue.size(); }
};
