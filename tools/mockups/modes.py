"""Mockups for the Charge (storm reserve) button and the Free-hour screen.

Draws the home screen at the firmware's real coordinates (main.cpp buildUi:
battery 342,34 126x170, mix bar 16,168 310x36, strip 10,212 460x100) and adds
the new controls. Usage: python modes.py <out_dir>
"""
import sys, os
from PIL import ImageFont
import redesign as R
from redesign import *

LV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.pio/libdeps/esp32-3248S035R/lvgl@9.2.2/")
MONT = {"r": LV + "scripts/built_in_font/Montserrat-Medium.ttf",
        "sb": LV + "demos/multilang/assets/fonts/Montserrat-SemiBold.ttf",
        "b": LV + "demos/multilang/assets/fonts/Montserrat-Bold.ttf"}
MONT["l"] = MONT["r"]
R.font = lambda px, w="r": ImageFont.truetype(MONT[w], int(px * K))

HOMEV = (167, 139, 250)
TRACK = (12, 18, 31)
SOLAR_HI = (253, 224, 71)
STORM = (129, 140, 248)   # indigo: storm reserve
FREE = (34, 211, 238)     # cyan: free power hour
SHADOW = (3, 5, 10)

BATT_X, BATT_Y, BATT_W, BATT_H = 342, 34, 126, 170
MIX_X, MIX_Y, MIX_W, MIX_H = 16, 168, 310, 36


# ── icons ────────────────────────────────────────────────────────────────
def icon_storm(c, cx, cy, s, col, bg):
    # cloud with a lightning bolt dropping out of it
    c.circle(cx - s * 0.18, cy - s * 0.08, s * 0.2, fill=col)
    c.circle(cx + s * 0.08, cy - s * 0.2, s * 0.26, fill=col)
    c.rrect(cx - s * 0.42, cy - s * 0.08, cx + s * 0.44, cy + s * 0.14, s * 0.11, fill=col)
    pts = [(0.06, 0.0), (-0.12, 0.3), (0.0, 0.3), (-0.08, 0.56), (0.18, 0.2), (0.05, 0.2), (0.14, 0.0)]
    c.d.polygon([((cx + x * s) * K, (cy + y * s) * K) for x, y in pts], fill=bg)
    pts = [(0.08, 0.04), (-0.07, 0.28), (0.03, 0.28), (-0.04, 0.5), (0.14, 0.23), (0.04, 0.23), (0.12, 0.04)]
    c.d.polygon([((cx + x * s) * K, (cy + y * s) * K) for x, y in pts], fill=col)


def icon_bolt(c, cx, cy, s, col, bg):
    # outlined bolt, same points as makeBoltIcon() on a 24px grid
    pts = [(14, 2), (6, 13), (11, 13), (9, 22), (18, 10), (13, 10), (14, 2)]
    c.line([(cx + (x - 12) * s / 24, cy + (y - 12) * s / 24) for x, y in pts], col, s * 0.09)


def icon_clock(c, cx, cy, s, col, bg):
    c.circle(cx, cy, s * 0.46, outline=col, width=s * 0.1)
    c.line([(cx, cy), (cx, cy - s * 0.26)], col, s * 0.1)
    c.line([(cx, cy), (cx + s * 0.18, cy + s * 0.08)], col, s * 0.1)


def icon_close(c, cx, cy, s, col):
    d = s * 0.32
    c.line([(cx - d, cy - d), (cx + d, cy + d)], col, s * 0.11)
    c.line([(cx - d, cy + d), (cx + d, cy - d)], col, s * 0.11)


def icon_house(c, cx, cy, s, col, bg):
    icon_home(c, cx, cy, s, col)
    c.rrect(cx - s * 0.1, cy + s * 0.12, cx + s * 0.1, cy + s * 0.45, s * 0.03, fill=bg)


