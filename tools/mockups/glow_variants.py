"""Mockups: the "glow" band from band_variants.py, made easier to see on the
yellow (20-49%) and red (<20%) fills. Their gradients are much lighter, so the
same white glow that reads on green almost disappears. Green stays as it is.

  glow        as in band_variants.py, the same everywhere (peak 32%)
  glow+       yellow/red peak 50%
  glow++      yellow/red peak 65%, 52 px high, ONE wave at a time (period = fill
              height); the firmware's look, and it applies to green too
  glow+ edge  glow+ with faint dark shoulders either side, for contrast

Writes overview.webp and home-<variant>.webp (full screen, yellow, discharging).
Usage: python glow_variants.py <out_dir>
"""
import sys, os, math
from band_variants import *

GREEN = band(glow(40, 0.32), period=64)


def glow_edge(width, peak, shoulder, depth):
    lit = glow(width, peak)
    def f(s, p):
        d = min(s, p - s)
        if width / 2 <= d < width / 2 + shoulder:
            return -depth * math.sin(math.pi * (d - width / 2) / shoulder)
        return lit(s, p)
    return f


def by_colour(other):
    """Green fill (soc >= 50) keeps the plain glow; yellow and red get `other`."""
    f = lambda soc: GREEN if soc >= 50 else other
    f.per_soc = True
    return f


GLOWS = [
    ("glow",       GREEN),
    ("glow+",      by_colour(band(glow(40, 0.50), period=64))),
    ("glow++",     band(glow(52, 0.65), sweep_gap=0)),
    ("glow+ edge", by_colour(band(glow_edge(36, 0.50, 12, 0.12), period=64))),
]
GLOW_STATES = [(76, 4.5), (45, 0.6), (32, -1.5), (18, -0.9)]   # green, yellow, yellow, red


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    files = [P("overview.webp")]
    overview(files[0], GLOWS, GLOW_STATES)
    evening = dict(pv=0.0, grid=0.2, ess=-1.5, soc=32, clock="19:40")
    for name, look in GLOWS:
        files.append(P(f"home-{name.replace(' ', '-')}.webp"))
        home_anim(files[-1], look(evening["soc"]) if getattr(look, "per_soc", False) else look, **evening)
    print("\n".join(files))
