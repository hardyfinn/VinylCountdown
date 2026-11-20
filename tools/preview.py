#!/usr/bin/env python3
# Picture of all panels side by side with Rack's own component graphics.
#   python3 tools/preview.py out.png [path/to/Rack/res/ComponentLibrary]
import os, sys, re, base64, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
LIB = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "Rack-src", "res", "ComponentLibrary")
sys.path.insert(0, os.path.join(HERE, "..", "res"))
import make_panels as mp

PX = 25.4 / 75.0
KNOB = {"knobH": "RoundHugeBlackKnob", "knobL": "RoundLargeBlackKnob", "knob": "RoundBlackKnob",
        "knobS": "RoundSmallBlackKnob", "trim": "Trimpot"}
SAMPLE = {
    "VinylCountdown": ["RIAA", "> COL"],
    "Scratch": ["LIVE  needle 1.2 s ago", "1 --  2 --  3 BABY  4 --  5 --  6 CHIRP  7 --  8 CRAB"],
}


def comp(g, name, x, y, angle=0.0):
    s = open(os.path.join(LIB, name + ".svg")).read()
    m = re.search(r'width="([0-9.]+)(px|mm)?"', s); n = re.search(r'height="([0-9.]+)(px|mm)?"', s)
    w = float(m.group(1)) * (1 if m.group(2) == "mm" else PX); h = float(n.group(1)) * (1 if n.group(2) == "mm" else PX)
    data = base64.b64encode(s.encode()).decode()
    g.append(f'<g transform="rotate({angle} {x} {y})"><image href="data:image/svg+xml;base64,{data}" '
             f'x="{x - w/2}" y="{y - h/2}" width="{w}" height="{h}"/></g>')


imgs = []
total_w = 0.0
for name, m in mp.MODULES.items():
    W = m["hp"] * mp.HP
    svg = open(os.path.join(HERE, "..", "res", name + ".svg")).read()
    g = []
    for i, (kind, ident, x, y, label, extra) in enumerate(m["items"]):
        if kind in KNOB:
            base = KNOB[kind]
            if os.path.exists(os.path.join(LIB, base + "_bg.svg")):
                comp(g, base + "_bg", x, y)
            comp(g, base, x, y, -135 + 270 * (0.3 + 0.37 * ((i * 7) % 5) / 4))
        elif kind == "sw2": comp(g, "CKSS_0", x, y)
        elif kind == "sw3": comp(g, "CKSSThree_1", x, y)
        elif kind == "btn": comp(g, "VCVButton_0", x, y)
        elif kind == "latch":
            g.append(f'<circle cx="{x}" cy="{y}" r="2.0" fill="#2b2b2b" stroke="#000" stroke-width="0.3"/><circle cx="{x}" cy="{y}" r="1.2" fill="#f0e6c8" fill-opacity="0.9"/>')
        elif kind in ("in", "out"): comp(g, "PJ301M", x, y)
        elif kind == "light":
            g.append(f'<circle cx="{x}" cy="{y}" r="1.1" fill="{m["accent"]}" fill-opacity="0.8" stroke="#000" stroke-width="0.2"/>')
        elif kind == "disp":
            demo = {"Studio": ["A > TAPE SAT +50%", "B > FLUTTER +50%"], "ListeningRoom": ["A > DISTANCE +50%", "B > SPEAKER +50%"]}
            if name in demo and ident in ("A_DISP", "B_DISP"):
                lines = [demo[name][0 if ident == "A_DISP" else 1]]
            elif ident == "SPK_DISP":
                lines = ["BOOKSHELF", "HI-FI"]
            else:
                lines = {"A_DISP": ["A > GROOVE RADIUS  +60%"], "B_DISP": ["B > WRECK XFADE  -40%"]}.get(ident, SAMPLE.get(name, ["", ""]))
            w, h = extra
            for j, ln in enumerate(lines):
                g.append(f'<text x="{x + w / 2}" y="{y + (3.3 if h < 6 else 4.0) + j * 4.2}" font-size="{2.2 if w < 40 else 2.6}" text-anchor="middle" '
                         f'fill="{m["accent"]}" style="font-family:monospace;font-weight:bold">{ln}</text>')
    for sx in (mp.HP, W - 2 * mp.HP):
        for sy in (0.0, mp.H - mp.HP):
            if m["hp"] <= 8 and sx > mp.HP:  # narrow panels: two screws
                continue
            comp(g, "ScrewSilver", sx + 2.54, sy + 2.54)
    svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
    path = f"/tmp/vc_{name}.svg"
    open(path, "w").write(svg)
    imgs.append((path, W))
    total_w += W + 3

scale = 5.0  # px per mm
html = '<html><body style="margin:0;background:#3a3a3a;display:flex;gap:15px;padding:15px">'
for p, W in imgs:
    html += f'<img src="{p}" style="width:{W * scale}px;height:{mp.H * scale}px">'
html += "</body></html>"
open("/tmp/vc_prev.html", "w").write(html)
pw, ph = int(total_w * scale + 30 + 15 * len(imgs)), int(mp.H * scale + 30)
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 1}});
await p.goto('file:///tmp/vc_prev.html'); await p.waitForTimeout(600);
await p.screenshot({{path: '{OUT}'}}); await b.close(); }})();"""
open("/tmp/vc_prev.js", "w").write(js)
subprocess.run(["node", "/tmp/vc_prev.js"], check=True, cwd="/tmp")
print("wrote", OUT)
