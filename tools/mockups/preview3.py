"""Preview of the firmware layout at its real coordinates (main.cpp buildUi)."""
import sys, os
from PIL import ImageFont
import redesign as R
from redesign import *

LV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.pio/libdeps/esp32-3248S035R/lvgl@9.2.2/"
MONT = {"r": LV + "scripts/built_in_font/Montserrat-Medium.ttf",
        "sb": LV + "demos/multilang/assets/fonts/Montserrat-SemiBold.ttf",
        "b": LV + "demos/multilang/assets/fonts/Montserrat-Bold.ttf"}
MONT["l"] = MONT["r"]
R.font = lambda px, w="r": ImageFont.truetype(MONT[w], int(px * K))
HOMEV = (167, 139, 250)


def render(path, pv, grid, ess, soc):
    home = max(0, pv + grid - ess)
    c = Canvas()
    # clock + wifi
    c.text(442, 13, "20:41", 14, MUTED, "r", "rm")
    icon_wifi(c, 460, 11, 14, MUTED)

    # hero
    c.rrect(14, 52, 70, 108, 14, fill=mix(HOMEV, CARD, 0.16))
    icon_home(c, 42, 82, 32, HOMEV)
    c.rrect(42 - 3, 82 + 32 * 0.12, 42 + 3, 82 + 32 * 0.45, 1, fill=mix(HOMEV, CARD, 0.16))
    val = f"{home:.1f}"
    c.text(80, 120, val, 112, TEXT, "sb", "ls")
    c.text(88 + c.textw(val, 112, "sb"), 120, "kW", 24, MUTED, "r", "ls")

    fs = min(home, pv); fb = min(home - fs, max(0, -ess)); fg = max(0, home - fs - fb)
    shares = [(fs, SOLAR, "solar"), (fb, DISCH, "battery"), (fg, IMPORT, "grid")]
    top = max(shares, key=lambda t: t[0])
    c.text(16, 167, f"{round(100 * top[0] / home)}% {top[2]}", 16, top[1], "r", "ls")
    c.rrect(16, 176, 326, 190, 7, fill=CARD2)
    vis = [t for t in shares if t[0] / home > 0.005]
    avail = 310 - 3 * (len(vis) - 1)
    x = 16
    for i, (sh, col, _) in enumerate(vis):
        w = avail - (x - 16 - 3 * i) if i == len(vis) - 1 else round(avail * sh / home)
        c.rrect(x, 176, x + w, 190, 7, fill=col)
        x += w + 3

    # battery
    BX, BY, BW, BH = 342, 34, 126, 150
    c.rrect(BX + (BW - 44) / 2, BY - 9, BX + (BW + 44) / 2, BY + 2, 4, fill=BORDER)
    c.rrect(BX, BY, BX + BW, BY + BH, 18, fill=CARD, outline=BORDER, width=2)
    innerH = BH - 4
    fillH = (innerH - 10) * soc / 100
    bcol = BATT if soc >= 50 else (SOLAR if soc >= 20 else IMPORT)
    fx0, fx1, fy1 = BX + 7, BX + BW - 7, BY + 2 + innerH - 5
    if fillH > 0:
        c.grad_rrect(fx0, fy1 - fillH, fx1, fy1, 12, mix((255, 255, 255), bcol, 0.35), mix(bcol, (0, 0, 0), 0.8))
    infill = fillH >= 52
    cy = BY + 2 + (innerH - 5 - fillH / 2 if infill else (5 + innerH - 5 - fillH) / 2)
    tc = (6, 40, 30) if infill else TEXT
    c.text(BX + BW / 2 - 6, cy, f"{soc}", 44, tc, "sb", "mm")
    c.text(BX + BW / 2 - 6 + c.textw(f"{soc}", 44, "sb") / 2 + 2, cy + 15, "%", 16, tc, "r", "ls")
    flow = f"Charging {ess:.1f} kW" if ess > 0.05 else (f"Discharging {-ess:.1f} kW" if ess < -0.05 else "Idle")
    fcol = BATT if ess > 0.05 else (DISCH if ess < -0.05 else MUTED)
    c.text(BX + BW / 2, BY + BH + 14, flow, 14, fcol, "r", "mm")

    # strip
    c.rrect(10, 212, 470, 312, 14, fill=CARD, outline=BORDER)
    ox, oy = 11, 213
    gcol = IMPORT if grid > 0.05 else (EXPORT if grid < -0.05 else MUTED)
    secs = [(icon_sun, SOLAR, f"{pv:.1f}", pv),
            (icon_grid, gcol, f"{abs(grid):.1f}", grid),
            (lambda cc, x, y, s, col: icon_batt(cc, x, y, s, col, soc / 100, CARD), fcol,
             "0.0" if abs(ess) < 0.05 else f"{ess:+.1f}", ess)]
    for i, (ic, col, v, kw) in enumerate(secs):
        x = ox + 152 * i
        if i:
            c.line([(x, oy + 14), (x, oy + 84)], BORDER, 1, caps=False)
        c.rrect(x + 12, oy + 12, x + 46, oy + 46, 9, fill=mix(col, CARD, 0.16))
        ic(c, x + 29, oy + 29, 22, col)
        bw = 152 - 56 - 14
        c.rrect(x + 56, oy + 26, x + 56 + bw, oy + 32, 3, fill=(12, 18, 31))
        frac = min(1, abs(kw) / 8)
        if abs(kw) > 0.05:
            c.rrect(x + 56, oy + 26, x + 56 + max(bw * 0.025, bw * frac), oy + 32, 3, fill=col)
        c.text(x + 12, oy + 86, v, 44, TEXT, "sb", "ls")
        c.text(x + 17 + c.textw(v, 44, "sb"), oy + 86, "kW", 16, MUTED, "r", "ls")
    c.save(path)


out = sys.argv[1]
render(os.path.join(out, "preview-day.png"), 4.2, -1.3, 0.5, 76)
render(os.path.join(out, "preview-evening.png"), 0.0, 2.8, -0.0, 18)
render(os.path.join(out, "preview-wide.png"), 0.3, 1.2, -10.5, 55)
print("done")
