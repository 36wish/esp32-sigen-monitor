"""Design D + B-style bars, rendered with the Montserrat fonts LVGL ships."""
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

MAX_KW = 8.0


def mock_final(path, pv=PV, grid=GRID, ess=ESS, soc=SOC):
    home = pv + grid - ess
    c = Canvas()
    c.glow(lambda g: g.circle(60, 40, 140, fill=SOLAR + (50,)), radius=60, alpha=1)
    status_bar(c, left="HOME ENERGY")

    # hero
    c.text(20, 44, "Using now", 14, MUTED, "r", "lm")
    c.text(16, 132, f"{home:.1f}", 92, TEXT, "sb", "ls")
    c.text(24 + c.textw(f"{home:.1f}", 92, "sb"), 128, "kW", 24, MUTED, "r", "ls")

    # powered-by mix bar (solar / battery / grid shares of home load)
    from_solar = min(home, pv)
    from_batt = min(home - from_solar, max(0, -ess))
    from_grid = max(0, home - from_solar - from_batt)
    x0, x1, y = 20, 306, 160
    c.text(x0, y, "POWERED BY", 9, MUTED, "b", "lm", spacing=1.6)
    pct = round(100 * from_solar / home) if home > 0 else 0
    c.text(x1, y, f"{pct}% solar", 11, SOLAR, "sb", "rm")
    c.rrect(x0, y + 10, x1, y + 20, 5, fill=CARD2)
    xa = x0
    for share, c0, c1 in ((from_solar, (253, 224, 71), SOLAR), (from_batt, DISCH, DISCH),
                          (from_grid, IMPORT, IMPORT)):
        if share <= 0:
            continue
        xb = xa + (x1 - x0) * share / home
        c.grad_rrect(xa, y + 10, xb, y + 20, 5, c0, c1, vertical=False)
        xa = xb

    # battery (right)
    bx0, by0, bx1, by1 = 344, 34, 452, 190
    c.rrect(bx0 + 34, by0 - 9, bx1 - 34, by0 + 2, 4, fill=BORDER)
    c.rrect(bx0, by0, bx1, by1, 18, fill=CARD, outline=BORDER, width=2)
    inset = 7
    fill_top = by1 - inset - (by1 - by0 - 2 * inset) * soc / 100
    bcol = BATT if soc >= 50 else (SOLAR if soc >= 20 else IMPORT)
    c.glow(lambda g: g.rrect(bx0 + inset, fill_top, bx1 - inset, by1 - inset, 12, fill=bcol + (200,)),
           radius=10, alpha=0.45)
    c.grad_rrect(bx0 + inset, fill_top, bx1 - inset, by1 - inset, 12,
                 mix((255, 255, 255), bcol, 0.35), mix(bcol, (0, 0, 0), 0.8))
    cy = max((fill_top + by1) / 2, by0 + 40)
    c.text((bx0 + bx1) / 2 - 6, cy, f"{soc}", 40, (6, 40, 30), "sb", "mm")
    c.text((bx0 + bx1) / 2 + c.textw(f"{soc}", 40, "sb") / 2 - 3, cy + 8, "%", 16, (6, 60, 44), "sb", "lm")
    flow = f"Charging {ess:.1f} kW" if ess > 0.05 else (f"Discharging {-ess:.1f} kW" if ess < -0.05 else "Idle")
    c.text((bx0 + bx1) / 2, by1 + 15, flow, 11.5, BATT if ess >= 0 else DISCH, "sb", "mm")

    # bottom strip with B-style bars
    sy0, sy1 = 222, H - 8
    c.rrect(10, sy0, W - 10, sy1, 14, fill=CARD, outline=BORDER)
    gcol = EXPORT if grid < 0 else IMPORT
    secs = [
        (icon_sun, SOLAR, f"{pv:.1f}", "Solar", pv / MAX_KW),
        (icon_grid, gcol, f"{abs(grid):.1f}", "Exporting" if grid < 0 else "Importing", abs(grid) / MAX_KW),
        (lambda cc, x, y, s, col: icon_batt(cc, x, y, s, col, soc / 100, CARD),
         BATT if ess >= 0 else DISCH,
         f"{ess:+.1f}", "Battery", abs(ess) / MAX_KW),
    ]
    cw = (W - 20) / 3
    for i, (ic, col, val, lab, frac) in enumerate(secs):
        x = 10 + cw * i
        if i:
            c.line([(x, sy0 + 14), (x, sy1 - 14)], BORDER, 1, caps=False)
        c.rrect(x + 12, sy0 + 12, x + 46, sy0 + 46, 9, fill=mix(col, CARD, 0.16))
        ic(c, x + 29, sy0 + 29, 22, col)
        c.text(x + 56, sy0 + 23, val, 20, TEXT, "sb", "lm")
        c.text(x + 60 + c.textw(val, 20, "sb"), sy0 + 25, "kW", 11, MUTED, "r", "lm")
        c.text(x + 56, sy0 + 41, lab, 10.5, col, "r", "lm")
        bx0_, bx1_, byy = x + 12, x + cw - 12, sy1 - 18
        c.rrect(bx0_, byy - 3, bx1_, byy + 3, 3, fill=(12, 18, 31))
        c.rrect(bx0_, byy - 3, bx0_ + max(6, (bx1_ - bx0_) * min(1, frac)), byy + 3, 3, fill=col)
    c.save(path)


if __name__ == "__main__":
    out = sys.argv[1]
    mock_final(os.path.join(out, "mockup-final.png"))
    # evening: solar low, battery discharging, grid importing
    mock_final(os.path.join(out, "mockup-final-evening.png"), pv=0.3, grid=0.9, ess=-1.6, soc=34)
    print("done")
