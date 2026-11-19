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
            "sw2": 7.2, "sw3": 9.6, "btn": 5.6, "in": 5.6, "out": 5.6, "light": 2.8, "disp": 0}
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

MODULES = {
    "Platter": dict(hp=10, accent="#E8B04A", sub="off-centre wow", items=[
        ("knobL", "OFFSET", 14.5, 31.0, "OFFSET", None),
        ("knobL", "RADIUS", 36.3, 31.0, "RADIUS", None),
        ("sw3", "RPM", 25.4, 57.0, "RPM", ["33", "45", "78"]),
        ("in", "OFFSET_IN", C3[0], 72.0, "OFFSET", None),
        ("in", "RADIUS_IN", C3[1], 72.0, "RADIUS", None),
        ("in", "RESET_IN", C3[2], 72.0, "RESET", None),
        ("out", "WOW_OUT", C3[0], 88.0, "WOW", None),
        ("out", "REV_OUT", C3[1], 88.0, "REV", None),
        ("out", "PHASE_OUT", C3[2], 88.0, "PHASE", None),
        ("in", "IN_L", A10[0], 104.0, "IN L", None),
        ("in", "IN_R", A10[1], 104.0, "IN R", None),
        ("out", "OUT_L", A10[0], 118.0, "OUT L", None),
        ("out", "OUT_R", A10[1], 118.0, "OUT R", None),
    ], marks=[("EDGE", 30.0, 42.6, "middle"), ("LABEL", 43.0, 42.6, "middle")]),

    "InnerGroove": dict(hp=12, title="Inner Groove", accent="#D9774B", sub="edge to label", items=[
        ("knobH", "RADIUS", 30.48, 34.0, "RADIUS", None),
        ("sw3", "STYLUS", 12.0, 62.0, "STYLUS", ["CON", "ELL", "SHI", "<"]),
        ("knob", "TRACK", 27.0, 62.0, "TRACK", None),
        ("knob", "CUT", 41.5, 62.0, "CUT", None),
        ("sw2", "RPM", 52.5, 62.0, "RPM", ["33", "45"]),
        ("knob", "SIDE", 13.0, 80.0, "SIDE", None),
        ("btn", "PLAY", 30.48, 80.0, "PLAY", None),
        ("light", "MIST", 48.0, 80.0, "MISTRACK", None),
        ("in", "RADIUS_IN", C4_12[0], 96.0, "RADIUS", None),
        ("in", "PLAY_IN", C4_12[1], 96.0, "PLAY", None),
        ("in", "RESET_IN", C4_12[2], 96.0, "RESET", None),
        ("out", "POS_OUT", C4_12[3], 96.0, "POS", None),
        ("in", "IN_L", C4_12[0], 112.0, "IN L", None),
        ("in", "IN_R", C4_12[1], 112.0, "IN R", None),
        ("out", "OUT_L", C4_12[2], 112.0, "OUT L", None),
        ("out", "OUT_R", C4_12[3], 112.0, "OUT R", None),
    ], marks=[("EDGE", 15.2, 46.6, "middle"), ("LABEL", 45.8, 46.6, "middle")]),

    "Lathe": dict(hp=14, accent="#C9C2B4", sub="cutting room", items=[
        ("knobL", "DRIVE", 13.0, 31.0, "DRIVE", None),
        ("knobL", "ELLIPTIC", 35.56, 31.0, "ELLIPTIC", None),
        ("knobL", "HF", 58.1, 31.0, "HF LIMIT", None),
        ("light", "HF_LIGHT", 66.6, 22.0, "", None),
        ("knob", "ECHO", 11.0, 55.0, "ECHO", None),
        ("sw2", "REVS", 26.0, 55.0, "REVS", ["1", "2"]),
        ("sw3", "RPM", 41.5, 55.0, "RPM", ["33", "45", "78"]),
        ("knob", "BEATS", 60.1, 55.0, "BEATS", None),
        ("in", "DRIVE_IN", 10.5, 76.0, "DRIVE", None),
        ("in", "ELLIPTIC_IN", 27.2, 76.0, "ELLIPT", None),
        ("in", "HF_IN", 43.9, 76.0, "HF", None),
        ("in", "ECHO_IN", 60.6, 76.0, "ECHO", None),
        ("in", "CLOCK_IN", 10.5, 93.0, "CLOCK", None),
        ("out", "WET_L", 43.9, 93.0, "ECHO L", None),
        ("out", "WET_R", 60.6, 93.0, "ECHO R", None),
        ("in", "IN_L", 10.5, 110.0, "IN L", None),
        ("in", "IN_R", 27.2, 110.0, "IN R", None),
        ("out", "OUT_L", 43.9, 110.0, "OUT L", None),
        ("out", "OUT_R", 60.6, 110.0, "OUT R", None),
    ]),

    "Riaa": dict(hp=12, title="RIAA Sandwich", accent="#7FB8A4", sub="", items=[
        ("knobL", "CUT", 13.5, 30.0, "CUT", None),
        ("knobL", "PLAY", 47.46, 30.0, "PLAY", None),
        ("disp", "DISP", 5.0, 42.0, "", (50.96, 9.0)),
        ("knob", "DRIVE", 16.0, 64.0, "DRIVE", None),
        ("knob", "MIX", 44.96, 64.0, "MIX", None),
        ("out", "SEND_L", C4_12[0], 82.0, "SEND L", None),
        ("out", "SEND_R", C4_12[1], 82.0, "SEND R", None),
        ("in", "RTN_L", C4_12[2], 82.0, "RTN L", None),
        ("in", "RTN_R", C4_12[3], 82.0, "RTN R", None),
        ("in", "DRIVE_IN", C4_12[0], 97.5, "DRIVE", None),
        ("in", "MIX_IN", C4_12[1], 97.5, "MIX", None),
        ("in", "CUT_IN", C4_12[2], 97.5, "CUT", None),
        ("in", "PLAY_IN", C4_12[3], 97.5, "PLAY", None),
        ("in", "IN_L", C4_12[0], 113.0, "IN L", None),
        ("in", "IN_R", C4_12[1], 113.0, "IN R", None),
        ("out", "OUT_L", C4_12[2], 113.0, "OUT L", None),
        ("out", "OUT_R", C4_12[3], 113.0, "OUT R", None),
    ]),

    "Trainwreck": dict(hp=12, accent="#E0605A", sub="two decks", items=[
        ("knobL", "DRIFT", 13.0, 31.0, "DRIFT", None),
        ("knobL", "SLOP", 47.96, 31.0, "SLOP", None),
        ("disp", "DISP", 22.5, 25.5, "", (15.96, 11.0)),
        ("knob", "OFFSET", 11.0, 55.0, "OFFSET", None),
        ("knobL", "XFADE", 30.48, 55.0, "A  XFADE  B", None),
        ("btn", "SYNC", 50.5, 55.0, "SYNC", None),
        ("sw2", "RANGE", 10.0, 72.5, "", ["LATE", "BOTH"]),
        ("light", "NUDGE_LIGHT", 44.0, 72.5, "", None),
        ("out", "NUDGE_OUT", C4_12[3], 72.5, "NUDGE", None),
        ("in", "DRIFT_IN", C4_12[0], 87.0, "DRIFT", None),
        ("in", "XFADE_IN", C4_12[1], 87.0, "XFADE", None),
        ("in", "SYNC_IN", C4_12[2], 87.0, "SYNC", None),
        ("out", "OFFSET_OUT", C4_12[3], 87.0, "OFFSET", None),
        ("in", "A_L", C4_12[0], 102.0, "A L", None),
        ("in", "A_R", C4_12[1], 102.0, "A R", None),
        ("in", "B_L", C4_12[2], 102.0, "B L", None),
        ("in", "B_R", C4_12[3], 102.0, "B R", None),
        ("out", "OUT_L", C4_12[0], 117.0, "OUT L", None),
        ("out", "OUT_R", C4_12[1], 117.0, "OUT R", None),
        ("out", "BOUT_L", C4_12[2], 117.0, "B L", None),
        ("out", "BOUT_R", C4_12[3], 117.0, "B R", None),
    ]),

    "Stylus": dict(hp=10, accent="#B7A3D9", sub="cartridge", items=[
        ("knobL", "LOAD", 14.5, 31.0, "LOAD", None),
        ("knobL", "AZIMUTH", 36.3, 31.0, "AZIMUTH", None),
        ("knob", "XTALK", 14.5, 54.0, "XTALK", None),
        ("knob", "VERT", 36.3, 54.0, "VERTICAL", None),
        ("sw2", "TYPE", 22.4, 70.0, "", ["MM", "MC"]),
        ("in", "LOAD_IN", C3[0], 86.0, "LOAD", None),
        ("in", "AZIMUTH_IN", C3[1], 86.0, "AZIM", None),
        ("in", "VERT_IN", C3[2], 86.0, "VERT", None),
        ("in", "IN_L", A10[0], 102.0, "IN L", None),
        ("in", "IN_R", A10[1], 102.0, "IN R", None),
        ("out", "OUT_L", A10[0], 117.0, "OUT L", None),
        ("out", "OUT_R", A10[1], 117.0, "OUT R", None),
    ]),

    "Pressing": dict(hp=8, accent="#8FB3D9", sub="", items=[
        ("knobL", "GEN", 20.32, 32.0, "GENERATION", None),
        ("knobL", "WEAR", 20.32, 60.0, "WEAR", None),
        ("in", "GEN_IN", 10.16, 82.0, "GEN", None),
        ("in", "WEAR_IN", 30.48, 82.0, "WEAR", None),
        ("in", "IN_L", 10.16, 99.0, "IN L", None),
        ("in", "IN_R", 30.48, 99.0, "IN R", None),
        ("out", "OUT_L", 10.16, 116.0, "OUT L", None),
        ("out", "OUT_R", 30.48, 116.0, "OUT R", None),
    ], marks=[("LACQUER", 6.0, 43.4, "start"), ("LATE", 34.64, 43.4, "end"),
              ("FRESH", 6.0, 71.4, "start"), ("WORN", 34.64, 71.4, "end")]),
}

