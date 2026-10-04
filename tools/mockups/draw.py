"""Render four redesign concepts for the 480x320 Sigenergy dashboard.
Drawn at 4x and downsampled to 2x (960x640) for anti-aliasing."""
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 480, 320
K = 4            # supersample factor
OUT = 2          # output scale
FONTS = "C:/Windows/Fonts/"

# sample reading: 4.2 kW solar, 1.3 kW export, battery charging 0.5 kW at 76%
PV, GRID, ESS, SOC = 4.2, -1.3, 0.5, 76
HOME = PV + GRID - ESS

BG      = (8, 12, 22)
CARD    = (17, 24, 40)
CARD2   = (22, 31, 51)
BORDER  = (32, 44, 68)
TEXT    = (238, 243, 251)
MUTED   = (128, 145, 178)
DIM     = (70, 84, 112)
SOLAR   = (251, 191, 36)
HOMEC   = (167, 139, 250)
BATT    = (52, 211, 153)
EXPORT  = (52, 211, 153)
IMPORT  = (248, 113, 113)
DISCH   = (96, 165, 250)


def font(px, w="r"):
    name = {"r": "segoeui.ttf", "b": "segoeuib.ttf", "l": "segoeuil.ttf",
            "sb": "seguisb.ttf"}[w]
    try:
        return ImageFont.truetype(FONTS + name, int(px * K))
    except OSError:
        return ImageFont.truetype(FONTS + "segoeuib.ttf", int(px * K))


class Canvas:
    def __init__(self, bg=BG):
        self.img = Image.new("RGBA", (W * K, H * K), bg + (255,))
        self.d = ImageDraw.Draw(self.img)

    # geometry helpers take logical px
    def p(self, *v):
        return [x * K for x in v]

    def rrect(self, x0, y0, x1, y1, r, fill=None, outline=None, width=1):
        self.d.rounded_rectangle(self.p(x0, y0, x1, y1), r * K, fill=fill,
                                 outline=outline, width=int(width * K) if outline else 0)

    def grad_rrect(self, x0, y0, x1, y1, r, c0, c1, vertical=True, outline=None):
        w, h = int((x1 - x0) * K), int((y1 - y0) * K)
        g = Image.new("RGBA", (w, h))
        gd = ImageDraw.Draw(g)
        n = h if vertical else w
        for i in range(n):
            t = i / max(1, n - 1)
            c = tuple(int(c0[j] + (c1[j] - c0[j]) * t) for j in range(len(c0)))
            if len(c) == 3:
                c = c + (255,)
            if vertical:
                gd.line([(0, i), (w, i)], fill=c)
            else:
                gd.line([(i, 0), (i, h)], fill=c)
        m = Image.new("L", (w, h), 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], r * K, fill=255)
        self.img.paste(g, (int(x0 * K), int(y0 * K)), m)
        if outline:
            self.rrect(x0, y0, x1, y1, r, outline=outline, width=1)

    def circle(self, cx, cy, r, fill=None, outline=None, width=1):
        self.d.ellipse(self.p(cx - r, cy - r, cx + r, cy + r), fill=fill, outline=outline,
                       width=int(width * K) if outline else 0)

    def arc(self, cx, cy, r, a0, a1, width, color, caps=True):
        """Angles in degrees, 0 = 3 o'clock, clockwise."""
        if a1 <= a0:
            return
        self.d.arc(self.p(cx - r, cy - r, cx + r, cy + r), a0, a1, fill=color,
                   width=int(width * K))
        if caps:
            rm = r - width / 2
            for a in (a0, a1):
                x = cx + rm * math.cos(math.radians(a))
                y = cy + rm * math.sin(math.radians(a))
                self.circle(x, y, width / 2, fill=color)

    def line(self, pts, color, width=2, caps=True):
        self.d.line([(x * K, y * K) for x, y in pts], fill=color, width=int(width * K),
                    joint="curve")
        if caps:
            for x, y in (pts[0], pts[-1]):
                self.circle(x, y, width / 2, fill=color)

    def text(self, x, y, s, size, color, w="r", anchor="la", spacing=0):
        f = font(size, w)
        if spacing:
            # manual letter-spacing for small caps labels
            total = sum(self.d.textlength(ch, font=f) for ch in s) + spacing * K * (len(s) - 1)
            if anchor[0] == "m":
                x = x - total / K / 2
            elif anchor[0] == "r":
                x = x - total / K
            cx = x * K
            for ch in s:
                self.d.text((cx, y * K), ch, font=f, fill=color, anchor="l" + anchor[1])
                cx += self.d.textlength(ch, font=f) + spacing * K
            return
        self.d.text((x * K, y * K), s, font=f, fill=color, anchor=anchor)

    def textw(self, s, size, w="r"):
        return self.d.textlength(s, font=font(size, w)) / K

    def glow(self, draw_fn, radius=8, alpha=0.7):
        layer = Canvas(bg=(0, 0, 0))
        layer.img = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
        layer.d = ImageDraw.Draw(layer.img)
        draw_fn(layer)
        blurred = layer.img.filter(ImageFilter.GaussianBlur(radius * K))
        if alpha != 1:
            a = blurred.split()[3].point(lambda v: int(v * alpha))
            blurred.putalpha(a)
        self.img.alpha_composite(blurred)

    def save(self, path):
        out = self.img.convert("RGB").resize((W * OUT, H * OUT), Image.LANCZOS)
        out.save(path)


