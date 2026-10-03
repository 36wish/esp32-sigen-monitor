"""Mockups: icon-only mode buttons stacked between the hero and the battery.

The hero ("4.4 kW", the widest realistic value) ends at x~308, so the battery
narrows from 126 to 108px (still fits "100%", ~90px) to free a button column.
Hero drops back to its unlifted position (no buttons under it any more).
Usage: python stack.py <out_dir>
"""
import sys, os
import modes as M
from modes import *

BATT_W2 = 108
BATT_X2 = 468 - BATT_W2


def icon_storm(c, cx, cy, s, col, bg=None):
    """Thundercloud on a 32px grid, built from what LVGL can draw: three filled
    circles + a rounded box for the cloud, a 3px polyline bolt underneath."""
    k = s / 32
    P = lambda x, y: (cx + (x - 16) * k, cy + (y - 16) * k)
    for x, y, r in ((10, 11, 5), (18, 8.5, 7), (24, 12, 4.5)):
        c.circle(*P(x, y), r * k, fill=col)
    x0, y0 = P(4, 11); x1, y1 = P(28.5, 17)
    c.rrect(x0, y0, x1, y1, 3 * k, fill=col)
    c.line([P(18, 19), P(12, 25), P(19, 25), P(13, 31)], col, 3 * k)


def battery(c, soc, outline=BORDER):
    """modes.battery() with the SOC text centred as socRow is in the firmware."""
    X, Y, W, H = M.BATT_X, M.BATT_Y, M.BATT_W, M.BATT_H
    c.rrect(X + (W - 44) / 2, Y - 9, X + (W + 44) / 2, Y + 2, 4, fill=outline)
    c.rrect(X, Y, X + W, Y + H, 18, fill=CARD, outline=outline, width=2)
    innerH = H - 4
    fillH = (innerH - 10) * soc / 100
    bcol = BATT if soc >= 50 else (SOLAR if soc >= 20 else IMPORT)
    fy1 = Y + 2 + innerH - 5
    if fillH > 0:
        c.grad_rrect(X + 7, fy1 - fillH, X + W - 7, fy1, 12, mix((255, 255, 255), bcol, 0.35), mix(bcol, (0, 0, 0), 0.8))
    infill = fillH >= 52
    cy = Y + 2 + (innerH - 5 - fillH / 2 if infill else (5 + innerH - 5 - fillH) / 2)
    tc = (6, 40, 30) if infill else TEXT
    nw = c.textw(f"{soc}", 44, "sb")
    x0 = X + (W - (nw + 2 + c.textw("%", 16, "r"))) / 2
    c.text(x0, cy + 15, f"{soc}", 44, tc, "sb", "ls")
    c.text(x0 + nw + 2, cy + 15, "%", 16, tc, "r", "ls")


def icon_button(c, x, y, w, h, icon, accent, on=False):
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
    sz = 32 if icon is icon_storm else 24
    icon(c, x + w / 2, y + h / 2, sz, fg, None)


def screen(path, pv, grid, ess, soc, clock, variant, storm_on, hero_text=None):
    M.BATT_X, M.BATT_W = BATT_X2, BATT_W2
    M.MIX_W = 310 if variant == "A" else 286
    home, shares = flows(pv, grid, ess)
    c = Canvas()
    status(c, clock)
    hero(c, icon_house, HOMEV, hero_text or f"{home:.1f}")
    mix_bar(c, shares)
    battery(c, soc, outline=mix(STORM, BG, 0.75) if storm_on else BORDER)
    gcol = IMPORT if grid > 0.05 else (EXPORT if grid < -0.05 else MUTED)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (tower, gcol, f"{abs(grid):.1f}", grid),
              (batt_icon(soc), ess_col(ess), ess_txt(ess), ess)])
    bx, bw = 314, 40
    if variant == "A":   # two squares beside the hero digits
        icon_button(c, bx, 44, bw, 40, icon_storm, STORM, storm_on)
        icon_button(c, bx, 94, bw, 40, icon_clock, FREE)
    else:                # two tall buttons spanning the battery height; mix bar shortened
        icon_button(c, bx, 34, bw, 79, icon_storm, STORM, storm_on)
        icon_button(c, bx, 122, bw, 79, icon_clock, FREE)
    c.save(path)


