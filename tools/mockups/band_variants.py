"""Mockups: variations on look A from battery_wave.py, the light band that
travels through the battery fill (up = charging, down = discharging, speed
from the power).

Every variant is a brightness profile along the direction of travel: alpha as
a function of s, the px behind the crest (0 = crest, PERIOD = the next crest).
Positive alpha lightens the fill, negative darkens it. Optional: a tilt, and
"sweep" (one band per pass through the fill instead of a fixed period).

  A        the original: a 6 px ramp up to the crest, a 32 px tail, every 48 px
  glint    a thin bright crest with a short tail
  glow     a wide, soft, symmetric band
  sawtooth tail fills the whole period, so the fill is never plain
  swell    light crest, then a shallow dark trough, like a wave with a shadow
  sweep    one long band per pass through the whole fill
  sheen    A, tilted, like light catching glass

Writes overview.webp (rows = flow states, columns = variants) and
home-<variant>.webp (full screen, charging).
Usage: python band_variants.py <out_dir>
"""
import sys, os, math
from PIL import Image
from battery_wave import *


def fit_period(v, p):
    """Nudges period p so v * LOOP_S is a whole number of periods (seamless loop)."""
    travel = abs(v) * LOOP_S
    return travel / max(1, round(travel / p))


def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


# ── profiles: (s, period) -> alpha, s = px behind the crest ──────────────
def crest_tail(lead, trail, peak, power=2):
    def f(s, p):
        if s <= trail:
            return peak * (1 - s / trail) ** power
        if s > p - lead:
            return peak * smooth((p - s) / lead)
        return 0
    return f


def glow(width, peak):
    def f(s, p):
        d = min(s, p - s)   # distance from the crest, either side
        return peak * math.cos(math.pi * d / width) ** 2 if d < width / 2 else 0
    return f


def sawtooth(peak, lead=4):
    def f(s, p):
        return peak * smooth((p - s) / lead) if s > p - lead else peak * (1 - s / (p - lead))
    return f


def swell(peak, trail, trough, depth):
    light = crest_tail(6, trail, peak)
    def f(s, p):
        if trail * 0.6 < s < trail * 0.6 + trough:
            return -depth * math.sin(math.pi * (s - trail * 0.6) / trough)
        return light(s, p)
    return f


def band(profile, period=48, slope=0.0, sweep_gap=None):
    """A look for battery(fill_fx=look(v, t)). sweep_gap: period = fill
    height + gap, so one band at a time passes right through."""
    def look(v, t):
        def fx(c, x0, y0, x1, y1):
            if v == 0:
                return
            w, h = x1 - x0, y1 - y0
            p = fit_period(v, h + sweep_gap if sweep_gap is not None else period)
            off = v * t
            m = abs(slope) * w / 2               # extra rows the tilt reaches into
            rows = int((h + 2 * m) * K)
            lite, dark = [], []
            for i in range(rows):
                u = (i / K - m + off) % p
                a = profile(u if v > 0 else (-u) % p, p)
                lite.append(int(255 * max(0, a)))
                dark.append(int(255 * max(0, -a)))
            wk, hk = int(w * K), int(h * K)
            layer = Image.new("RGBA", (wk, hk), (0, 0, 0, 0))
            for col, alpha in ((FX, lite), ((0, 0, 0), dark)):
                if not any(alpha):
                    continue
                a = Image.new("L", (1, rows))
                a.putdata(alpha)
                a = a.resize((wk, rows), Image.NEAREST)
                # shear: output (x, y) samples row y + m + slope * (x - w/2)
                a = a.transform((wk, hk), Image.AFFINE, (1, 0, 0, slope, 1, m * K - slope * wk / 2),
                                resample=Image.BILINEAR)
                part = Image.new("RGBA", (wk, hk), col + (0,))
                part.putalpha(a)
                layer.alpha_composite(part)
            composite_clipped(c, x0, y0, layer)
        return fx
    return look


VARIANTS = [
    ("A",        band(crest_tail(6, 32, 0.45))),
    ("glint",    band(crest_tail(2, 12, 0.75), period=36)),
    ("glow",     band(glow(40, 0.32), period=64)),
    ("sawtooth", band(sawtooth(0.32), period=40)),
    ("swell",    band(swell(0.45, 20, 24, 0.16), period=56)),
    ("sweep",    band(crest_tail(8, 56, 0.5), sweep_gap=24)),
    ("sheen",    band(crest_tail(6, 32, 0.45), slope=-0.45)),
]


def overview(path, variants=VARIANTS, states=STATES):
    """Battery crops: one row per flow state, one column per variant. A
    variant's look may also be a function of soc returning the look."""
    bx0, by0, bx1, by1 = BATT_X - 12, BATT_Y - 18, BATT_X + BATT_W + 12, BATT_Y + BATT_H + 12
    cw, ch = (bx1 - bx0) * OUT, (by1 - by0) * OUT
    pad, top, left = 20, 52, 190
    base = Image.new("RGB", (left + len(variants) * (cw + pad), top + len(states) * (ch + pad)), (30, 32, 38))
    d = ImageDraw.Draw(base)
    f, fs = ImageFont.truetype(MONT["sb"], 26), ImageFont.truetype(MONT["r"], 18)
    for j, (name, _) in enumerate(variants):
        d.text((left + j * (cw + pad) + cw / 2, 14), name, font=f, fill=(220, 225, 235), anchor="mt")
    for i, (soc, kw) in enumerate(states):
        y = top + i * (ch + pad) + ch / 2
        d.text((pad, y - 14), f"{kw:+.1f} kW {soc}%", font=f, fill=(220, 225, 235), anchor="lm")
        d.text((pad, y + 16), f"{abs(speed(kw)):.0f} px/s {'up' if kw > 0 else 'down'}",
               font=fs, fill=(150, 158, 175), anchor="lm")

    c = Canvas()
    out = []
    for t in frames():
        im = base.copy()
        for j, (_, look) in enumerate(variants):
            for i, (soc, kw) in enumerate(states):
                c.d.rectangle(c.p(bx0, by0, bx1, by1), fill=BG)
                fx = look(soc) if getattr(look, "per_soc", False) else look
                battery(c, soc, fill_fx=fx(speed(kw), t))
                cell = c.img.crop(tuple(c.p(bx0, by0, bx1, by1))).convert("RGB").resize((cw, ch), Image.LANCZOS)
                im.paste(cell, (left + j * (cw + pad), top + i * (ch + pad)))
        out.append(im)
    save_anim(out, path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    files = [P("overview.webp")]
    overview(files[0])
    charging = dict(pv=5.6, grid=-0.4, ess=3.2, soc=68, clock="13:05")
    for name, look in VARIANTS:
        files.append(P(f"home-{name}.webp"))
        home_anim(files[-1], look, **charging)
    print("\n".join(files))
