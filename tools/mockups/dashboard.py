"""Mockups of the current dashboard, drawn at the buildUi() coordinates in
src/dashboard.cpp: home screen (with Charge off/on and off-peak) and free hour.

Uses the Montserrat TTFs from the LVGL package, so `pio run` must have run once.
Usage: python dashboard.py <out_dir>
"""
import sys, os
from PIL import Image, ImageDraw, ImageFont
import draw as R
from draw import *

LV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.pio/libdeps/esp32-3248S035R/lvgl@9.2.2/")
MONT = {"r": LV + "scripts/built_in_font/Montserrat-Medium.ttf",
        "sb": LV + "demos/multilang/assets/fonts/Montserrat-SemiBold.ttf",
        "b": LV + "demos/multilang/assets/fonts/Montserrat-Bold.ttf"}
MONT["l"] = MONT["r"]
R.font = lambda px, w="r": ImageFont.truetype(MONT[w], int(px * K))

# extra palette (theme.h)
HOMEV = (167, 139, 250)    # COL_HOME
TRACK = (12, 18, 31)       # COL_TRACK
SOLAR_HI = (253, 224, 71)  # COL_SOLAR_HI
STORM = (129, 140, 248)    # COL_RESERVE
FREE = (34, 211, 238)      # COL_FREE
SHADOW = (3, 5, 10)        # COL_SHADOW
OFFPEAK = (156, 163, 176)  # COL_OFFPEAK

# layout (dashboard.cpp)
IP, IP_GAP = "192.168.1.50", 30
BATT_X, BATT_Y, BATT_W, BATT_H = 360, 34, 108, 170
MIX_X, MIX_W, MIX_H = 16, 286, 36
MIX_Y = BATT_Y + BATT_H - MIX_H
HERO_X, HERO_BASE = 16, 152
TILE, FREE_TILE = (16, 4, 32), (58, 6, 32)
MODE_BTN_X, MODE_BTN_W, MODE_BTN_H = 314, 40, 79
CHARGE_BTN_Y, FREE_BTN_Y = BATT_Y, 122
CLOSE = (10, 4, 38, 36)
CD_DIGIT_W, CD_COLON_W = 30, 11
COUNTDOWN_W = 4 * CD_DIGIT_W + CD_COLON_W
FREE_W = BATT_X - 14 - MIX_X
FREE_BAR_W = FREE_W - COUNTDOWN_W - 16
STRIP_X, STRIP_Y, STRIP_W, STRIP_H = 10, 212, 460, 100
SECTION_W = (STRIP_W - 2) // 3
BAR_MAX_KW = 8


# ── icons (makeXxxIcon in widgets.cpp) ───────────────────────────────────
def icon_storm(c, cx, cy, s, col, bg=None):
    """Thundercloud on a 32px grid: three circles + a rounded box, 3px bolt."""
    k = s / 32
    P = lambda x, y: (cx + (x - 16) * k, cy + (y - 16) * k)
    for x, y, r in ((10, 11, 5), (18, 8.5, 7), (24, 12, 4.5)):
        c.circle(*P(x, y), r * k, fill=col)
    x0, y0 = P(4, 11); x1, y1 = P(28.5, 17)
    c.rrect(x0, y0, x1, y1, 3 * k, fill=col)
    c.line([P(18, 19), P(12, 25), P(19, 25), P(13, 31)], col, 3 * k)


def icon_clock(c, cx, cy, s, col, bg=None):
    c.circle(cx, cy, s * 0.46, outline=col, width=s * 0.1)
    c.line([(cx, cy), (cx, cy - s * 0.26)], col, s * 0.1)
    c.line([(cx, cy), (cx + s * 0.18, cy + s * 0.08)], col, s * 0.1)


def icon_close(c, cx, cy, s, col, bg=None):
    d = s * 0.32
    c.line([(cx - d, cy - d), (cx + d, cy + d)], col, s * 0.11)
    c.line([(cx - d, cy + d), (cx + d, cy - d)], col, s * 0.11)


def icon_house(c, cx, cy, s, col, bg):
    icon_home(c, cx, cy, s, col)
    c.rrect(cx - s * 0.1, cy + s * 0.12, cx + s * 0.1, cy + s * 0.45, s * 0.03, fill=bg)


