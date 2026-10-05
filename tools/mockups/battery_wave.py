"""Mockups: an animation inside the battery fill showing the flow. It travels
up while charging and down while discharging, faster the more power flows.
Idle (|ess| < 0.05 kW) stays still. Three looks:

  A  band      a soft light band with a crest at the front and a long tail
  B  chevrons  a column of faint ^ (charging) or v (discharging)
  C  ripples   horizontal sine lines, like ripples in liquid

Speed: SPEED_BASE + SPEED_PER_KW * |kW| px/s, capped at SPEED_MAX. The mockups
round it to a multiple of PERIOD / LOOP_S so each animation loops seamlessly.

Writes animated WebPs: overview.webp (rows = looks, columns = flow states) and
home-<look>-charging/discharging.webp (full screen).
Usage: python battery_wave.py <out_dir>
"""
import sys, os, math
from PIL import Image, ImageChops
from dashboard import *

SPEED_BASE, SPEED_PER_KW, SPEED_MAX = 12, 16, 90   # px/s
PERIOD = 48          # px; the pattern repeats every PERIOD px
LOOP_S, FPS = 6, 25  # loop length and frame rate (LVGL refreshes at ~30 fps)
FX = (255, 255, 255)


def speed(kw):
    """Pixels per second the pattern travels, positive = up (charging)."""
    if abs(kw) < 0.05:
        return 0
    v = min(SPEED_MAX, SPEED_BASE + SPEED_PER_KW * abs(kw))
    step = PERIOD / LOOP_S
    v = max(step, round(v / step) * step)   # whole periods per loop
    return math.copysign(v, kw)


def clipped(c, x0, y0, x1, y1, draw_fn):
    """Draws draw_fn(layer_draw, w, h) on a transparent layer the size of the
    fill (supersampled px) and composites it with composite_clipped()."""
    w, h = int((x1 - x0) * K), int((y1 - y0) * K)
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(layer), w, h)
    composite_clipped(c, x0, y0, layer)


def composite_clipped(c, x0, y0, layer):
    """Clips an RGBA layer the size of the fill to the fill's rounded rect,
    fades it in near the top and bottom edges, and composites it on."""
    w, h = layer.size
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], 12 * K, fill=255)
    fade = Image.new("L", (1, h))
    edge = min(10 * K, h / 5)   # shorter on a low fill, or it fades the whole band out
    for y in range(h):
        d = min(y, h - 1 - y)
        fade.putpixel((0, y), int(255 * min(1, d / edge)))
    mask = ImageChops.multiply(mask, fade.resize((w, h)))
    layer.putalpha(ImageChops.multiply(layer.getchannel("A"), mask))
    c.img.alpha_composite(layer, (int(x0 * K), int(y0 * K)))


def phase(v, t):
    """Pattern offset in px at time t; content at fill y shows pattern (y + offset)."""
    return v * t


def fx_band(v, t):
    """A: one band per PERIOD: a short ramp up to a crest, then a long tail behind it."""
    def fx(c, x0, y0, x1, y1):
        off = phase(v, t)
        lead, trail, peak = 6, 32, 0.45

        def draw(d, w, h):
            for yk in range(h):
                u = (yk / K + off) % PERIOD
                s = u if v > 0 else (-u) % PERIOD   # px behind the crest
                if s <= trail:
                    a = peak * (1 - s / trail) ** 2
                elif s > PERIOD - lead:
                    x = (PERIOD - s) / lead       # 1 just ahead of the crest, 0 at lead px
                    a = peak * x * x * (3 - 2 * x)
                else:
                    continue
                d.line([(0, yk), (w, yk)], fill=FX + (int(255 * a),))
        clipped(c, x0, y0, x1, y1, draw)
    return fx


def fx_chevrons(v, t):
    """B: chevrons every PERIOD / 2 px, pointing the way the energy flows."""
    def fx(c, x0, y0, x1, y1):
        off = phase(v, t)
        gap, half, rise, stroke = PERIOD / 2, 16, 8, 4.5
        up = v > 0

        def draw(d, w, h):
            cx = w / 2
            n0 = math.floor(off / gap) - 1   # first row at or above fill y = 0
            for n in range(n0, n0 + int(h / K / gap) + 3):
                yc = (n * gap - off) * K          # pattern y = n*gap at fill y = n*gap - off
                tip, base = (yc - rise * K / 2, yc + rise * K / 2) if up else (yc + rise * K / 2, yc - rise * K / 2)
                pts = [(cx - half * K, base), (cx, tip), (cx + half * K, base)]
                d.line(pts, fill=FX + (90,), width=int(stroke * K), joint="curve")
                for px, py in (pts[0], pts[2]):
                    r = stroke * K / 2
                    d.ellipse([px - r, py - r, px + r, py + r], fill=FX + (90,))
        clipped(c, x0, y0, x1, y1, draw)
    return fx


