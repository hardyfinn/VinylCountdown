#!/usr/bin/env python3
# Draws every VinylCountdown panel (res/<Module>.svg) AND writes
# src/Layout.hpp, so the C++ places each control exactly where the panel
# prints its label. Rack can't draw SVG <text>, so labels become paths.
#   pip install fonttools ; python3 res/make_panels.py
import os, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

H = 128.5
HP = 5.08
CONTROL = 3.2   # control label size (project standard)
JACK = 2.9      # jack label size (project standard)

BG = "#161515"        # record black
BG2 = "#201E1D"
GROOVE = "#2A2726"
INK = "#EFE8DA"
SOFT = "#A39C92"
OUTSTRIP = "#EFE8DA"

# how far above its centre each kind of control gets its label
LABEL_DY = {"knobH": 12.4, "knobL": 10.2, "knob": 7.4, "knobS": 6.4, "trim": 5.0,
            "sw2": 7.2, "sw3": 9.6, "latch": 0, "text": 0, "btn": 5.6, "in": 5.6, "out": 5.6, "light": 2.8, "disp": 0}
LABEL_SIZE = {"in": JACK, "out": JACK}

# ---------------------------------------------------------------------------
# Module specs: (kind, id, x, y, label, extra)
#   sw2/sw3 extra = position names from value 0 upwards (drawn at the right,
#   or at the left when extra ends with "<")
#   disp extra = (w, h)
# ---------------------------------------------------------------------------
C3 = [10.16, 25.4, 40.64]                 # 10 HP, 3 columns
C4_12 = [9.0, 23.0, 37.96, 51.96]         # 12 HP, 4 columns
A10 = [14.5, 36.3]                        # 10 HP audio pairs

# ---- Vinyl Countdown: 32 HP, seven sections in record order -------------
VC = [5.0 + 10.9 + 21.8 * i for i in range(7)]
SECTIONS = [("LATHE", "#C9C2B4"), ("RIAA", "#7FB8A4"), ("PRESSING", "#8FB3D9"), ("GROOVE", "#D9774B"),
            ("STYLUS", "#B7A3D9"), ("PLATTER", "#E8B04A"), ("WRECK", "#E0605A")]
vc = []
SEC_IDS = ["LATHE", "RIAA", "PRESS", "GROOVE", "STYLUS", "PLATTER", "WRECK"]
for i, (nm, col) in enumerate(SECTIONS):
    vc.append(("text", f"{SEC_IDS[i]}_NAME", VC[i], 15.6, nm, (3.2, col)))
    vc.append(("latch", f"{SEC_IDS[i]}_ON", VC[i], 20.8, "", None))