def mix(c, bg, t):
    return tuple(int(bg[i] + (c[i] - bg[i]) * t) for i in range(3))


# ── icons (vector, centred on cx,cy, size s) ────────────────────────────────
def icon_sun(c, cx, cy, s, col):
    c.circle(cx, cy, s * 0.28, fill=col)
    for i in range(8):
        a = math.radians(i * 45)
        c.line([(cx + math.cos(a) * s * 0.42, cy + math.sin(a) * s * 0.42),
                (cx + math.cos(a) * s * 0.58, cy + math.sin(a) * s * 0.58)], col, s * 0.09)


def icon_home(c, cx, cy, s, col):
    w = s * 0.5
    c.d.polygon([((cx - w * 1.1) * K, (cy - s * 0.02) * K), (cx * K, (cy - s * 0.52) * K),
                 ((cx + w * 1.1) * K, (cy - s * 0.02) * K)], fill=col)
    c.rrect(cx - w * 0.75, cy - s * 0.1, cx + w * 0.75, cy + s * 0.45, s * 0.05, fill=col)
    c.rrect(cx - s * 0.1, cy + s * 0.12, cx + s * 0.1, cy + s * 0.45, s * 0.03, fill=BG)


def icon_batt(c, cx, cy, s, col, level=None, bg=BG):
    w, h = s * 0.42, s * 0.8
    c.rrect(cx - s * 0.1, cy - h / 2 - s * 0.08, cx + s * 0.1, cy - h / 2 + 1, s * 0.03, fill=col)
    c.rrect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, s * 0.08, outline=col, width=s * 0.07)
    if level is not None:
        inset = s * 0.11
        top = cy + h / 2 - inset - (h - 2 * inset) * level
        c.rrect(cx - w / 2 + inset, top, cx + w / 2 - inset, cy + h / 2 - inset, s * 0.03, fill=col)


def icon_grid(c, cx, cy, s, col):
    # simple transmission tower
    t = s * 0.07
    top, bot = cy - s * 0.5, cy + s * 0.5
    c.line([(cx, top), (cx - s * 0.32, bot)], col, t)
    c.line([(cx, top), (cx + s * 0.32, bot)], col, t)
    c.line([(cx - s * 0.38, cy - s * 0.22), (cx + s * 0.38, cy - s * 0.22)], col, t)
    c.line([(cx - s * 0.3, cy + s * 0.05), (cx + s * 0.3, cy + s * 0.05)], col, t)
    c.line([(cx - s * 0.12, cy - s * 0.22), (cx + s * 0.2, cy + s * 0.05)], col, t * 0.8)
    c.line([(cx + s * 0.12, cy - s * 0.22), (cx - s * 0.2, cy + s * 0.05)], col, t * 0.8)