# ── widgets ──────────────────────────────────────────────────────────────
def button(c, x, y, w, h, icon, label, accent, on=False, sub=None):
    """Raised button; when on it sinks DROP px, darkens at the top (inset look)
    and picks up the accent colour. All of it maps to LVGL bg_grad + translate_y."""
    DROP, r = 3, 10
    if on:
        y += DROP
        c.grad_rrect(x, y, x + w, y + h, r, mix(accent, BG, 0.18), mix(accent, BG, 0.50))
        c.grad_rrect(x + 1.5, y + 1.5, x + w - 1.5, y + 7, r - 2, mix(accent, BG, 0.06), mix(accent, BG, 0.20))
        c.rrect(x, y, x + w, y + h, r, outline=accent, width=1.5)
        fg, lab = accent, TEXT
    else:
        c.rrect(x, y + DROP, x + w, y + h + DROP, r, fill=SHADOW)
        c.grad_rrect(x, y, x + w, y + h, r, mix(TEXT, CARD2, 0.07), CARD2)
        c.rrect(x, y, x + w, y + h, r, outline=BORDER, width=1)
        fg, lab = accent, TEXT
    isz = min(22, h - 12)
    tw = c.textw(label, 16, "sb")
    if sub:
        tw = max(tw, c.textw(sub, 12, "r"))
    gx = x + (w - (isz + 8 + tw)) / 2
    bg = mix(accent, BG, 0.40) if on else CARD2
    if on:
        fg = TEXT
    icon(c, gx + isz / 2, y + h / 2, isz, fg, bg)
    if sub:
        c.text(gx + isz + 8, y + h / 2 - 1, label, 16, lab, "sb", "ls")
        c.text(gx + isz + 8, y + h / 2 + 13, sub, 12, accent, "r", "ls")
    else:
        c.text(gx + isz + 8, y + h / 2, label, 16, lab, "sb", "lm")
    if on:  # small "on" LED
        c.circle(x + w - 9, y + h / 2, 3.5, fill=(255, 255, 255))


def status(c, clock, show_wifi=True):
    c.text(442, 13, clock, 14, MUTED, "r", "rm")
    icon_wifi(c, 460, 11, 14, MUTED)


def hero(c, tile_icon, tile_col, value, dy=0, num=TEXT):
    c.rrect(14, 52 + dy, 70, 108 + dy, 14, fill=mix(tile_col, CARD, 0.16))
    tile_icon(c, 42, 80 + dy, 32, tile_col, mix(tile_col, CARD, 0.16))
    c.text(80, 120 + dy, value, 112, num, "sb", "ls")
    c.text(88 + c.textw(value, 112, "sb"), 120 + dy, "kW", 24, MUTED, "r", "ls")


def mix_bar(c, shares):
    c.rrect(MIX_X, MIX_Y, MIX_X + MIX_W, MIX_Y + MIX_H, 10, fill=CARD2)
    vis = [s for s in shares if s[0] > 0.005]
    avail = MIX_W - 3 * (len(vis) - 1)
    x, used = MIX_X, 0
    for i, (sh, col) in enumerate(vis):
        w = avail - used if i == len(vis) - 1 else round(avail * sh)
        used += w
        if col == SOLAR:
            c.grad_rrect(x, MIX_Y, x + w, MIX_Y + MIX_H, 10, SOLAR_HI, SOLAR, vertical=False)
        else:
            c.rrect(x, MIX_Y, x + w, MIX_Y + MIX_H, 10, fill=col)
        x += w + 3


def battery(c, soc, outline=BORDER):
    c.rrect(BATT_X + (BATT_W - 44) / 2, BATT_Y - 9, BATT_X + (BATT_W + 44) / 2, BATT_Y + 2, 4,
            fill=outline if outline != BORDER else BORDER)
    c.rrect(BATT_X, BATT_Y, BATT_X + BATT_W, BATT_Y + BATT_H, 18, fill=CARD, outline=outline, width=2)
    innerH = BATT_H - 4
    fillH = (innerH - 10) * soc / 100
    bcol = BATT if soc >= 50 else (SOLAR if soc >= 20 else IMPORT)
    fx0, fx1, fy1 = BATT_X + 7, BATT_X + BATT_W - 7, BATT_Y + 2 + innerH - 5
    if fillH > 0:
        c.grad_rrect(fx0, fy1 - fillH, fx1, fy1, 12, mix((255, 255, 255), bcol, 0.35), mix(bcol, (0, 0, 0), 0.8))
    infill = fillH >= 52
    cy = BATT_Y + 2 + (innerH - 5 - fillH / 2 if infill else (5 + innerH - 5 - fillH) / 2)
    tc = (6, 40, 30) if infill else TEXT
    c.text(BATT_X + BATT_W / 2 - 6, cy, f"{soc}", 44, tc, "sb", "mm")
    c.text(BATT_X + BATT_W / 2 - 6 + c.textw(f"{soc}", 44, "sb") / 2 + 2, cy + 15, "%", 16, tc, "r", "ls")


