#!/usr/bin/env python3
"""Genera Assets/Sampler5Sans.ttf, la tipografia propia del plugin.

Cada letra se define con trazos (lineas y arcos) sobre una grilla; despues se les da
grosor con puntas redondas y se guarda como TrueType.

Uso:  pip install fonttools shapely  &&  python3 Tools/make_font.py
"""

import math
import os

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from shapely.geometry import LineString, MultiPolygon
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

FAMILY = "Sampler5 Sans"
STROKE = 92          # grosor del trazo
SIDE = 88            # espacio a cada lado, medido desde el centro del trazo
CAP, XH, ASC, DESC = 700, 500, 740, -210


def L(*pts):
    return [tuple(p) for p in pts]


def A(cx, cy, rx, ry, a0, a1):
    """Arco de elipse de a0 a a1 grados (0 = derecha, 90 = arriba)."""
    steps = max(4, int(abs(a1 - a0) / 6))
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / steps)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / steps))) for i in range(steps + 1)]


def dot(x, y):
    return [(x, y), (x, y + 1)]


G = {}

# ---- mayusculas ------------------------------------------------------------
G["A"] = [L((0, 0), (220, CAP), (440, 0)), L((72, 230), (368, 230))]
G["B"] = [L((0, 0), (0, CAP)), L((0, CAP), (230, CAP)) + A(230, 530, 170, 170, 90, -90) + L((0, 360)),
          L((0, 360), (250, 360)) + A(250, 180, 180, 180, 90, -90) + L((0, 0))]
G["C"] = [A(225, 350, 225, 350, 48, 312)]
G["D"] = [L((0, 0), (0, CAP)), L((0, CAP), (170, CAP)) + A(170, 350, 270, 350, 90, -90) + L((0, 0))]
G["E"] = [L((400, CAP), (0, CAP), (0, 0), (400, 0)), L((0, 360), (330, 360))]
G["F"] = [L((400, CAP), (0, CAP), (0, 0)), L((0, 360), (320, 360))]
G["G"] = [A(225, 350, 225, 350, 48, 360) + L((250, 350))]
G["H"] = [L((0, 0), (0, CAP)), L((440, 0), (440, CAP)), L((0, 360), (440, 360))]
G["I"] = [L((0, 0), (0, CAP))]
G["J"] = [L((300, CAP), (300, 170)) + A(150, 170, 150, 170, 0, -180)]
G["K"] = [L((0, 0), (0, CAP)), L((400, CAP), (0, 290)), L((135, 415), (420, 0))]
G["L"] = [L((0, CAP), (0, 0), (370, 0))]
G["M"] = [L((0, 0), (0, CAP), (280, 170), (560, CAP), (560, 0))]
G["N"] = [L((0, 0), (0, CAP), (440, 0), (440, CAP))]
G["O"] = [A(230, 350, 230, 350, 0, 360)]
G["P"] = [L((0, 0), (0, CAP)), L((0, CAP), (240, CAP)) + A(240, 505, 195, 195, 90, -90) + L((0, 310))]
G["Q"] = [A(230, 350, 230, 350, 0, 360), L((280, 150), (470, -40))]
G["R"] = [L((0, 0), (0, CAP)), L((0, CAP), (240, CAP)) + A(240, 505, 195, 195, 90, -90) + L((0, 310)),
          L((230, 310), (440, 0))]
G["S"] = [A(215, 525, 195, 175, 35, 270) + A(215, 175, 210, 175, 90, -145)]
G["T"] = [L((0, CAP), (440, CAP)), L((220, CAP), (220, 0))]
G["U"] = [L((0, CAP), (0, 220)) + A(220, 220, 220, 220, 180, 360) + L((440, CAP))]
G["V"] = [L((0, CAP), (220, 0), (440, CAP))]
G["W"] = [L((0, CAP), (150, 0), (310, 560), (470, 0), (620, CAP))]
G["X"] = [L((0, CAP), (420, 0)), L((420, CAP), (0, 0))]
G["Y"] = [L((0, CAP), (220, 320), (440, CAP)), L((220, 320), (220, 0))]
G["Z"] = [L((0, CAP), (420, CAP), (0, 0), (420, 0))]