def icon_wifi(c, cx, cy, s, col):
    for r in (s * 0.25, s * 0.5, s * 0.75):
        c.arc(cx, cy + s * 0.35, r, 225, 315, s * 0.12, col)
    c.circle(cx, cy + s * 0.25, s * 0.09, fill=col)


def status_bar(c, y=14, left="SIGENERGY", show_time=True):
    c.text(14, y, left, 10, MUTED, "b", "lm", spacing=1.6)
    c.circle(14 + c.textw(left, 10, "b") + 1.6 * len(left) + 6, y, 3, fill=EXPORT)
    x = W - 14
    icon_wifi(c, x - 6, y - 2, 14, MUTED)
    if show_time:
        c.text(x - 20, y, "20:41", 12, MUTED, "sb", "rm")


# ════════════════════════════════════════════════════════════════════════════
# A — Power flow
# ════════════════════════════════════════════════════════════════════════════
def mock_flow(path):
    c = Canvas()
    # subtle radial vignette
    c.glow(lambda g: g.circle(240, 170, 120, fill=(40, 60, 110, 90)), radius=50, alpha=1)
    status_bar(c)

    hub = (240, 168)
    nodes = {
        "solar":   (240, 66),
        "grid":    (76, 168),
        "battery": (404, 168),
        "home":    (240, 270),
    }
    R = 30
    gcol = EXPORT if GRID < 0 else IMPORT

    # flow lines (solar→hub, hub→grid, hub→battery, hub→home)
    flows = [("solar", SOLAR, True), ("grid", gcol, False),
             ("battery", BATT, False), ("home", HOMEC, False)]
    for name, col, inbound in flows:
        nx, ny = nodes[name]
        # endpoint on node edge
        dx, dy = hub[0] - nx, hub[1] - ny
        L = math.hypot(dx, dy)
        sx, sy = nx + dx / L * (R + 6), ny + dy / L * (R + 6)
        c.line([(sx, sy), hub], mix(col, BG, 0.28), 4)
        # moving "packets" (static frame of the animation)
        n = 3
        for i in range(n):
            t = (i + 0.35) / n
            if inbound:
                px, py = sx + (hub[0] - sx) * t, sy + (hub[1] - sy) * t
            else:
                px, py = hub[0] + (sx - hub[0]) * t, hub[1] + (sy - hub[1]) * t
            c.glow(lambda g, px=px, py=py, col=col: g.circle(px, py, 4, fill=col + (255,)),
                   radius=3, alpha=0.9)
            c.circle(px, py, 2.6, fill=mix(col, (255, 255, 255), 0.75))
    c.circle(*hub, 6, fill=CARD2, outline=BORDER, width=1.5)

    def node(name, col, draw_icon, ring=None):
        x, y = nodes[name]
        c.glow(lambda g: g.circle(x, y, R, fill=col + (110,)), radius=10, alpha=0.6)
        c.circle(x, y, R, fill=CARD)
        if ring is None:
            c.circle(x, y, R, outline=col, width=2.5)
        else:
            c.circle(x, y, R, outline=mix(col, BG, 0.25), width=3)
            c.arc(x, y, R + 0.5, -90, -90 + 360 * ring, 3.5, col)
        draw_icon(c, x, y, 26, col)

    node("solar", SOLAR, icon_sun)
    node("grid", gcol, icon_grid)
    node("battery", BATT, lambda cc, x, y, s, col: icon_batt(cc, x, y, s, col, SOC / 100),
         ring=SOC / 100)
    node("home", HOMEC, icon_home)

    # labels
    sx, sy = nodes["solar"]
    c.text(sx + R + 14, sy - 6, f"{PV:.1f}", 30, TEXT, "sb", "ls")
    c.text(sx + R + 14 + c.textw(f"{PV:.1f}", 30, "sb") + 4, sy - 7, "kW", 13, MUTED, "r", "ls")
    c.text(sx + R + 15, sy + 10, "SOLAR", 9, SOLAR, "b", "lm", spacing=1.4)

    hx, hy = nodes["home"]
    c.text(hx + R + 14, hy + 4, f"{HOME:.1f}", 30, TEXT, "sb", "ls")
    c.text(hx + R + 14 + c.textw(f"{HOME:.1f}", 30, "sb") + 4, hy + 3, "kW", 13, MUTED, "r", "ls")
    c.text(hx + R + 15, hy + 18, "HOME", 9, HOMEC, "b", "lm", spacing=1.4)
    c.text(hx - R - 14, hy + 4, "100%", 22, TEXT, "sb", "rs")
    c.text(hx - R - 14, hy + 18, "SELF-POWERED", 9, MUTED, "b", "rm", spacing=1.2)

    gx, gy = nodes["grid"]
    c.text(gx, gy + R + 26, f"{abs(GRID):.1f} kW", 20, TEXT, "sb", "ms")
    c.text(gx, gy + R + 38, "EXPORTING" if GRID < 0 else "IMPORTING", 9, gcol, "b", "mm", spacing=1.4)

    bx, by = nodes["battery"]
    c.text(bx, by + R + 26, f"{SOC}%", 20, TEXT, "sb", "ms")
    c.text(bx, by + R + 38, f"CHARGING {ESS:.1f} kW", 9, BATT, "b", "mm", spacing=1.2)
    c.save(path)