def sun(c, cx, cy, s, col, bg): icon_sun(c, cx, cy, s, col)
def tower(c, cx, cy, s, col, bg): icon_grid(c, cx, cy, s, col)
def red_tower(c, cx, cy, s, col, bg): icon_grid(c, cx, cy, s, IMPORT)
def batt_icon(soc):
    return lambda c, cx, cy, s, col, bg: icon_batt(c, cx, cy, s, col, soc / 100, bg)


# ── regions (one per build*() in dashboard.cpp) ──────────────────────────
def top_bar(c, clock):
    """Clock right-anchored at 442, plant IP IP_GAP px left of its text."""
    c.text(442, 13, clock, 14, MUTED, "r", "rm")
    icon_wifi(c, 460, 11, 14, MUTED)
    c.text(442 - c.textw(clock, 14, "r") - IP_GAP, 13, IP, 14, MUTED, "r", "rm")


def text_sx(c, x, y, s, size, color, sx=1.0, w="sb"):
    """Text on baseline y, optionally scaled horizontally. Returns the width."""
    f = R.font(size, w)
    asc, desc = f.getmetrics()
    tw = int(c.d.textlength(s, font=f)) + 8
    layer = Image.new("RGBA", (tw, asc + desc), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((0, asc), s, font=f, fill=color, anchor="ls")
    layer = layer.resize((max(1, int(tw * sx)), asc + desc), Image.LANCZOS)
    c.img.alpha_composite(layer, (int(x * K), int(y * K - asc)))
    return c.d.textlength(s, font=f) / K * sx


def hero(c, tile_icon, tile_col, kw, num=TEXT, tile=TILE, text=None):
    x, y, t = tile
    tbg = mix(tile_col, CARD, 0.16)
    c.rrect(x, y, x + t, y + t, 10, fill=tbg)
    tile_icon(c, x + t / 2, y + t / 2, 22, tile_col, tbg)
    value = text or (f"{kw:.0f}" if kw >= 9.95 else f"{kw:.1f}")
    w = text_sx(c, HERO_X, HERO_BASE, value, 144, num)
    c.text(HERO_X + w + 8, HERO_BASE, "kW", 24, MUTED, "r", "ls")


def mix_bar(c, shares):
    c.rrect(MIX_X, MIX_Y, MIX_X + MIX_W, MIX_Y + MIX_H, 10, fill=CARD2)
    vis = [s for s in shares if s[0] > 0.005]
    avail = MIX_W - 3 * (len(vis) - 1)
    x, used = MIX_X, 0
    for i, (sh, col) in enumerate(vis):
        w = max(4, avail - used if i == len(vis) - 1 else round(avail * sh))
        used += w
        if col == SOLAR:
            c.grad_rrect(x, MIX_Y, x + w, MIX_Y + MIX_H, 10, SOLAR_HI, SOLAR, vertical=False)
        else:
            c.rrect(x, MIX_Y, x + w, MIX_Y + MIX_H, 10, fill=col)
        x += w + 3


def icon_button(c, x, y, w, h, icon, accent, on=False, size=24):
    """makeButton(): raised with a drop shadow; checked sinks BTN_DROP px."""
    DROP, r = 3, 10
    if on:
        y += DROP
        c.grad_rrect(x, y, x + w, y + h, r, mix(accent, BG, 0.18), mix(accent, BG, 0.50))
        c.rrect(x, y, x + w, y + h, r, outline=accent, width=2)
        fg = TEXT
    else:
        c.rrect(x, y + DROP, x + w, y + h + DROP, r, fill=SHADOW)
        c.grad_rrect(x, y, x + w, y + h, r, mix(TEXT, CARD2, 0.07), CARD2)
        c.rrect(x, y, x + w, y + h, r, outline=BORDER, width=1)
        fg = accent
    icon(c, x + w / 2, y + h / 2, size, fg, None)


def mode_buttons(c, charge_on):
    icon_button(c, MODE_BTN_X, CHARGE_BTN_Y, MODE_BTN_W, MODE_BTN_H, icon_storm, STORM, charge_on, 32)
    icon_button(c, MODE_BTN_X, FREE_BTN_Y, MODE_BTN_W, MODE_BTN_H, icon_clock, FREE)


def free_header(c, left_s):
    """Close button, FREE HOUR title and the countdown in the mix bar's slot."""
    bx, by, bw, bh = CLOSE
    icon_button(c, bx, by, bw, bh, lambda cc, x, y, s, col, bg: icon_close(cc, x, y, 20, TEXT), TEXT)
    x, y, t = FREE_TILE
    c.text(x + t + 12, y + t / 2, "FREE HOUR", 14, FREE, "b", "lm", spacing=2)
    c.rrect(MIX_X, MIX_Y + 6, MIX_X + FREE_BAR_W, MIX_Y + MIX_H, 9, fill=CARD2)
    c.grad_rrect(MIX_X, MIX_Y + 6, MIX_X + max(18, FREE_BAR_W * left_s / 3600), MIX_Y + MIX_H, 9,
                 mix((255, 255, 255), FREE, 0.35), FREE, vertical=False)
    # each digit centred in a fixed cell, like cdDigits[]
    mm, ss = divmod(left_s, 60)
    cx = MIX_X + FREE_W - COUNTDOWN_W
    for i, ch in enumerate(f"{mm:02d}{ss:02d}"):
        if i == 2:
            c.text(cx + CD_COLON_W / 2, MIX_Y + MIX_H, ":", 44, FREE, "sb", "ms")
            cx += CD_COLON_W
        c.text(cx + CD_DIGIT_W / 2, MIX_Y + MIX_H, ch, 44, FREE, "sb", "ms")
        cx += CD_DIGIT_W


def battery(c, soc, charge_on=False, fill_fx=None):
    """fill_fx(c, x0, y0, x1, y1), if given, draws over the fill, under the SOC."""
    edge = mix(STORM, BG, 0.75) if charge_on else BORDER
    X, Y, W, H = BATT_X, BATT_Y, BATT_W, BATT_H
    c.rrect(X + (W - 44) / 2, Y - 9, X + (W + 44) / 2, Y + 2, 4, fill=edge)
    c.rrect(X, Y, X + W, Y + H, 18, fill=CARD, outline=edge, width=2)
    innerH = H - 4
    fillH = (innerH - 10) * soc / 100
    bcol = BATT if soc >= 50 else (SOLAR if soc >= 20 else IMPORT)
    fy1 = Y + 2 + innerH - 5
    if fillH > 0:
        c.grad_rrect(X + 7, fy1 - fillH, X + W - 7, fy1, 12, mix((255, 255, 255), bcol, 0.35), mix(bcol, (0, 0, 0), 0.8))
        if fill_fx:
            fill_fx(c, X + 7, fy1 - fillH, X + W - 7, fy1)
    infill = fillH >= 52
    cy = Y + 2 + (innerH - 5 - fillH / 2 if infill else (5 + innerH - 5 - fillH) / 2)
    tc = (6, 40, 30) if infill else TEXT
    nw = c.textw(f"{soc}", 44, "sb")
    x0 = X + (W - (nw + 2 + c.textw("%", 16, "r"))) / 2
    c.text(x0, cy + 15, f"{soc}", 44, tc, "sb", "ls")
    c.text(x0 + nw + 2, cy + 15, "%", 16, tc, "r", "ls")


def strip(c, cells):
    """cells: list of (icon_fn(c,cx,cy,s,col,bg), colour, value_text, kw_for_bar)."""
    c.rrect(STRIP_X, STRIP_Y, STRIP_X + STRIP_W, STRIP_Y + STRIP_H, 14, fill=CARD, outline=BORDER)
    ox, oy = STRIP_X + 1, STRIP_Y + 1
    for i, (ic, col, v, kw) in enumerate(cells):
        x = ox + SECTION_W * i
        if i:
            c.line([(x, oy + 14), (x, oy + 84)], BORDER, 1, caps=False)
        tbg = mix(col, CARD, 0.16)
        c.rrect(x + 12, oy + 12, x + 46, oy + 46, 9, fill=tbg)
        ic(c, x + 29, oy + 29, 22, col, tbg)
        bw = SECTION_W - 56 - 14
        c.rrect(x + 56, oy + 26, x + 56 + bw, oy + 32, 3, fill=TRACK)
        if abs(kw) > 0.05:
            frac = min(1, abs(kw) / BAR_MAX_KW)
            c.rrect(x + 56, oy + 26, x + 56 + max(bw * 0.025, bw * frac), oy + 32, 3, fill=col)
        c.text(x + 12, oy + 86, v, 44, TEXT, "sb", "ls")
        c.text(x + 17 + c.textw(v, 44, "sb"), oy + 86, "kW", 16, MUTED, "r", "ls")


# ── values (updateUi()) ──────────────────────────────────────────────────
def flows(pv, grid, ess):
    home = max(0, pv + grid - ess)
    fs = min(home, pv); fb = min(home - fs, max(0, -ess)); fg = max(0, home - fs - fb)
    return home, [(fs / home, SOLAR), (fg / home, IMPORT), (fb / home, DISCH)] if home > 0.05 else []


def ess_cell(soc, ess):
    col = BATT if ess > 0.05 else (DISCH if ess < -0.05 else MUTED)
    txt = "0.0" if abs(ess) < 0.05 else f"{ess:+.1f}"
    return (batt_icon(soc), col, txt, ess)


def is_offpeak(clock):
    h = int(clock.split(":")[0])
    return h >= 21 or h < 7   # OFFPEAK_START_HOUR / OFFPEAK_END_HOUR


# ── screens ──────────────────────────────────────────────────────────────
def home_screen(path, *a, **kw):
    home_canvas(*a, **kw).save(path)


def home_canvas(pv, grid, ess, soc, clock, charge_on=False, hero_text=None):
    home, shares = flows(pv, grid, ess)
    c = Canvas()
    top_bar(c, clock)
    hero(c, icon_house, HOMEV, home, num=OFFPEAK if is_offpeak(clock) else TEXT, text=hero_text)
    mix_bar(c, shares)
    mode_buttons(c, charge_on)
    battery(c, soc, charge_on)
    gcol = IMPORT if grid > 0.05 else (EXPORT if grid < -0.05 else MUTED)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (red_tower, gcol, f"{abs(grid):.1f}", grid),  # icon stays red, tile follows the flow
              ess_cell(soc, ess)])
    return c