# Scratch: 16 HP, 8 step knobs
SX = [8.0 + i * 9.32 for i in range(8)]
S6 = [8.0, 21.06, 34.12, 47.16, 60.22, 73.28]
scr = [("disp", "DISP", 4.0, 15.5, "", (73.28, 12.5))]
for i in range(8):
    scr.append(("knobS", f"STEP{i + 1}", SX[i], 40.0, str(i + 1), None))
    scr.append(("light", f"STEP{i + 1}_LIGHT", SX[i], 47.0, "", None))
scr += [
    ("knobL", "DEPTH", 13.5, 63.0, "DEPTH", None),
    ("knob", "CHANCE", 33.0, 63.0, "CHANCE", None),
    ("knob", "BACK", 50.0, 63.0, "BACK", None),
    ("knob", "STEPS", 67.0, 63.0, "STEPS", None),
    ("in", "CLOCK_IN", S6[0], 86.0, "CLOCK", None),
    ("in", "RESET_IN", S6[1], 86.0, "RESET", None),
    ("in", "DEPTH_IN", S6[2], 86.0, "DEPTH", None),
    ("in", "CHANCE_IN", S6[3], 86.0, "CHANCE", None),
    ("out", "HAND_OUT", S6[4], 86.0, "HAND", None),
    ("out", "FADER_OUT", S6[5], 86.0, "FADER", None),
    ("in", "IN_L", S6[0], 112.0, "IN L", None),
    ("in", "IN_R", S6[1], 112.0, "IN R", None),
    ("out", "OUT_L", S6[4], 112.0, "OUT L", None),
    ("out", "OUT_R", S6[5], 112.0, "OUT R", None),
]
MODULES["Scratch"] = dict(hp=16, accent="#E6C84F", sub="gesture sequencer", items=scr)

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
    tsize = 5.2 if len(title) < 12 else 4.4
    add(f'<circle cx="7.6" cy="9.7" r="2.6" fill="{acc}"/><circle cx="7.6" cy="9.7" r="0.55" fill="{BG}"/>')
    add(text(title, 11.6, 11.4, tsize, FONT_BOLD, INK, anchor="start", spacing=0.02))

    for kind, ident, x, y, label, extra in m["items"]:
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
    haer_logo.add_to_file(path, x=W / 2, y=126.0, cap=3.0, bg=BG)


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