# ════════════════════════════════════════════════════════════════════════════
# B — Rings
# ════════════════════════════════════════════════════════════════════════════
def mock_rings(path):
    c = Canvas()
    status_bar(c)

    # big home ring: source mix (solar / battery / grid) around the dial
    cx, cy, R = 128, 172, 108
    c.glow(lambda g: g.circle(cx, cy, R - 10, fill=(90, 70, 180, 70)), radius=30, alpha=1)
    start, sweep = 135, 270
    c.arc(cx, cy, R, start, start + sweep, 14, CARD2)
    load_frac = min(1, HOME / 8.0)          # 8 kW full-scale
    # segments by source; here all from solar
    segs = [(SOLAR, 1.0)]
    a = start
    for col, frac in segs:
        span = sweep * load_frac * frac
        c.glow(lambda g, a=a, span=span, col=col: g.arc(cx, cy, R, a, a + span, 14, col + (255,)),
               radius=6, alpha=0.6)
        c.arc(cx, cy, R, a, a + span, 14, col)
        a += span
    # tick marks
    for i in range(9):
        ang = math.radians(start + sweep * i / 8)
        r0, r1 = R - 22, R - 27
        c.line([(cx + math.cos(ang) * r0, cy + math.sin(ang) * r0),
                (cx + math.cos(ang) * r1, cy + math.sin(ang) * r1)], DIM, 1.5)
    c.text(cx, cy - 38, "HOME", 10, HOMEC, "b", "mm", spacing=2)
    c.text(cx - 6, cy + 14, f"{HOME:.1f}", 54, TEXT, "sb", "ms")
    c.text(cx + c.textw(f"{HOME:.1f}", 54, "sb") / 2 - 2, cy + 12, "kW", 15, MUTED, "r", "ls")
    # source legend under number
    c.circle(cx - 34, cy + 34, 3.5, fill=SOLAR)
    c.text(cx - 26, cy + 34, "100% solar", 12, MUTED, "r", "lm")
    c.text(cx - R + 14, cy + R - 6, "0", 10, DIM, "r", "mm")
    c.text(cx + R - 14, cy + R - 6, "8 kW", 10, DIM, "r", "mm")

    # right column: three rows
    x0, x1 = 256, 468
    rows = [
        ("SOLAR", f"{PV:.1f}", "kW", "generating", SOLAR, icon_sun, PV / 8.0),
        ("BATTERY", f"{SOC}", "%", f"+{ESS:.1f} kW in", BATT,
         lambda cc, x, y, s, col: icon_batt(cc, x, y, s, col, SOC / 100, CARD), SOC / 100),
        ("GRID", f"{abs(GRID):.1f}", "kW", "exporting", EXPORT, icon_grid, abs(GRID) / 8.0),
    ]
    y = 34
    h = 88
    for label, val, unit, sub, col, ic, frac in rows:
        c.grad_rrect(x0, y, x1, y + h - 8, 14, CARD2, CARD, outline=BORDER)
        # icon tile
        c.rrect(x0 + 12, y + 14, x0 + 50, y + 52, 10, fill=mix(col, CARD, 0.16))
        ic(c, x0 + 31, y + 33, 24, col)
        c.text(x0 + 62, y + 22, label, 9, col, "b", "lm", spacing=1.6)
        c.text(x0 + 61, y + 52, val, 28, TEXT, "sb", "ls")
        c.text(x0 + 64 + c.textw(val, 28, "sb"), y + 51, unit, 13, MUTED, "r", "ls")
        c.text(x1 - 12, y + 22, sub, 11, MUTED, "r", "rm")
        # bar
        bx0, bx1, byy = x0 + 12, x1 - 12, y + h - 22
        c.rrect(bx0, byy - 3, bx1, byy + 3, 3, fill=(12, 18, 31))
        c.rrect(bx0, byy - 3, bx0 + (bx1 - bx0) * frac, byy + 3, 3, fill=col)
        y += h
    c.save(path)


