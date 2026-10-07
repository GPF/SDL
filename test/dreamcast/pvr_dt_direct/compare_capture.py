#!/usr/bin/env python3
"""Compare the physical capture against expected direct 16-bit .dt values.

Ground truth is the generated .dt payload itself: decode the twiddled 16-bit
texels, expand each field to 8-bit, apply the PVR blend against the known
destination, quantize back to the framebuffer's 5/6-bit fields, and compare
with the captured pixels.

Field expansion observed on hardware is plain shifting (5-bit << 3, 6-bit << 2),
so white RGB565 reads back as (248, 252, 248).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

PANEL = 128
DIRECT_Y = 16
TARGET_Y = 176
PANELS = [16, 240, 464]
CASES = ["rgb565", "argb1555", "argb4444"]
BITS = {"rgb565": (5, 6, 5, 0), "argb1555": (5, 5, 5, 1), "argb4444": (4, 4, 4, 4)}
TOLERANCE = 8        # one 5-bit framebuffer step: rounding and dither
RAMP_TOLERANCE = 24  # ramp rows (1, 6, 7): source dither plus filtering
RAMP_ROWS = (1, 6, 7)
ALPHA_ROWS = (2, 3, 4, 5, 6)
ALPHA_TOLERANCE = 24  # hardware alpha rounding differs from a*17 by up to one step
BLOCK_PX = 16   # one 32x32 source block at the 2:1 panel scale
MARGIN = 3      # skip pixels this close to a block edge: filtering and 1px placement shifts


def morton(x, y):
    """pvrtex twiddle: y bits land in even positions, x bits in odd positions."""
    idx = 0
    for i in range(8):
        idx |= ((y >> i) & 1) << (2 * i)
        idx |= ((x >> i) & 1) << (2 * i + 1)
    return idx


def read_ppm(path):
    with open(path, "rb") as fh:
        assert fh.readline().strip() == b"P6"
        while True:
            line = fh.readline()
            if not line.startswith(b"#"):
                break
        w, h = (int(v) for v in line.split())
        fh.readline()
        pixels = fh.read()
    return w, h, pixels


def decode(path):
    data = open(path, "rb").read()
    assert data[:4] == b"DcTx", "bad magic"
    header_size = data[9] or 32          # pvrtex writes 0; the parser treats it as 32
    chunk_size = struct.unpack("<I", data[4:8])[0]
    pvr_type = struct.unpack("<I", data[16:20])[0]
    fmt = pvr_type >> 27
    twiddled = (pvr_type >> 26) & 1 == 0
    w = 8 << ((pvr_type >> 3) & 7)
    h = 8 << (pvr_type & 7)
    return fmt, twiddled, w, h, data[header_size:chunk_size]


def unpack(value, fmt):
    bits = BITS[fmt]
    b = value & ((1 << bits[2]) - 1)
    g = (value >> bits[2]) & ((1 << bits[1]) - 1)
    r = (value >> (bits[2] + bits[1])) & ((1 << bits[0]) - 1)
    a = (value >> 15) & 1 if bits[3] == 1 else (value >> 12) & 15 if bits[3] == 4 else 0
    return r, g, b, a


def expand(field, bits):
    """Replicate a source field to 8 bits so full-scale stays 255."""
    return field * 255 // ((1 << bits) - 1)


def to_fb(value, bits):
    """Store an 8-bit value in a 5/6-bit framebuffer field and read it back."""
    return (value >> (8 - bits)) << (8 - bits)


def expected(fmt, packed, dest, blend):
    r, g, b, a = unpack(packed, fmt)
    bits = BITS[fmt]
    src = (expand(r, bits[0]), expand(g, bits[1]), expand(b, bits[2]))
    fb_bits = (5, 6, 5)
    if not blend:
        return tuple(to_fb(s, fw) for s, fw in zip(src, fb_bits))
    alpha = (255 if a else 0) if bits[3] == 1 else a * 17
    return tuple(to_fb((s * alpha + d * (255 - alpha)) // 255, fw)
                 for s, d, fw in zip(src, dest, fb_bits))


def check_placement(w, pixels, x0, y0, dest):
    """Find the panel's drawn extents and require exactly PANEL x PANEL at (x0, y0).

    Row 0 and column 0 are opaque at both ends (white..red, white..green), so
    the first/last pixel that differs from the destination is the panel edge.
    The block comparison skips edge pixels and cannot see a shifted panel.
    """
    def px(x, y):
        off = (y * w + x) * 3
        return tuple(pixels[off:off + 3])

    xs = [x for x in range(x0 - 8, x0 + PANEL + 8) if px(x, y0 + 4) != dest]
    ys = [y for y in range(y0 - 8, y0 + PANEL + 8) if px(x0 + 5, y) != dest]
    if not xs or not ys:
        return ["panel not found"]
    got = (xs[0], ys[0], xs[-1] - xs[0] + 1, ys[-1] - ys[0] + 1)
    want = (x0, y0, PANEL, PANEL)
    return [] if got == want else ["placement x,y,w,h got%s want%s" % (got, want)]


def main(capture):
    w, h, pixels = read_ppm(capture)
    total = bad = 0
    dest_direct = (0, 0, 248)   # blue clear, as stored in the RGB565 framebuffer
    dest_target = (0, 0, 0)     # black clear inside the RGB565 target
    for case in CASES:
        fmt = {"rgb565": 1, "argb1555": 0, "argb4444": 2}[case]
        got_fmt, twiddled, tw, th, payload = decode(os.path.join(HERE, "romdisk", "%s.dt" % case))
        assert got_fmt == fmt and twiddled and tw == 256 and th == 256
        blend = case != "rgb565"
        for panel, x0 in [(CASES.index(case), PANELS[CASES.index(case)])]:
            for row, (y0, dest) in enumerate(((DIRECT_Y, dest_direct), (TARGET_Y, dest_target))):
                mismatches = checked = 0
                for dy in range(PANEL):
                    for dx in range(PANEL):
                        if min(dx % BLOCK_PX, BLOCK_PX - 1 - dx % BLOCK_PX,
                               dy % BLOCK_PX, BLOCK_PX - 1 - dy % BLOCK_PX) < MARGIN:
                            continue
                        # The default scale mode is linear, so a 2:1 minify blends
                        # the texels around (2dx, 2dy); accept anything inside
                        # the range those neighbours (and their dither) span.
                        wants = []
                        for ty in (2 * dy - 1, 2 * dy, 2 * dy + 1):
                            for tx in (2 * dx - 1, 2 * dx, 2 * dx + 1):
                                idx = morton(min(max(tx, 0), 255), min(max(ty, 0), 255))
                                packed = struct.unpack("<H", payload[idx * 2:idx * 2 + 2])[0]
                                wants.append(expected(case, packed, dest, blend))
                        lo = [min(c) for c in zip(*wants)]
                        hi = [max(c) for c in zip(*wants)]
                        want = wants[4]
                        off = ((y0 + dy) * w + (x0 + dx)) * 3
                        got = tuple(pixels[off:off + 3])
                        total += 1
                        checked += 1
                        tol = RAMP_TOLERANCE if dy // BLOCK_PX in RAMP_ROWS else \
                            ALPHA_TOLERANCE if blend and dy // BLOCK_PX in ALPHA_ROWS else TOLERANCE
                        if any(g < l - tol or g > h + tol
                               for g, l, h in zip(got, lo, hi)):
                            mismatches += 1
                            if mismatches <= 2:
                                print("  %s panel%d %s block(%d,%d) got%s want%s" %
                                      (case, panel, "direct" if row == 0 else "target",
                                       dx // 16, dy // 16, got, want))
                bad += mismatches
                for problem in check_placement(w, pixels, x0, y0, dest_direct):
                    bad += 1
                    print("  %s panel%d %s %s" % (case, panel,
                                                  "direct" if row == 0 else "target", problem))
                print("%s panel%d %s: %d/%d pixels match" %
                      (case, panel, "direct" if row == 0 else "target",
                       checked - mismatches, checked))
    print("total %d pixels, %d outside tolerance" % (total, bad))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "results", "pvr_dt_direct.ppm")))