# ---- numeros (todos del mismo ancho, para que no bailen al cambiar) ---------
G["0"] = [A(210, 350, 210, 350, 0, 360)]
G["1"] = [L((60, 550), (230, CAP), (230, 0)), L((70, 0), (390, 0))]
G["2"] = [A(210, 490, 210, 210, 165, -42) + L((0, 0), (420, 0))]
G["3"] = [A(200, 525, 190, 175, 150, -90) + A(200, 175, 215, 175, 90, -150)]
G["4"] = [L((330, 0), (330, CAP), (0, 215), (420, 215))]
G["5"] = [L((385, CAP), (60, CAP), (35, 405)) + A(195, 225, 225, 225, 128, -150)]
G["6"] = [A(210, 215, 210, 215, 0, 360), A(420, 215, 420, 485, 113, 180)]
G["7"] = [L((0, CAP), (420, CAP), (150, 0))]
G["8"] = [A(210, 532, 168, 168, 0, 360), A(210, 182, 208, 182, 0, 360)]
G["9"] = [A(210, 485, 210, 215, 0, 360), A(0, 485, 420, 485, -67, 0)]
DIGITS = "0123456789"

# ---- minusculas -------------------------------------------------------------
BOWL = A(190, 250, 190, 250, 0, 360)
G["a"] = [BOWL, L((380, XH), (380, 0))]
G["b"] = [BOWL, L((0, ASC), (0, 0))]
G["c"] = [A(190, 250, 190, 250, 50, 310)]
G["d"] = [BOWL, L((380, ASC), (380, 0))]
G["e"] = [L((0, 250), (380, 250)) + A(190, 250, 190, 250, 0, 312)]
G["f"] = [L((90, 0), (90, 570)) + A(230, 570, 140, 170, 180, 78), L((0, XH), (250, XH))]
G["g"] = [BOWL, L((380, XH), (380, -50)) + A(190, -50, 190, 160, 0, -150)]
G["h"] = [L((0, ASC), (0, 0)), A(190, 310, 190, 190, 180, 0) + L((380, 0))]
G["i"] = [L((0, XH), (0, 0)), dot(0, 690)]
G["j"] = [L((150, XH), (150, -60)) + A(0, -60, 150, 150, 0, -105), dot(150, 690)]
G["k"] = [L((0, ASC), (0, 0)), L((330, XH), (0, 190)), L((125, 300), (350, 0))]
G["l"] = [L((0, ASC), (0, 0))]
G["m"] = [L((0, XH), (0, 0)), A(160, 335, 160, 165, 180, 0) + L((320, 0)), A(480, 335, 160, 165, 180, 0) + L((640, 0))]
G["n"] = [L((0, XH), (0, 0)), A(190, 310, 190, 190, 180, 0) + L((380, 0))]
G["o"] = [BOWL]
G["p"] = [BOWL, L((0, XH), (0, DESC))]
G["q"] = [BOWL, L((380, XH), (380, DESC))]
G["r"] = [L((0, XH), (0, 0)), A(190, 300, 190, 200, 180, 55)]
G["s"] = [A(170, 372, 155, 128, 35, 270) + A(170, 128, 170, 128, 90, -145)]
G["t"] = [L((100, 680), (100, 150)) + A(250, 150, 150, 150, 180, 285), L((0, XH), (270, XH))]
G["u"] = [L((0, XH), (0, 190)) + A(190, 190, 190, 190, 180, 360) + L((380, XH)), L((380, XH), (380, 0))]
G["v"] = [L((0, XH), (180, 0), (360, XH))]
G["w"] = [L((0, XH), (130, 0), (270, 420), (410, 0), (540, XH))]
G["x"] = [L((0, XH), (340, 0)), L((340, XH), (0, 0))]
G["y"] = [L((0, XH), (180, 0)), L((360, XH), (105, DESC))]
G["z"] = [L((0, XH), (340, XH), (0, 0), (340, 0))]