# ── round 2: layout B with the house tile above the number ─────────────────
# Tile 32x32 at (16,4); the number starts at x=16 and sits on a baseline at
# y=152 (16px above the mix bar), so it can grow into the freed space.
#   C: bigger font (144px, uniform)       D: 128px stretched 1.2x horizontally
TILE = (16, 4, 32)
HERO_BASE = 152
HERO_FONTS = {"C": (144, 1.0), "D": (128, 1.2)}


def text_sx(c, x, y, s, size, color, sx, w="sb"):
    """Text on baseline y, scaled horizontally by sx. Returns the width."""
    from PIL import Image, ImageDraw
    f = R.font(size, w)
    asc, desc = f.getmetrics()
    tw = int(c.d.textlength(s, font=f)) + 8
    layer = Image.new("RGBA", (tw, asc + desc), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((0, asc), s, font=f, fill=color, anchor="ls")
    layer = layer.resize((max(1, int(tw * sx)), asc + desc), Image.LANCZOS)
    c.img.alpha_composite(layer, (int(x * K), int(y * K - asc)))
    return c.d.textlength(s, font=f) / K * sx


def hero2(c, variant, tile_icon, tile_col, value, num=TEXT, tile=TILE):
    x, y, t = tile
    tbg = mix(tile_col, CARD, 0.16)
    c.rrect(x, y, x + t, y + t, 10, fill=tbg)
    tile_icon(c, x + t / 2, y + t / 2, 22, tile_col, tbg)
    size, sx = HERO_FONTS[variant]
    w = text_sx(c, 16, HERO_BASE, value, size, num, sx)
    c.text(16 + w + 8, HERO_BASE, "kW", 24, MUTED, "r", "ls")


def screen2(path, pv, grid, ess, soc, clock, variant, storm_on, hero_text=None):
    M.BATT_X, M.BATT_W, M.MIX_W = BATT_X2, BATT_W2, 286
    home, shares = flows(pv, grid, ess)
    c = Canvas()
    status(c, clock)
    hero2(c, variant, icon_house, HOMEV, hero_text or f"{home:.1f}")
    mix_bar(c, shares)
    battery(c, soc, outline=mix(STORM, BG, 0.75) if storm_on else BORDER)
    gcol = IMPORT if grid > 0.05 else (EXPORT if grid < -0.05 else MUTED)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (tower, gcol, f"{abs(grid):.1f}", grid),
              (batt_icon(soc), ess_col(ess), ess_txt(ess), ess)])
    icon_button(c, 314, 34, 40, 79, icon_storm, STORM, storm_on)
    icon_button(c, 314, 122, 40, 79, icon_clock, FREE)
    c.save(path)


def free2(path, pv, grid, ess, soc, clock, left_s, variant):
    """Free hour: tile shows the grid, FREE HOUR title beside it, and the close
    button takes the Free-hour button's slot (tap the same spot to go back)."""
    M.BATT_X, M.BATT_W, M.MIX_W = BATT_X2, BATT_W2, 286
    home, _ = flows(pv, grid, ess)
    c = Canvas()
    status(c, clock)
    hero2(c, variant, lambda cc, x, y, s, col, bg: icon_grid(cc, x, y, s, col), IMPORT,
          f"{max(0, grid):.1f}")
    x, y, t = TILE
    c.text(x + t + 12, y + t / 2, "FREE HOUR", 14, FREE, "b", "lm", spacing=1.6)
    mm, ss = divmod(left_s, 60)
    cd = f"{mm:02d}:{ss:02d}"
    c.text(MIX_X + M.MIX_W, MIX_Y + MIX_H, cd, 44, FREE, "sb", "rs")
    bw = M.MIX_W - c.textw("00:00", 44, "sb") - 16
    c.rrect(MIX_X, MIX_Y + 6, MIX_X + bw, MIX_Y + MIX_H, 9, fill=CARD2)
    c.grad_rrect(MIX_X, MIX_Y + 6, MIX_X + max(18, bw * left_s / 3600), MIX_Y + MIX_H, 9,
                 mix((255, 255, 255), FREE, 0.35), FREE, vertical=False)
    battery(c, soc)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (icon_house, HOMEV, f"{home:.1f}", home),
              (batt_icon(soc), ess_col(ess), ess_txt(ess), ess)])
    icon_button(c, 314, 122, 40, 79, lambda cc, x, y, s, col, bg: icon_close(cc, x, y, s, TEXT), TEXT)
    c.save(path)


