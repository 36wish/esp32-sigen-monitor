"""Mockups: variations on storm_icon.py's look B, a solid bolt under the
thundercloud: bolder, bigger, tucked into the cloud, more upright, a smaller
cloud, and rounded corners. Close-ups off and on, plus the full home screen.

Usage: python storm_b_variants.py <out_dir>
"""
import sys, os
from storm_icon import *


def cloud_scaled(c, P, k, col, f=1.0, dy=0):
    """The thundercloud scaled by f about its middle (16, 9), then moved down dy."""
    T = lambda x, y: P(16 + (x - 16) * f, 9 + (y - 9) * f + dy)
    for x, y, r in ((10, 11, 5), (18, 8.5, 7), (24, 12, 4.5)):
        c.circle(*T(x, y), r * k * f, fill=col)
    x0, y0 = T(4, 11); x1, y1 = T(28.5, 17)
    c.rrect(x0, y0, x1, y1, 3 * k * f, fill=col)


def bolt_look(pts, cloud_f=1.0, cloud_dy=-1, round_r=0):
    """round_r > 0 strokes the outline with round joins, softening the corners
    and widening the bolt by round_r all round."""
    def look(c, cx, cy, s, col, bg=None):
        P, k = grid(cx, cy, s)
        cloud_scaled(c, P, k, col, cloud_f, cloud_dy)
        poly = [P(*p) for p in pts]
        polygon(c, poly, col)
        for a, b in zip(poly, poly[1:] + poly[:1]) if round_r else ():
            c.line([a, b], col, 2 * round_r * k)   # edge by edge: curved joins spike at the tip
    return look


VARIANTS = [
    ("B", bolt_look(BOLT_B)),
    ("B1-bold", bolt_look([(12.5, 17.5), (23.5, 17.5), (19.5, 22.5), (25.5, 22.5),
                           (11, 33.5), (14.5, 26.5), (7.5, 26.5)])),
    ("B2-big", bolt_look([(13.5, 15), (23, 15), (19, 21.5), (25, 21.5),
                          (10.5, 34), (14.5, 25.5), (8, 25.5)], cloud_dy=-3)),
    ("B3-tucked", bolt_look([(14, 12), (22.5, 12), (19, 20.5), (24.5, 20.5),
                             (11.5, 33), (15, 24), (9, 24)])),
    ("B4-upright", bolt_look([(14, 17.5), (21.5, 17.5), (19.5, 23), (24.5, 23),
                              (14, 34), (15.5, 26.5), (10.5, 26.5)])),
    ("B5-small-cloud", bolt_look([(13, 15), (23, 15), (19, 21.5), (25.5, 21.5),
                                  (10.5, 34), (14.5, 25.5), (7.5, 25.5)], cloud_f=0.8, cloud_dy=-2)),
    ("B6-soft", bolt_look([(14.5, 18), (22, 18), (18.5, 23.5), (23.5, 23.5),
                           (12, 32), (15.5, 26), (10, 26)], round_r=1)),
]


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    files = []
    for name, look in VARIANTS:
        for on in (False, True):
            files.append(P(f"{name}{'-on' if on else ''}.png"))
            screen(look, on).save(files[-1])
    closeups(P("closeups.png"), VARIANTS)
    sheet([f for f in files if not f.endswith("-on.png")], P("overview.png"))
    print("\n".join(files + [P("closeups.png")]))
