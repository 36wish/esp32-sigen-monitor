"""Shared drawing library for the dashboard mockups: 4x supersampled canvas,
palette and vector icons. Output is downsampled to 2x (960x640)."""
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 480, 320
K = 4            # supersample factor
OUT = 2          # output scale
FONTS = "C:/Windows/Fonts/"

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
