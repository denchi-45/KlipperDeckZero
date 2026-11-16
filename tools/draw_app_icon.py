"""Draws the app icon (the CoreXY box printer of assets/images/klipperdeck.png) at store size.

klipperdeck.png, klipperdeck_100.png and klipperdeck_80.png are the hand-tuned launcher sizes.
The store needs a square PNG of 128 to 512 px, so this redraws the same design as shapes on the
100-unit grid of those files, at 2048 px, and scales it down.

Usage: python tools/draw_app_icon.py [output dir]   (default: assets/images)
Needs Pillow.
"""
import sys

from PIL import Image, ImageDraw

OUT = sys.argv[1] if len(sys.argv) > 1 else "assets/images"
S = 2048
k = S / 100.0

TOP_BAND = (158, 173, 190)
TOP_EDGE = (172, 185, 200)
BODY = (226, 233, 240)
OUTLINE = (103, 110, 118)
WINDOW = (19, 26, 35)
BELT = (120, 134, 155)
RAIL = (240, 242, 246)
ORANGE = (255, 122, 26)
TEAL = (48, 207, 186)
TEAL_DARK = (32, 116, 110)
BED = (247, 247, 250)
PANEL = (21, 26, 35)
SCREEN = (45, 213, 192)


def p(*xy):
    return [v * k for v in xy]


img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)


def rrect(box, r, fill, corners=None, outline=None, width=0):
    d.rounded_rectangle(p(*box), radius=r * k, fill=fill, corners=corners,
                        outline=outline, width=int(round(width * k)))


def line(a, b, w, fill):
    d.line(p(*a, *b), fill=fill, width=int(round(w * k)))
    for x, y in (a, b):
        r = w / 2
        d.ellipse(p(x - r, y - r, x + r, y + r), fill=fill)


# Enclosure: small radius on top, large at the bottom.
rrect((0, 0, 100, 100), 14, BODY, corners=(False, False, True, True))
rrect((0, 0, 100, 30), 3.5, BODY, corners=(True, True, False, False))
# Top band with a lighter lower edge.
rrect((0, 0, 100, 12), 3.5, TOP_BAND, corners=(True, True, False, False))
d.rectangle(p(0, 11, 100, 12), fill=TOP_EDGE)

# Front window.
rrect((10.3, 15.8, 89.7, 64.6), 3.2, WINDOW, outline=OUTLINE, width=0.7)

# CoreXY belts (crossing behind the print head).
line((14.5, 19.8), (48, 31.5), 2.2, BELT)
line((85.5, 19.8), (52, 31.5), 2.2, BELT)
line((14.5, 44.2), (48, 32.5), 2.2, BELT)
line((85.5, 44.2), (52, 32.5), 2.2, BELT)

# Gantry rail.
rrect((14, 25.6, 86, 30), 1.4, RAIL)

# Print head: block, funnel and nozzle.
rrect((41, 22, 59, 31), 1.8, ORANGE)
def bezier(a, c, b, n=24):
    return [((1 - t) ** 2 * a[0] + 2 * (1 - t) * t * c[0] + t * t * b[0],
             (1 - t) ** 2 * a[1] + 2 * (1 - t) * t * c[1] + t * t * b[1])
            for t in (i / n for i in range(n + 1))]


right = bezier((59, 30.5), (54.6, 33.6), (51.5, 40.6))
left = [(100 - x, y) for x, y in reversed(right)]
d.polygon([v * k for pt in right + left for v in pt], fill=ORANGE)
rrect((48.7, 42, 51.3, 50.2), 0.6, ORANGE)

# Printed object: two teal layers with darker edges.
rrect((39.4, 50.3, 60.6, 55.1), 0.8, TEAL_DARK)
rrect((39.9, 50.8, 60.1, 54.6), 0.6, TEAL)
rrect((39.4, 55.8, 60.6, 59.9), 0.8, TEAL_DARK)
rrect((39.9, 56.3, 60.1, 59.6), 0.6, TEAL)

# Bed.
rrect((14, 59.8, 86, 63.1), 1.4, BED)

# Control panel with a screen bar, and the status dot.
rrect((10.4, 70.3, 41.3, 80.2), 1.8, PANEL, outline=OUTLINE, width=0.7)
rrect((13.2, 73.6, 30, 77.4), 1.9, SCREEN)
d.ellipse(p(73.7, 70.6, 83.7, 80.6), fill=ORANGE)

img.resize((256, 256), Image.LANCZOS).save(f"{OUT}/klipperdeck_256.png", optimize=True)
print(f"wrote {OUT}/klipperdeck_256.png")
