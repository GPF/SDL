#!/usr/bin/env python3
"""Pixel-diff two PPM captures with a per-channel tolerance.

Usage: ppm_diff.py <stored.ppm> <new.ppm> [tolerance] [max_clusters]

Counts pixels where any channel differs by more than the tolerance, reports the
bounding box of all differing pixels, and lists the largest connected clusters
(8-connectivity) so a difference can be located on screen.
"""
import sys
from collections import deque


def read_ppm(path):
    with open(path, "rb") as fh:
        if fh.readline().strip() != b"P6":
            raise SystemExit(f"{path}: not a binary PPM")
        while True:
            line = fh.readline()
            if not line.startswith(b"#"):
                break
        w, h = (int(v) for v in line.split())
        fh.readline()  # maxval
        pixels = fh.read()
    if len(pixels) != w * h * 3:
        raise SystemExit(f"{path}: expected {w*h*3} bytes, got {len(pixels)}")
    return w, h, pixels


def clusters(w, h, diff, limit):
    seen = [False] * (w * h)
    out = []
    for start in range(w * h):
        if not diff[start] or seen[start]:
            continue
        q = deque([start])
        seen[start] = True
        members = []
        while q:
            i = q.popleft()
            members.append(i)
            x, y = i % w, i // w
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h:
                        j = ny * w + nx
                        if diff[j] and not seen[j]:
                            seen[j] = True
                            q.append(j)
        xs = [i % w for i in members]
        ys = [i // w for i in members]
        out.append((len(members), min(xs), min(ys), max(xs), max(ys)))
    out.sort(reverse=True)
    return out[:limit]


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    w1, h1, a = read_ppm(sys.argv[1])
    w2, h2, b = read_ppm(sys.argv[2])
    tol = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    maxc = int(sys.argv[4]) if len(sys.argv) > 4 else 12
    if (w1, h1) != (w2, h2):
        print(f"size mismatch: {w1}x{h1} vs {w2}x{h2}")
        raise SystemExit(2)

    diff = [False] * (w1 * h1)
    n = 0
    for i in range(w1 * h1):
        j = i * 3
        if (abs(a[j] - b[j]) > tol or abs(a[j + 1] - b[j + 1]) > tol
                or abs(a[j + 2] - b[j + 2]) > tol):
            diff[i] = True
            n += 1

    print(f"{sys.argv[1]} vs {sys.argv[2]}: tolerance={tol} pixels={w1*h1} differing={n}")
    if not n:
        print("IDENTICAL within tolerance")
        raise SystemExit(0)
    xs = [i % w1 for i in range(w1 * h1) if diff[i]]
    ys = [i // w1 for i in range(w1 * h1) if diff[i]]
    print(f"bbox x={min(xs)}..{max(xs)} y={min(ys)}..{max(ys)}")
    print(f"clusters (count, x0, y0, x1, y1), up to {maxc}:")
    for c, x0, y0, x1, y1 in clusters(w1, h1, diff, maxc):
        print(f"  {c:6d} px  x={x0}..{x1} y={y0}..{y1}")
    raise SystemExit(1)


if __name__ == "__main__":
    main()