x = VC
vc += [
    # LATHE
    ("knobS", "DRIVE", x[0], 32.5, "DRIVE", None),
    ("knobS", "ELLIPTIC", x[0], 46.5, "ELLIPTIC", None),
    ("knobS", "HF", x[0], 60.5, "HF LIMIT", None),
    ("knobS", "ECHO", x[0], 74.5, "ECHO", None),
    ("light", "HF_LIGHT", x[0], 82.5, "", None),
    ("in", "DRIVE_IN", x[0], 93.5, "DRIVE", None),
    ("in", "ECHO_IN", x[0], 105.5, "ECHO", None),
    # RIAA
    ("knobL", "CUT", x[1], 36.0, "CUT", None),
    ("knobL", "PLAY", x[1], 62.0, "PLAY", None),
    ("disp", "RIAA_DISP", x[1] - 9.6, 73.0, "", (19.2, 9.0)),
    ("in", "CUT_IN", x[1], 93.5, "CUT", None),
    ("in", "PLAY_IN", x[1], 105.5, "PLAY", None),
    # PRESSING
    ("knobL", "GEN", x[2], 36.0, "GENERATION", None),
    ("knobL", "WEAR", x[2], 62.0, "WEAR", None),
    ("in", "GEN_IN", x[2], 93.5, "GEN", None),
    ("in", "WEAR_IN", x[2], 105.5, "WEAR", None),
    # GROOVE (inner groove)
    ("knobL", "RADIUS", x[3], 36.0, "RADIUS", None),
    ("knobS", "TRACK", x[3], 55.0, "TRACK", None),
    ("knobS", "SIDE", x[3], 69.5, "SIDE", None),
    ("btn", "SIDE_PLAY", x[3] - 3.6, 81.5, "", None),
    ("text", "SIDE_PLAY_T", x[3] + 0.6, 82.6, "PLAY", (2.9, "#A39C92", "start")),
    ("in", "RADIUS_IN", x[3], 93.5, "RADIUS", None),
    ("in", "SIDE_PLAY_IN", x[3], 105.5, "PLAY", None),
    # STYLUS
    ("sw3", "TIP", x[4] - 3.0, 33.5, "", ["CON", "ELL", "SHI"]),
    ("text", "TIP_T", x[4] + 0.6, 27.6, "TIP", (3.2, "#EFE8DA", "start")),
    ("knobS", "LOAD", x[4], 49.5, "LOAD", None),
    ("knobS", "AZIMUTH", x[4], 63.0, "AZIMUTH", None),
    ("knobS", "VERT", x[4], 76.5, "VERTICAL", None),
    ("in", "AZIMUTH_IN", x[4], 93.5, "AZIM", None),
    ("in", "VERT_IN", x[4], 105.5, "VERT", None),
    # PLATTER
    ("knobL", "OFFSET", x[5], 36.0, "OFFSET", None),
    ("sw3", "RPM", x[5] - 3.0, 57.0, "RPM", ["33", "45", "78"]),
    ("knobS", "BEATS", x[5], 74.0, "BEATS", None),
    ("light", "REV_LIGHT", x[5], 82.5, "", None),
    ("in", "OFFSET_IN", x[5], 93.5, "OFFSET", None),
    ("out", "WOW_OUT", x[5], 105.5, "WOW", None),
    # WRECK (trainwreck)
    ("knobS", "DRIFT", x[6], 32.5, "DRIFT", None),
    ("knobS", "SLOP", x[6], 46.5, "SLOP", None),
    ("knobS", "WOFFSET", x[6], 60.5, "OFFSET", None),
    ("knobS", "XFADE", x[6], 74.5, "XFADE", None),
    ("btn", "SYNC", x[6] - 3.6, 82.5, "", None),
    ("light", "NUDGE_LIGHT", x[6] + 3.6, 82.5, "", None),
    ("in", "XFADE_IN", x[6], 93.5, "XFADE", None),
    ("in", "SYNC_IN", x[6], 105.5, "SYNC", None),
    # bottom row
    ("in", "IN_L", x[0], 118.0, "IN L", None),
    ("in", "IN_R", x[1], 118.0, "IN R", None),
    ("in", "CLOCK_IN", x[2], 118.0, "CLOCK", None),
    ("in", "B_L", x[3], 118.0, "B L", None),
    ("in", "B_R", x[4], 118.0, "B R", None),
    ("out", "OUT_L", x[5], 118.0, "OUT L", None),
    ("out", "OUT_R", x[6], 118.0, "OUT R", None),
]
MODULES = {
    "VinylCountdown": dict(hp=32, title="Vinyl Countdown", accent="#E8B04A", items=vc, columns=VC,
                           title_y=8.4, title_size=3.6, logo=(VC[3], 125.6)),
}

# ---- Scratch: 24 HP ----------------------------------------------------------
SX = [8.5 + 14.99 * i for i in range(8)]
scr = [("disp", "DISP", 5.0, 15.0, "", (111.92, 18.0))]
for i in range(8):
    scr.append(("knobS", f"STEP{i + 1}", SX[i], 45.0, str(i + 1), None))
    scr.append(("light", f"STEP{i + 1}_LIGHT", SX[i], 51.5, "", None))
scr += [
    ("sw3", "SOURCE", 7.5, 67.0, "SOURCE", ["LIVE", "GRAB", "WAV"]),
    ("btn", "GRAB", 25.5, 67.0, "GRAB", None),
    ("knob", "CUE", 39.5, 67.0, "CUE", None),
    ("knob", "BACK", 54.0, 67.0, "BACK", None),
    ("knob", "GESTURE", 70.0, 67.0, "GESTURE", None),
    ("knob", "LENGTH", 88.0, 67.0, "LENGTH", None),
    ("knobL", "DEPTH", 107.0, 67.0, "DEPTH", None),
    ("knobL", "HUMAN", 13.0, 88.0, "HUMAN", None),
    ("knobL", "SLOPPY", 34.0, 88.0, "SLOPPY", None),
    ("knob", "CHANCE", 54.0, 88.0, "CHANCE", None),
    ("knob", "STEPS", 69.0, 88.0, "STEPS", None),
    ("knob", "CURVE", 84.0, 88.0, "CURVE", None),
    ("sw2", "MODE", 96.5, 88.0, "", ["INS", "OVER"]),
    ("knob", "DUCK", 113.0, 88.0, "DUCK", None),
]
for i, (k, n) in enumerate([("CLOCK_IN", "CLOCK"), ("RESET_IN", "RESET"), ("TRIG_IN", "TRIG"), ("GEST_IN", "GEST"),
                            ("GRAB_IN", "GRAB"), ("CUE_IN", "CUE"), ("DEPTH_IN", "DEPTH"), ("HUMAN_IN", "HUMAN")]):
    scr.append(("in", k, SX[i], 104.5, n, None))
for i, k, n, kind in [(0, "IN_L", "IN L", "in"), (1, "IN_R", "IN R", "in"), (3, "END_OUT", "END", "out"),
                      (4, "HAND_OUT", "HAND", "out"), (5, "FADER_OUT", "FADER", "out"),
                      (6, "OUT_L", "OUT L", "out"), (7, "OUT_R", "OUT R", "out")]:
    scr.append((kind, k, SX[i], 118.5, n, None))
