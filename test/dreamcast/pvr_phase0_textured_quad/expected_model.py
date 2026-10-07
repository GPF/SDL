#!/usr/bin/env python3
"""Expected output for pvr_phase0_textured_quad, derived from the fixture source.

Usage: expected_model.py capture.ppm [capture.ppm ...]

Fixture: 64x64 ARGB1555 texture, 8-texel checker cells, 0xfc00 red / 0x801f blue
(opaque), drawn into (256,176,128,128) over a white clear, so 2x magnification and
16 px cells. Capture is the RGB565 front buffer decoded as (r5<<3, g6<<2, b5<<3).

nearest : exact. Each texel is a 2x2 block of one color.
bilinear: ideal float filter with texel-center sampling and REPEAT addressing
          (the quad's edge pixels blend with the texture's opposite edge). The PVR
          output tracks this within a few 5-bit levels; the blend weights on hardware
          are biased (see results). Use a tolerance of 3 levels, not equality.
"""
import collections
import math
import sys

W, H = 640, 480
QX, QY, QS = 256, 176, 128
TOLERANCE_5BIT = 3.0


def read_ppm(path):
    with open(path, "rb") as fh:
        fh.readline()
        fh.readline()
        w, h = map(int, fh.readline().split())
        fh.readline()
        return w, h, fh.read()


def texel(tx, ty):
    v = 0x801F if (((tx >> 3) + (ty >> 3)) & 1) else 0xFC00
    return ((v >> 10) & 31, (v >> 5) & 31, v & 31)


def expected(mode):
    img = {}
    for py in range(QS):
        for px in range(QS):
            if mode == "nearest":
                rgb = texel(px // 2, py // 2)
            else:
                tu, tv = (px + 0.5) / 2 - 0.5, (py + 0.5) / 2 - 0.5
                i0, j0 = math.floor(tu), math.floor(tv)
                fu, fv = tu - i0, tv - j0
                acc = [0.0, 0.0, 0.0]
                for dj, wj in ((0, 1 - fv), (1, fv)):
                    for di, wi in ((0, 1 - fu), (1, fu)):
                        t = texel((i0 + di) % 64, (j0 + dj) % 64)
                        for k in range(3):
                            acc[k] += wi * wj * t[k]
                rgb = tuple(acc)
            img[(QX + px, QY + py)] = rgb
    return img


def score(path, exp):
    w, h, d = read_ppm(path)
    outside_bad = 0
    for y in range(h):
        for x in range(w):
            if not (QY <= y < QY + QS and QX <= x < QX + QS):
                outside_bad += d[(y * w + x) * 3:(y * w + x) * 3 + 3] != b"\xf8\xfc\xf8"
    hist, worst, exact = collections.Counter(), 0.0, 0
    for (x, y), e in exp.items():
        p = d[(y * w + x) * 3:(y * w + x) * 3 + 3]
        err = max(abs(p[0] / 8 - e[0]), abs(p[1] / 8 - e[1]), abs(p[2] / 8 - e[2]))
        hist[int(err + 0.5)] += 1
        worst = max(worst, err)
        exact += err < 0.5
    return outside_bad, exact, worst, dict(sorted(hist.items()))


if __name__ == "__main__":
    models = {m: expected(m) for m in ("nearest", "bilinear")}
    for path in sys.argv[1:]:
        print(path)
        for name, exp in models.items():
            ob, ex, worst, hist = score(path, exp)
            verdict = "MATCH" if ob == 0 and worst <= (0.0 if name == "nearest" else TOLERANCE_5BIT) else "no"
            print(f"  vs {name:8s} {verdict:5s} outside_quad_bad={ob} exact={ex}/{QS * QS} "
                  f"worst_5bit={worst:.2f} err_hist={hist}")
