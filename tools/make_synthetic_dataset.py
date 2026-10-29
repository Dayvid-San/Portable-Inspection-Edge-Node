#!/usr/bin/env python3
"""Gera um conjunto SINTETICO (PGM) so para validar o harness de avaliacao.

Os numeros obtidos aqui NAO dizem nada sobre desempenho em fotos reais.
Uso: python3 tools/make_synthetic_dataset.py <pasta_saida> [n_por_classe] [semente]
Cria <pasta>/positive (superficie com linhas escuras finas) e <pasta>/negative
(superficie com ruido e manchas suaves, sem linhas).
"""
import os, random, sys

W = H = 256


def surface(rng):
    base = rng.randint(100, 150)
    img = [[base + rng.randint(-12, 12) for _ in range(W)] for _ in range(H)]
    for _ in range(rng.randint(1, 3)):  # manchas suaves (confundidores)
        cx, cy, r = rng.randrange(W), rng.randrange(H), rng.randint(20, 50)
        d = rng.randint(-25, 25)
        for y in range(max(0, cy - r), min(H, cy + r)):
            for x in range(max(0, cx - r), min(W, cx + r)):
                if (x - cx) ** 2 + (y - cy) ** 2 < r * r:
                    img[y][x] = max(0, min(255, img[y][x] + d))
    return img


def crack(img, rng):
    x, y = rng.randrange(W // 4, 3 * W // 4), rng.randrange(0, H // 4)
    for _ in range(rng.randint(120, 220)):
        x = max(1, min(W - 3, x + rng.choice((-1, 0, 0, 1))))
        y = min(H - 3, y + 1)
        for dx in (0, 1):
            img[y][x + dx] = max(0, img[y][x + dx] - 90)


def write(path, img):
    with open(path, "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % (W, H))
        f.write(bytes(v for row in img for v in row))


def main():
    out = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 0)
    for label in ("positive", "negative"):
        os.makedirs(os.path.join(out, label), exist_ok=True)
        for i in range(n):
            img = surface(rng)
            if label == "positive":
                for _ in range(rng.randint(1, 3)):
                    crack(img, rng)
            write(os.path.join(out, label, "%s_%03d.pgm" % (label, i)), img)


if __name__ == "__main__":
    main()