MODULES["Scratch"] = dict(hp=24, accent="#E6C84F", items=scr, logo=((SX[1] + SX[3]) / 2, 125.6))

# ---------------------------------------------------------------------------
fonts = {}


def text(s, x, y, size, font=FONT_BOLD, fill=INK, anchor="middle", spacing=0.04):
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = {"middle": x - width * scale / 2, "end": x - width * scale}.get(anchor, x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


def panel(name, m):
    W = m["hp"] * HP
    acc = m["accent"]
    p = []
    add = p.append
    add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
    # faint grooves: concentric arcs from a "spindle" far below the panel
    cx, cy = W * 0.5, H + 60.0
    for i in range(26):
        r = 70.0 + i * 3.1
        add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="{GROOVE}" stroke-width="0.25"/>')
    # title + label-coloured dot (a record label)
    title = m.get("title", name)
    tsize = m.get("title_size", 5.2 if len(title) < 12 else 4.4)
    ty = m.get("title_y", 11.4)
    add(f'<circle cx="7.6" cy="{ty - 1.7}" r="{tsize * 0.5}" fill="{acc}"/><circle cx="7.6" cy="{ty - 1.7}" r="0.5" fill="{BG}"/>')
    add(text(title, 7.6 + tsize * 0.8, ty, tsize, FONT_BOLD, INK, anchor="start", spacing=0.02))
    cols = m.get("columns")
    if cols:
        for i in range(1, len(cols)):
            xx = (cols[i - 1] + cols[i]) / 2
            add(f'<line x1="{xx}" y1="12.0" x2="{xx}" y2="112.0" stroke="#34302E" stroke-width="0.35"/>')

    for kind, ident, x, y, label, extra in m["items"]:
        if kind == "text":
            size, col = extra[0], extra[1]
            anchor = extra[2] if len(extra) > 2 else "middle"
            add(text(label, x, y, size, FONT_BOLD, col, anchor=anchor))
            continue
        if kind == "latch":
            continue
        if kind == "disp":
            w, h = extra
            add(f'<rect x="{x - 0.5}" y="{y - 0.5}" width="{w + 1.0}" height="{h + 1.0}" rx="0.9" '
                f'fill="#0B0B0B" stroke="#3A3634" stroke-width="0.3"/>')
            continue
        if kind == "out":
            add(f'<rect x="{x - 6.0}" y="{y - 9.2}" width="12.0" height="13.6" rx="1.3" '
                f'fill="{OUTSTRIP}" fill-opacity="0.13"/>')
        if label:
            size = LABEL_SIZE.get(kind, CONTROL)
            col = INK if kind not in ("in", "out") else (SOFT if kind == "in" else INK)
            if kind == "light":
                add(text(label, x, y + 5.4, JACK, FONT_BOLD, SOFT))
            else:
                add(text(label, x, y - LABEL_DY[kind], size, FONT_BOLD, col))
        if kind in ("sw2", "sw3") and extra:
            names = [e for e in extra if e != "<"]
            left = extra[-1] == "<"
            n = len(names)
            # value 0 at the bottom, highest value at the top
            for i, nm in enumerate(names):
                if n == 2:
                    yy = y + 1.0 + (2.6 if i == 0 else -2.6)
                else:
                    yy = y + 1.0 + (1 - i) * 4.0
                xx = x - 3.6 if left else x + 3.6
                add(text(nm, xx, yy, JACK, FONT_BOLD, SOFT, anchor="end" if left else "start"))
    for s, x, y, anchor in m.get("marks", []):
        add(text(s, x, y, JACK, FONT_MED, SOFT, anchor=anchor))

    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
           + "\n".join(p) + "\n</svg>\n")
    path = os.path.join(HERE, name + ".svg")
    open(path, "w").write(svg)
    sys.path.insert(0, HERE)
    import haer_logo
    lx, ly = m.get("logo", (W / 2, 126.0))
    haer_logo.add_to_file(path, x=lx, y=ly, cap=3.0, bg=BG)


def layout_header():
    out = ["// Generated by res/make_panels.py - do not edit by hand.",
           "// Positions (mm) of every control, shared with the panel SVGs.",
           "#pragma once", "", "namespace layout {"]
    for name, m in MODULES.items():
        out.append(f"namespace {name} {{")
        out.append(f"static const float WIDTH = {m['hp'] * HP:.2f}f;")
        for kind, ident, x, y, label, extra in m["items"]:
            out.append(f"static const float {ident}_X = {x:.3f}f, {ident}_Y = {y:.3f}f;")
            if kind == "disp":
                out.append(f"static const float {ident}_W = {extra[0]:.3f}f, {ident}_H = {extra[1]:.3f}f;")
        out.append("}")
    out.append("} // namespace layout")
    open(os.path.join(ROOT, "src", "Layout.hpp"), "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    for name, m in MODULES.items():
        panel(name, m)
    layout_header()
    print("ok:", ", ".join(MODULES))