# ════════════════════════════════════════════════════════════════════════════
# C — Tiles + live chart
# ════════════════════════════════════════════════════════════════════════════
def mock_chart(path):
    c = Canvas()
    status_bar(c)

    tiles = [
        ("SOLAR", f"{PV:.1f}", "kW", SOLAR, icon_sun),
        ("HOME", f"{HOME:.1f}", "kW", HOMEC, icon_home),
        ("BATTERY", f"{SOC}", "%", BATT, lambda cc, x, y, s, col: icon_batt(cc, x, y, s, col, SOC / 100, CARD)),
        ("GRID", f"{GRID:+.1f}", "kW", EXPORT, icon_grid),
    ]
    subs = ["generating", "100% self-powered", f"+{ESS:.1f} kW in", "exporting"]
    gap, x, y0, y1 = 8, 10, 30, 128
    tw = (W - 20 - gap * 3) / 4
    for (label, val, unit, col, ic), sub in zip(tiles, subs):
        c.grad_rrect(x, y0, x + tw, y1, 14, mix(col, CARD, 0.13), CARD, outline=BORDER)
        c.rrect(x + 14, y0 + 0, x + 44, y0 + 3, 1.5, fill=col)
        ic(c, x + 24, y0 + 25, 20, col)
        c.text(x + 40, y0 + 25, label, 9, col, "b", "lm", spacing=1.3)
        c.text(x + 12, y0 + 70, val, 30, TEXT, "sb", "ls")
        c.text(x + 15 + c.textw(val, 30, "sb"), y0 + 69, unit, 12, MUTED, "r", "ls")
        c.text(x + 12, y0 + 85, sub, 10.5, MUTED, "r", "lm")
        x += tw + gap

    # chart panel
    px0, py0, px1, py1 = 10, 136, W - 10, H - 10
    c.rrect(px0, py0, px1, py1, 14, fill=CARD, outline=BORDER)
    c.text(px0 + 14, py0 + 16, "LAST 6 HOURS", 9, MUTED, "b", "lm", spacing=1.6)
    lx = px1 - 14
    for name, col in (("Home", HOMEC), ("Solar", SOLAR)):
        c.text(lx, py0 + 16, name, 11, MUTED, "r", "rm")
        lx -= c.textw(name, 11) + 6
        c.circle(lx, py0 + 16, 3.5, fill=col)
        lx -= 16

    gx0, gx1, gy0, gy1 = px0 + 32, px1 - 14, py0 + 32, py1 - 22
    for i, lab in enumerate(["6", "4", "2", "0"]):
        yy = gy0 + (gy1 - gy0) * i / 3
        c.line([(gx0, yy), (gx1, yy)], (26, 35, 54), 1, caps=False)
        c.text(gx0 - 8, yy, lab, 9.5, DIM, "r", "rm")
    for i, lab in enumerate(["15:00", "16:00", "17:00", "18:00", "19:00", "20:00", "now"]):
        xx = gx0 + (gx1 - gx0) * i / 6
        c.text(xx, gy1 + 11, lab, 9.5, DIM, "r", "mm")

    N = 120
    def solar(t):   # afternoon decline, a bit of cloud
        return max(0, 5.6 * math.cos(t * 1.15) ** 1.4 - 0.9 * max(0, math.sin(t * 23)) * (t < 0.5)) * 0.75 + 0.05 + (t > 0.98) * 0
    def home(t):
        return 0.8 + 0.4 * math.sin(t * 9) + 1.6 / (1 + math.exp(-(t - 0.55) * 80)) / (1 + math.exp((t - 0.7) * 80)) + (1.1 * t if t > 0.85 else 0)
    def pt(i, fn, scale=6.0):
        t = i / (N - 1)
        v = fn(t)
        return gx0 + (gx1 - gx0) * t, gy1 - (gy1 - gy0) * min(v, scale) / scale
    # fix endpoints to match live values
    sol = [pt(i, solar) for i in range(N)]
    hom = [pt(i, home) for i in range(N)]
    # ease the last stretch into the live values so the line ends on "now"
    for arr, live in ((sol, PV), (hom, HOME)):
        target = gy1 - (gy1 - gy0) * live / 6
        M = 14
        for j in range(M):
            i = N - M + j
            t = (j + 1) / M
            t = t * t * (3 - 2 * t)
            arr[i] = (arr[i][0], arr[i][1] + (target - arr[i][1]) * t)

    # solar area gradient
    area = Image.new("RGBA", c.img.size, (0, 0, 0, 0))
    ad = ImageDraw.Draw(area)
    poly = [(x * K, y * K) for x, y in sol] + [(gx1 * K, gy1 * K), (gx0 * K, gy1 * K)]
    mask = Image.new("L", c.img.size, 0)
    ImageDraw.Draw(mask).polygon(poly, fill=255)
    grad = Image.new("RGBA", c.img.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    for yy in range(int(gy0 * K), int(gy1 * K)):
        t = (yy - gy0 * K) / ((gy1 - gy0) * K)
        gd.line([(0, yy), (W * K, yy)], fill=SOLAR + (int(110 * (1 - t) + 10),))
    area.paste(grad, (0, 0), mask)
    c.img.alpha_composite(area)
    c.line(sol, SOLAR, 2)
    c.line(hom, HOMEC, 2)
    for (x_, y_), col in ((sol[-1], SOLAR), (hom[-1], HOMEC)):
        c.glow(lambda g, x_=x_, y_=y_, col=col: g.circle(x_, y_, 6, fill=col + (255,)), radius=4)
        c.circle(x_, y_, 4, fill=col, outline=CARD, width=1.5)
    c.save(path)


# ════════════════════════════════════════════════════════════════════════════
# D — Hero number + battery
# ════════════════════════════════════════════════════════════════════════════
def mock_hero(path):
    c = Canvas()
    # warm sunlight wash top-left
    c.glow(lambda g: g.circle(60, 40, 140, fill=SOLAR + (55,)), radius=60, alpha=1)
    status_bar(c, left="HOME ENERGY")

    # hero
    c.text(22, 52, "Using now", 14, MUTED, "r", "lm")
    c.text(18, 150, f"{HOME:.1f}", 104, TEXT, "sb", "ls")
    c.text(26 + c.textw(f"{HOME:.1f}", 104, "sb"), 146, "kW", 26, MUTED, "r", "ls")

    # source mix bar
    x0, x1, y = 22, 300, 178
    c.text(x0, y - 4, "POWERED BY", 9, MUTED, "b", "lm", spacing=1.6)
    c.text(x1, y - 4, "100% solar", 11, SOLAR, "sb", "rm")
    c.rrect(x0, y + 8, x1, y + 18, 5, fill=CARD2)
    c.grad_rrect(x0, y + 8, x1, y + 18, 5, (253, 224, 71), SOLAR, vertical=False)

    # battery hero (right)
    bx0, by0, bx1, by1 = 340, 40, 450, 226
    c.rrect(bx0 + 34, by0 - 10, bx1 - 34, by0 + 2, 4, fill=BORDER)
    c.rrect(bx0, by0, bx1, by1, 20, fill=CARD, outline=BORDER, width=2)
    inset = 7
    fill_top = by1 - inset - (by1 - by0 - 2 * inset) * SOC / 100
    c.glow(lambda g: g.rrect(bx0 + inset, fill_top, bx1 - inset, by1 - inset, 14, fill=BATT + (200,)),
           radius=10, alpha=0.5)
    c.grad_rrect(bx0 + inset, fill_top, bx1 - inset, by1 - inset, 14, (110, 231, 183), (16, 150, 110))
    c.text((bx0 + bx1) / 2, (fill_top + by1) / 2 - 4, f"{SOC}", 40, (6, 40, 30), "sb", "mm")
    c.text((bx0 + bx1) / 2, (fill_top + by1) / 2 + 24, "%", 16, (6, 60, 44), "sb", "mm")
    c.text((bx0 + bx1) / 2, by0 + 22, "▲", 12, BATT, "r", "mm")
    c.text((bx0 + bx1) / 2, by1 + 14, f"Charging {ESS:.1f} kW", 12, BATT, "sb", "mm")

    # bottom stat strip
    sy0, sy1 = 254, H - 10
    c.rrect(10, sy0, W - 10, sy1, 14, fill=CARD, outline=BORDER)
    cols = [
        (icon_sun, SOLAR, f"{PV:.1f} kW", "Solar"),
        (icon_grid, EXPORT, f"{abs(GRID):.1f} kW", "Exporting"),
        (lambda cc, x, y, s, col: icon_batt(cc, x, y, s, col, SOC / 100, CARD), BATT, f"+{ESS:.1f} kW", "Battery"),
    ]
    cw = (W - 20) / 3
    for i, (ic, col, val, lab) in enumerate(cols):
        x = 10 + cw * i
        if i:
            c.line([(x, sy0 + 14), (x, sy1 - 14)], BORDER, 1, caps=False)
        ic(c, x + 30, (sy0 + sy1) / 2, 24, col)
        c.text(x + 52, (sy0 + sy1) / 2 - 8, val, 18, TEXT, "sb", "lm")
        c.text(x + 52, (sy0 + sy1) / 2 + 12, lab, 11, MUTED, "r", "lm")
    c.save(path)


if __name__ == "__main__":
    import os, sys
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    mock_flow(os.path.join(out, "mockup-A-power-flow.png"))
    mock_rings(os.path.join(out, "mockup-B-rings.png"))
    mock_chart(os.path.join(out, "mockup-C-tiles-chart.png"))
    mock_hero(os.path.join(out, "mockup-D-hero.png"))
    print("done")