# Round 3 (C refined): close button in the top-left corner, grid tile and title
# shifted right of it; with the button column empty the countdown runs to the
# battery (FREE_W wide), clearing the space under "kW".
CLOSE = (10, 4, 38, 36)
FREE_TILE = (58, 6, 32)
FREE_W = BATT_X2 - 14 - MIX_X


def free3(path, pv, grid, ess, soc, clock, left_s, variant, hero_text=None):
    M.BATT_X, M.BATT_W = BATT_X2, BATT_W2
    home, _ = flows(pv, grid, ess)
    c = Canvas()
    status(c, clock)
    bx, by, bw_, bh = CLOSE
    icon_button(c, bx, by, bw_, bh, lambda cc, x, y, s, col, bg: icon_close(cc, x, y, 20, TEXT), TEXT)
    hero2(c, variant, lambda cc, x, y, s, col, bg: icon_grid(cc, x, y, s, col), IMPORT,
          hero_text or f"{max(0, grid):.1f}", tile=FREE_TILE)
    x, y, t = FREE_TILE
    c.text(x + t + 12, y + t / 2, "FREE HOUR", 14, FREE, "b", "lm", spacing=1.6)
    mm, ss = divmod(left_s, 60)
    c.text(MIX_X + FREE_W, MIX_Y + MIX_H, f"{mm:02d}:{ss:02d}", 44, FREE, "sb", "rs")
    bw = FREE_W - c.textw("00:00", 44, "sb") - 16
    c.rrect(MIX_X, MIX_Y + 6, MIX_X + bw, MIX_Y + MIX_H, 9, fill=CARD2)
    c.grad_rrect(MIX_X, MIX_Y + 6, MIX_X + max(18, bw * left_s / 3600), MIX_Y + MIX_H, 9,
                 mix((255, 255, 255), FREE, 0.35), FREE, vertical=False)
    battery(c, soc)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (icon_house, HOMEV, f"{home:.1f}", home),
              (batt_icon(soc), ess_col(ess), ess_txt(ess), ess)])
    c.save(path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    day = dict(pv=4.2, grid=-1.3, ess=0.5, soc=76, clock="14:20")
    storm = dict(pv=4.2, grid=1.7, ess=3.5, soc=100, clock="14:20")
    free = dict(pv=0.2, grid=6.8, ess=4.5, soc=54, clock="20:36", left_s=23 * 60 + 41)
    files = []
    for v, tag in (("A", "A-squares"), ("B", "B-tall")):
        screen(P(f"stack-{tag}-off.png"), **day, variant=v, storm_on=False)
        screen(P(f"stack-{tag}-storm-on.png"), **storm, variant=v, storm_on=True)
        screen(P(f"stack-{tag}-widest.png"), **day, variant=v, storm_on=False, hero_text="4.4")
        files += [P(f"stack-{tag}-off.png"), P(f"stack-{tag}-storm-on.png"), P(f"stack-{tag}-widest.png")]
    sheet(files, P("stack-overview.png"), cols=3)
    files2 = []
    for v, tag in (("C", "C-bigger"), ("D", "D-stretched")):
        names = [f"stack-{tag}-{n}.png" for n in ("off", "storm-on", "widest", "free-hour")]
        screen2(P(names[0]), **day, variant=v, storm_on=False)
        screen2(P(names[1]), **storm, variant=v, storm_on=True)
        screen2(P(names[2]), **day, variant=v, storm_on=False, hero_text="4.4")
        free2(P(names[3]), **free, variant=v)
        files2 += [P(n) for n in names]
    sheet(files2, P("stack-round2-overview.png"), cols=4)
    files3 = [P("stack-C-bigger-off.png"), P("stack-C-bigger-free-hour.png"),
              P("stack-C2-free-hour.png"), P("stack-C2-free-hour-widest.png")]
    free3(files3[2], **free, variant="C")
    free3(files3[3], **dict(free, left_s=58 * 60 + 8), variant="C", hero_text="4.4")
    sheet(files3, P("stack-round3-overview.png"), cols=2)
    print("\n".join(files + files2 + files3[2:]))
