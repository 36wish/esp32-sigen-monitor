"""Mockups: thicker lightning bolt on the Charge button's thundercloud.
Each look is drawn on the 32px grid of makeStormIcon() (D is 32x44), off and
pressed in, as button close-ups and on the full home screen.

Usage: python storm_icon.py <out_dir>
"""
import sys, os
from PIL import Image, ImageDraw
import dashboard as D
from dashboard import *


def grid(cx, cy, s, h=32):
    k = s / 32
    return lambda x, y: (cx + (x - 16) * k, cy + (y - h / 2) * k), k


def cloud(c, P, k, col, dy=0):
    for x, y, r in ((10, 11, 5), (18, 8.5, 7), (24, 12, 4.5)):
        c.circle(*P(x, y + dy), r * k, fill=col)
    x0, y0 = P(4, 11 + dy); x1, y1 = P(28.5, 17 + dy)
    c.rrect(x0, y0, x1, y1, 3 * k, fill=col)


def polygon(c, pts, col):
    c.d.polygon([(x * K, y * K) for x, y in pts], fill=col)


def knockout(c, pts, col, w):
    """Outline the bolt in the button colour so it reads where it crosses the cloud."""
    ring = [(x * K, y * K) for x, y in pts + pts[:1]]
    c.d.line(ring, fill=col, width=int(w * K * 2), joint="curve")
    for x, y in pts:
        c.circle(x, y, w, fill=col)


def now(c, cx, cy, s, col, bg=None):
    """Current: 3px zigzag line under the cloud."""
    P, k = grid(cx, cy, s)
    cloud(c, P, k, col)
    c.line([P(18, 19), P(12, 25), P(19, 25), P(13, 31)], col, 3 * k)


def a_line5(c, cx, cy, s, col, bg=None):
    """A: same zigzag, 5px line."""
    P, k = grid(cx, cy, s)
    cloud(c, P, k, col, dy=-1)
    c.line([P(18.5, 18.5), P(12, 24.5), P(20, 24.5), P(13.5, 30.5)], col, 5 * k)


BOLT_B = [(14, 17.5), (22.5, 17.5), (19, 23), (24.5, 23), (11.5, 33), (15, 26), (9, 26)]


def b_solid(c, cx, cy, s, col, bg=None):
    """B: solid filled bolt, under the cloud."""
    P, k = grid(cx, cy, s)
    cloud(c, P, k, col, dy=-1)
    polygon(c, [P(*p) for p in BOLT_B], col)


BOLT_C = [(14, 9), (23, 9), (19, 18), (25, 18), (11, 33), (14.5, 22.5), (8.5, 22.5)]


def c_through(c, cx, cy, s, col, bg):
    """C: bigger solid bolt that starts inside the cloud, cut out of it."""
    P, k = grid(cx, cy, s)
    cloud(c, P, k, col, dy=-2)
    pts = [P(*p) for p in BOLT_C]
    knockout(c, pts, bg, 1.6 * k)
    polygon(c, pts, col)


BOLT_D = [(14.5, 19), (22, 19), (18.5, 27), (24, 27), (11, 43), (15, 31), (9.5, 31)]


def d_tall(c, cx, cy, s, col, bg=None):
    """D: icon 32x44, using more of the tall button: a long solid bolt."""
    P, k = grid(cx, cy, s, h=44)
    cloud(c, P, k, col, dy=0)
    polygon(c, [P(*p) for p in BOLT_D], col)


LOOKS = [("now", now), ("A-line5", a_line5), ("B-solid", b_solid),
         ("C-through", c_through), ("D-tall", d_tall)]


def btn_bg(on):
    """Button colour at the icon's centre (half way down the gradient)."""
    if on:
        return mix(mix(STORM, BG, 0.18), mix(STORM, BG, 0.50), 0.5)
    return mix(mix(TEXT, CARD2, 0.07), CARD2, 0.5)


def screen(look, on):
    D.icon_storm = lambda c, cx, cy, s, col, bg=None: look(c, cx, cy, s, col, btn_bg(on))
    st = dict(pv=4.2, grid=1.7, ess=3.5, soc=100, clock="14:20") if on else \
         dict(pv=4.2, grid=-1.3, ess=0.5, soc=76, clock="14:20")
    return home_canvas(**st, charge_on=on)


def closeups(out, looks=LOOKS):
    """Every look off and on, cropped from the 4x canvas around the button."""
    x0, y0, x1, y1 = (MODE_BTN_X - 10) * K, (CHARGE_BTN_Y - 8) * K, \
                     (MODE_BTN_X + MODE_BTN_W + 5) * K, (CHARGE_BTN_Y + MODE_BTN_H + 12) * K
    cw, ch = x1 - x0, y1 - y0
    pad, lab = 30, 60
    S = Image.new("RGB", (len(looks) * (2 * cw + pad) + pad * 2, ch + lab + 2 * pad), (30, 32, 38))
    d = ImageDraw.Draw(S)
    f = R.font(26 / K, "sb")
    for i, (name, look) in enumerate(looks):
        x = pad + i * (2 * cw + pad)
        d.text((x, pad), name, font=f, fill=(220, 225, 235))
        for j, on in enumerate((False, True)):
            crop = screen(look, on).img.crop((x0, y0, x1, y1)).convert("RGB")
            S.paste(crop, (x + j * cw, pad + lab))
    S.save(out)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    files = []
    for name, look in LOOKS:
        for on in (False, True):
            files.append(P(f"{name}{'-on' if on else ''}.png"))
            screen(look, on).save(files[-1])
    closeups(P("closeups.png"))
    sheet([f for f in files if not f.endswith("-on.png")], P("overview.png"))
    print("\n".join(files + [P("closeups.png")]))