# ---- signos -----------------------------------------------------------------
G["."] = [dot(0, 0)]
G[","] = [L((30, 10), (0, -110))]
G[":"] = [dot(0, 0), dot(0, 400)]
G[";"] = [L((30, 10), (0, -110)), dot(30, 400)]
G["-"] = [L((0, 290), (260, 290))]
G["_"] = [L((0, -90), (420, -90))]
G["+"] = [L((0, 320), (360, 320)), L((180, 140), (180, 500))]
G["="] = [L((0, 230), (360, 230)), L((0, 420), (360, 420))]
G["/"] = [L((0, -60), (300, 760))]
G["\\"] = [L((0, 760), (300, -60))]
G["|"] = [L((0, -120), (0, 780))]
G["("] = [A(260, 330, 260, 480, 115, 245)]
G[")"] = [A(-150, 330, 260, 480, 65, -65)]
G["["] = [L((170, 780), (0, 780), (0, -120), (170, -120))]
G["]"] = [L((0, 780), (170, 780), (170, -120), (0, -120))]
G["!"] = [L((0, CAP), (0, 220)), dot(0, 0)]
G["?"] = [A(190, 520, 190, 180, 165, -70) + L((190, 290), (190, 210)), dot(190, 0)]
G["'"] = [L((0, CAP), (0, 520))]
G['"'] = [L((0, CAP), (0, 520)), L((170, CAP), (170, 520))]
G["#"] = [L((150, CAP), (70, 0)), L((400, CAP), (320, 0)), L((30, 470), (470, 470)), L((0, 230), (440, 230))]
G["%"] = [A(105, 560, 105, 140, 0, 360), A(455, 140, 105, 140, 0, 360), L((70, 0), (490, CAP))]
G["*"] = [L((150, 660), (150, 380)), L((30, 590), (270, 450)), L((270, 590), (30, 450))]
G["<"] = [L((340, 540), (0, 320), (340, 100))]
G[">"] = [L((0, 540), (340, 320), (0, 100))]
G["&"] = [L((430, 0), (130, 470)) + A(230, 560, 110, 140, 215, -35) + L((40, 250)) + A(200, 170, 180, 170, 150, 380)]
G["@"] = [A(330, 300, 130, 150, 0, 360), L((460, 450), (460, 220)) + A(540, 220, 80, 80, 180, 360)
          + A(330, 300, 290, 330, 0, 320)]

ACCENTS = {"á": "a", "é": "e", "í": "i", "ó": "o", "ú": "u", "Á": "A", "É": "E", "Í": "I", "Ó": "O", "Ú": "U"}
G["ñ"] = G["n"] + [L((70, 650), (150, 700), (230, 650), (310, 700))]
G["Ñ"] = G["N"] + [L((100, 850), (180, 900), (260, 850), (340, 900))]
for accented, base in ACCENTS.items():
    strokes = [s for s in G[base] if not (base == "i" and len(s) == 2 and s[0][1] > 600)]
    xs = [p[0] for s in strokes for p in s]
    cx = (min(xs) + max(xs)) / 2
    top = 850 if base.isupper() else 640
    G[accented] = strokes + [L((cx - 30, top), (cx + 70, top + 110))]


def outline(strokes):
    shapes = []
    for s in strokes:
        shapes.append(LineString(s).buffer(STROKE / 2, resolution=6, cap_style=1, join_style=1))
    return unary_union(shapes)


def build():
    glyph_order = [".notdef", "space"]
    cmap = {32: "space", 0xA0: "space"}
    glyphs, metrics = {}, {}

    pen = TTGlyphPen(None)
    for pts in ([(60, 0), (60, 700), (440, 700), (440, 0)], [(130, 70), (370, 70), (370, 630), (130, 630)]):
        pen.moveTo(pts[0])
        for p in pts[1:]:
            pen.lineTo(p)
        pen.closePath()
    glyphs[".notdef"], metrics[".notdef"] = pen.glyph(), (500, 60)
    glyphs["space"], metrics["space"] = TTGlyphPen(None).glyph(), (300, 0)

    digit_width = 420 + 2 * SIDE
    for char, strokes in G.items():
        xs = [p[0] for s in strokes for p in s]
        lo, hi = min(xs), max(xs)
        if char in DIGITS:
            shift, advance = SIDE + (420 - (hi - lo)) / 2 - lo, digit_width
        else:
            shift, advance = SIDE - lo, (hi - lo) + 2 * SIDE

        shape = outline([[(x + shift, y) for x, y in s] for s in strokes])
        polys = list(shape.geoms) if isinstance(shape, MultiPolygon) else [shape]
        pen = TTGlyphPen(None)
        for poly in polys:
            poly = orient(poly.simplify(1.5), sign=-1.0)   # TrueType: exterior en sentido horario
            for ring in [poly.exterior] + list(poly.interiors):
                pts = [(round(x), round(y)) for x, y in list(ring.coords)[:-1]]
                pen.moveTo(pts[0])
                for p in pts[1:]:
                    pen.lineTo(p)
                pen.closePath()

        name = "uni%04X" % ord(char)
        glyph_order.append(name)
        cmap[ord(char)] = name
        glyphs[name] = pen.glyph()
        metrics[name] = (round(advance), round(SIDE - STROKE / 2))

    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=930, descent=-260)
    fb.setupNameTable({"familyName": FAMILY, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=930, sTypoDescender=-260, sTypoLineGap=0, usWinAscent=1000, usWinDescent=300,
                sxHeight=XH, sCapHeight=CAP)
    fb.setupPost()

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets", "Sampler5Sans.ttf")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    fb.save(out)
    print("guardado", os.path.normpath(out), "-", len(glyph_order), "glifos")


if __name__ == "__main__":
    build()