def strip(c, cells, num=TEXT):
    """cells: list of (icon_fn(c,cx,cy,s,col,bg), colour, value_text, kw_for_bar)."""
    c.rrect(10, 212, 470, 312, 14, fill=CARD, outline=BORDER)
    ox, oy = 11, 213
    for i, (ic, col, v, kw) in enumerate(cells):
        x = ox + 152 * i
        if i:
            c.line([(x, oy + 14), (x, oy + 84)], BORDER, 1, caps=False)
        tbg = mix(col, CARD, 0.16)
        c.rrect(x + 12, oy + 12, x + 46, oy + 46, 9, fill=tbg)
        ic(c, x + 29, oy + 29, 22, col, tbg)
        bw = 152 - 56 - 14
        c.rrect(x + 56, oy + 26, x + 56 + bw, oy + 32, 3, fill=TRACK)
        frac = min(1, abs(kw) / 8)
        if abs(kw) > 0.05:
            c.rrect(x + 56, oy + 26, x + 56 + max(bw * 0.025, bw * frac), oy + 32, 3, fill=col)
        c.text(x + 12, oy + 86, v, 44, num, "sb", "ls")
        c.text(x + 17 + c.textw(v, 44, "sb"), oy + 86, "kW", 16, MUTED, "r", "ls")


def sun(c, cx, cy, s, col, bg): icon_sun(c, cx, cy, s, col)
def tower(c, cx, cy, s, col, bg): icon_grid(c, cx, cy, s, col)
def batt_icon(soc):
    return lambda c, cx, cy, s, col, bg: icon_batt(c, cx, cy, s, col, soc / 100, bg)


def flows(pv, grid, ess):
    home = max(0, pv + grid - ess)
    fs = min(home, pv); fb = min(home - fs, max(0, -ess)); fg = max(0, home - fs - fb)
    return home, [(fs / home, SOLAR), (fg / home, IMPORT), (fb / home, DISCH)] if home > 0.05 else []


def ess_col(ess):
    return BATT if ess > 0.05 else (DISCH if ess < -0.05 else MUTED)


def ess_txt(ess):
    return "0.0" if abs(ess) < 0.05 else f"{ess:+.1f}"


# ── screens ──────────────────────────────────────────────────────────────
def home_screen(path, pv, grid, ess, soc, clock, placement, storm_on):
    home, shares = flows(pv, grid, ess)
    c = Canvas()
    status(c, clock)
    hero(c, icon_house, HOMEV, f"{home:.1f}", dy=0 if placement == "top" else -14)
    mix_bar(c, shares)
    battery(c, soc, outline=mix(STORM, BG, 0.75) if storm_on else BORDER)
    gcol = IMPORT if grid > 0.05 else (EXPORT if grid < -0.05 else MUTED)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (tower, gcol, f"{abs(grid):.1f}", grid),
              (batt_icon(soc), ess_col(ess), ess_txt(ess), ess)])
    if placement == "top":
        button(c, 10, 3, 118, 32, icon_bolt, "Charge", STORM, storm_on)
        button(c, 136, 3, 128, 32, icon_clock, "Free hour", FREE, False)
    else:  # hero moved up 14px; buttons fill the gap above the mix bar
        button(c, 16, 117, 151, 40, icon_bolt, "Charge", STORM, storm_on)
        button(c, 175, 117, 151, 40, icon_clock, "Free hour", FREE, False)
    c.save(path)