def free_screen(path, pv, grid, ess, soc, clock, left_s, hero_text=None):
    home, _ = flows(pv, grid, ess)
    c = Canvas()
    top_bar(c, clock)
    hero(c, tower, IMPORT, max(0, grid), num=OFFPEAK if is_offpeak(clock) else TEXT,
         tile=FREE_TILE, text=hero_text)
    free_header(c, left_s)
    battery(c, soc)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (icon_house, HOMEV, f"{home:.1f}", home),
              ess_cell(soc, ess)])
    c.save(path)


def sheet(paths, out, cols=2):
    ims = [Image.open(p) for p in paths]
    w, h = ims[0].size
    pad, lab = 24, 44
    rows = (len(ims) + cols - 1) // cols
    S = Image.new("RGB", (cols * w + (cols + 1) * pad, rows * (h + lab) + (rows + 1) * pad), (30, 32, 38))
    d = ImageDraw.Draw(S)
    f = ImageFont.truetype(MONT["sb"], 26)
    for i, (p, im) in enumerate(zip(paths, ims)):
        x = pad + (i % cols) * (w + pad)
        y = pad + (i // cols) * (h + lab + pad)
        d.text((x, y), os.path.splitext(os.path.basename(p))[0], font=f, fill=(220, 225, 235))
        S.paste(im, (x, y + lab))
    S.save(out)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    day = dict(pv=4.2, grid=-1.3, ess=0.5, soc=76, clock="14:20")
    charge = dict(pv=4.2, grid=1.7, ess=3.5, soc=100, clock="14:20")
    night = dict(pv=0.0, grid=1.2, ess=0.0, soc=45, clock="22:15")
    free = dict(pv=0.2, grid=6.8, ess=4.5, soc=54, clock="20:36", left_s=23 * 60 + 41)
    files = [P(n) for n in ("home.png", "home-charge-on.png", "home-offpeak.png", "home-widest.png",
                            "free-hour.png", "free-hour-widest.png")]
    home_screen(files[0], **day)
    home_screen(files[1], **charge, charge_on=True)
    home_screen(files[2], **night)
    home_screen(files[3], **day, hero_text="4.4")   # widest realistic hero value
    free_screen(files[4], **free)
    free_screen(files[5], **dict(free, left_s=58 * 60 + 8), hero_text="4.4")
    sheet(files, P("overview.png"))
    print("\n".join(files))