def fx_ripples(v, t):
    """C: sine lines every PERIOD / 2 px across the fill."""
    def fx(c, x0, y0, x1, y1):
        off = phase(v, t)
        gap, amp, wl, stroke = PERIOD / 2, 3, 47, 3

        def draw(d, w, h):
            n0 = math.floor(off / gap) - 1   # first row at or above fill y = 0
            for n in range(n0, n0 + int(h / K / gap) + 3):
                yc = (n * gap - off) * K
                pts = [(x, yc + amp * K * math.sin(2 * math.pi * x / (wl * K))) for x in range(0, w + 8, 8)]
                d.line(pts, fill=FX + (80,), width=int(stroke * K), joint="curve")
        clipped(c, x0, y0, x1, y1, draw)
    return fx


LOOKS = [("A band", fx_band), ("B chevrons", fx_chevrons), ("C ripples", fx_ripples)]
STATES = [(45, 0.6), (76, 4.5), (61, -1.8), (18, -0.9)]   # (soc %, ess kW)


def frames():
    n = LOOP_S * FPS
    return [i / FPS for i in range(n)]


def save_anim(imgs, path):
    """Animated WebP: full colour (a GIF's 256 colours band the soft fills)."""
    imgs[0].save(path, save_all=True, append_images=imgs[1:], duration=1000 // FPS, loop=0,
                 quality=90, method=4)


def overview(path):
    """Battery crops: one row per look, one column per flow state."""
    bx0, by0, bx1, by1 = BATT_X - 12, BATT_Y - 18, BATT_X + BATT_W + 12, BATT_Y + BATT_H + 12
    cw, ch = (bx1 - bx0) * OUT, (by1 - by0) * OUT
    pad, top, left = 20, 64, 180
    sheet_w = left + len(STATES) * (cw + pad)
    sheet_h = top + len(LOOKS) * (ch + pad)
    base = Image.new("RGB", (sheet_w, sheet_h), (30, 32, 38))
    d = ImageDraw.Draw(base)
    f, fs = ImageFont.truetype(MONT["sb"], 24), ImageFont.truetype(MONT["r"], 18)
    for j, (soc, kw) in enumerate(STATES):
        x = left + j * (cw + pad)
        d.text((x + cw / 2, 14), f"{kw:+.1f} kW  {soc}%", font=f, fill=(220, 225, 235), anchor="mt")
        d.text((x + cw / 2, 42), f"{abs(speed(kw)):.0f} px/s {'up' if kw > 0 else 'down'}",
               font=fs, fill=(150, 158, 175), anchor="mt")
    for i, (name, _) in enumerate(LOOKS):
        d.text((pad, top + i * (ch + pad) + ch / 2), name, font=f, fill=(220, 225, 235), anchor="lm")

    c = Canvas()
    out = []
    for t in frames():
        im = base.copy()
        for i, (_, look) in enumerate(LOOKS):
            for j, (soc, kw) in enumerate(STATES):
                c.d.rectangle(c.p(bx0, by0, bx1, by1), fill=BG)
                battery(c, soc, fill_fx=look(speed(kw), t))
                cell = c.img.crop(tuple(c.p(bx0, by0, bx1, by1))).convert("RGB").resize((cw, ch), Image.LANCZOS)
                im.paste(cell, (left + j * (cw + pad), top + i * (ch + pad)))
        out.append(im)
    save_anim(out, path)


def home_anim(path, look, pv, grid, ess, soc, clock):
    """Full screen: the static dashboard once, the battery redrawn per frame."""
    c = home_canvas(pv, grid, ess, soc, clock)
    region = c.p(BATT_X - 4, BATT_Y - 12, BATT_X + BATT_W + 4, BATT_Y + BATT_H + 4)
    out = []
    for t in frames():
        c.d.rectangle(region, fill=BG)
        battery(c, soc, fill_fx=look(speed(ess), t))
        out.append(c.img.convert("RGB").resize((W * OUT, H * OUT), Image.LANCZOS))
    save_anim(out, path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    files = [P("overview.webp")]
    overview(files[0])
    charging = dict(pv=5.6, grid=-0.4, ess=3.2, soc=68, clock="13:05")
    discharging = dict(pv=0.0, grid=0.1, ess=-1.6, soc=61, clock="19:40")
    for name, look in LOOKS:
        tag = name.replace(" ", "-")
        for flow, state in (("charging", charging), ("discharging", discharging)):
            files.append(P(f"home-{tag}-{flow}.webp"))
            home_anim(files[-1], look, **state)
    print("\n".join(files))