def free_screen(path, pv, grid, ess, soc, clock, left_s, variant):
    home, shares = flows(pv, grid, ess)
    c = Canvas()
    status(c, clock)
    # header: close button + title
    c.rrect(10, 4 + 3, 48, 40 + 3, 10, fill=SHADOW)
    c.grad_rrect(10, 4, 48, 40, 10, mix(TEXT, CARD2, 0.07), CARD2)
    c.rrect(10, 4, 48, 40, 10, outline=BORDER, width=1)
    icon_close(c, 29, 22, 20, TEXT)
    c.text(60, 23, "FREE HOUR", 14, FREE, "b", "lm", spacing=1.6)
    mm, ss = divmod(left_s, 60)
    cd = f"{mm:02d}:{ss:02d}"
    # hero shows grid import instead of house use
    hero(c, lambda cc, x, y, s, col, bg: icon_grid(cc, x, y, s, col), IMPORT, f"{max(0, grid):.1f}")
    if variant == "header":
        # countdown pill in the header, mix bar unchanged
        tx = 60 + c.textw("FREE HOUR", 14, "b") + 1.6 * 9 + 14
        w = 22 + 8 + c.textw(cd, 16, "sb") + 26
        c.rrect(tx, 7, tx + w, 37, 15, fill=mix(FREE, BG, 0.18))
        icon_clock(c, tx + 18, 22, 16, FREE, BG)
        c.text(tx + 32, 22, cd, 16, FREE, "sb", "lm")
        mix_bar(c, [(s, FREE if col == IMPORT else col) for s, col in shares])
    else:
        # countdown takes the mix bar slot: big mm:ss + bar draining to the hour
        c.text(MIX_X, 158, "Free power ends in", 16, MUTED, "r", "ls")
        c.text(MIX_X + MIX_W, MIX_Y + MIX_H, cd, 44, FREE, "sb", "rs")
        bw = MIX_W - c.textw("00:00", 44, "sb") - 16
        c.rrect(MIX_X, MIX_Y + 6, MIX_X + bw, MIX_Y + MIX_H, 9, fill=CARD2)
        fw = bw * left_s / 3600
        c.grad_rrect(MIX_X, MIX_Y + 6, MIX_X + max(18, fw), MIX_Y + MIX_H, 9,
                     mix((255, 255, 255), FREE, 0.35), FREE, vertical=False)
    battery(c, soc)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (icon_house, HOMEV, f"{home:.1f}", home),
              (batt_icon(soc), ess_col(ess), ess_txt(ess), ess)])
    c.save(path)


def sheet(paths, out, cols=2):
    from PIL import Image, ImageDraw
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
    P = lambda n: os.path.join(out, n)
    # daytime: 4.2 kW solar, exporting 1.3, battery +0.5 at 76%
    day = dict(pv=4.2, grid=-1.3, ess=0.5, soc=76, clock="14:20")
    # storm reserve on: battery pulls extra from the grid to reach 100%
    storm = dict(pv=4.2, grid=1.7, ess=3.5, soc=76, clock="14:20")
    # 20:36:19 during a free hour: house 2.5 kW, battery soaking up 4.5 kW
    free = dict(pv=0.2, grid=6.8, ess=4.5, soc=54, clock="20:36", left_s=23 * 60 + 41)

    files = []
    for pl, tag in (("top", "A-top"), ("gap", "B-under-hero")):
        home_screen(P(f"modes-{tag}-off.png"), **day, placement=pl, storm_on=False)
        home_screen(P(f"modes-{tag}-storm-on.png"), **storm, placement=pl, storm_on=True)
        files += [P(f"modes-{tag}-off.png"), P(f"modes-{tag}-storm-on.png")]
    free_screen(P("free-hour-A-header.png"), **free, variant="header")
    free_screen(P("free-hour-B-countdown-bar.png"), **free, variant="bar")
    files += [P("free-hour-A-header.png"), P("free-hour-B-countdown-bar.png")]
    sheet(files, P("modes-overview.png"))
    print("\n".join(files))
