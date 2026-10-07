#!/usr/bin/env python3
"""Extract the triangle clip/emit functions from SDL_render_dreamcast_pvr_geometry.c
(any revision) into a standalone C file whose symbols carry a prefix, so two revisions
can be linked into one host program and compared. Usage: extract.py SOURCE.c PREFIX > out.c"""
import re
import sys

NAMES = [
    "DC_PVR_FloatChannelToByte", "DC_PVR_PackColor", "DC_PVR_ValidateQueuedBlend",
    "DC_PVR_InterpolateColor", "DC_PVR_InterpolateVertex", "DC_PVR_ClipPlaneDistance",
    "DC_PVR_ClipPlaneContains", "DC_PVR_AppendClippedVertex", "DC_PVR_ClipAgainstPlane",
    "DC_PVR_ClipTriangle", "DC_PVR_FloatIsFinite", "DC_PVR_DoubleIsFinite",
    "DC_PVR_VertexIsFinite", "DC_PVR_TriangleInsideBounds", "DC_PVR_EmitClippedTriangle",
    "DC_PVR_QuadIntersectsBounds", "DC_PVR_RectQuadFastPath", "DC_PVR_EmitClippedQuad",
    "DC_PVR_IsPowerOfTwoScale", "DC_PVR_QueueGeometry", "DC_PVR_QueueCopyGeometry",
    "DC_PVR_QueueCopy",
]


def function_source(text, name):
    m = re.search(r"^(?:static\s+)?[A-Za-z_][\w\s\*]*?\b" + name + r"\s*\(", text, re.M)
    if not m:
        return None
    start = text.rfind("\n\n", 0, m.start()) + 2
    # include a preceding comment block directly attached to the function
    i = text.index("{", m.end())
    depth = 0
    j = i
    while True:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return text[m.start():j + 1]


def main():
    src, prefix = sys.argv[1], sys.argv[2]
    text = open(src).read()
    out = []
    for name in NAMES:
        body = function_source(text, name)
        if body is None:
            continue
        if not body.startswith("static"):
            body = "static " + body
        out.append(body)
    code = "\n\n".join(out)
    # Rename functions only; types and constants stay shared between the two sides.
    for name in NAMES + ["DC_PVR_EmitVertex"]:
        code = re.sub(r"\b" + name + r"\b", prefix + name[len("DC_PVR_"):], code)
    print(code)


main()
