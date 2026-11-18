#!/usr/bin/env python3
"""Receptor minimo (maquina do usuario) para a prova de conceito.

Uso: UPLINK_TOKEN=segredo python3 tools/receiver.py [porta] [pasta_saida]
Recebe POST /api/ingest-scan com Bearer token e grava o relatorio e as imagens sinalizadas.
Escuta apenas em 127.0.0.1; para receber de outra maquina, ponha-o atras de HTTPS (proxy reverso).
"""
import base64, hmac, json, os, sys, time
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
OUT = sys.argv[2] if len(sys.argv) > 2 else "received"
TOKEN = os.environ.get("UPLINK_TOKEN", "")
MAX_BODY = 64 * 1024 * 1024
EXT = {"image/jpeg": "jpg", "image/png": "png", "image/webp": "webp", "image/gif": "gif"}
_seq = 0


def safe(name):
    return "".join(c for c in str(name) if c.isalnum() or c in "-_")[:64] or "x"


class Handler(BaseHTTPRequestHandler):
    def reply(self, code, msg=""):
        body = json.dumps({"status": msg or str(code)}).encode()
        self.send_response(code)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        global _seq
        if self.path != "/api/ingest-scan":
            return self.reply(404)
        auth = self.headers.get("authorization", "")
        if not TOKEN or not hmac.compare_digest(auth.encode(), ("Bearer " + TOKEN).encode()):
            return self.reply(401, "unauthorized")
        try:
            n = int(self.headers.get("content-length", 0))
        except ValueError:
            return self.reply(400, "bad length")
        if n <= 0:
            return self.reply(400, "empty body")
        if n > MAX_BODY:
            return self.reply(413, "too large")
        try:
            report = json.loads(self.rfile.read(n))
            _seq += 1
            folder = os.path.join(OUT, "%s_%d_%d" % (safe(report["survey_id"]), int(time.time()), _seq))
            os.makedirs(folder)
            for im in report.pop("images", []):
                path = os.path.join(folder, "%s.%s" % (safe(im["id"]), EXT.get(im["mime"], "bin")))
                with open(path, "wb") as f:
                    f.write(base64.b64decode(im["data_base64"], validate=True))
            with open(os.path.join(folder, "report.json"), "w") as f:
                json.dump(report, f, indent=2)
        except (KeyError, ValueError, TypeError):
            return self.reply(400, "invalid report")
        print("recebido:", folder, "flags:", [x["pattern"] for x in report["flags"]], flush=True)
        self.reply(201, "created")

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    if not TOKEN:
        sys.exit("defina UPLINK_TOKEN")
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
