"""Mockups for off-peak (21:00-07:00) grey numbers and the tighter IP / clock gap.

Usage: python offpeak.py <out_dir>
"""
import sys, os
from PIL import Image, ImageDraw
from modes import *

OFFPEAK_NUM = (156, 163, 176)  # neutral grey for the big numbers off-peak
IP = "192.168.1.50"
OLD_GAP, NEW_GAP = 61, 30      # IP text end -> clock text start ("21:30": 348 -> ~409)


def topbar(c, clock, gap, reconnecting=False):
    """Clock right-aligned at 442 (as now); the IP sits `gap` px left of the
    clock's actual text, with the reconnect spinner centred in the gap."""
    c.text(442, 13, clock, 14, MUTED, "r", "rm")
    icon_wifi(c, 460, 11, 14, MUTED)
    tx = 442 - c.textw(clock, 14, "r")
    c.text(tx - gap, 13, IP, 14, MUTED, "r", "rm")
    if reconnecting:  # stand-in for LV_SYMBOL_REFRESH
        cx = tx - gap / 2
        c.arc(cx, 13, 6, 300, 600, 1.8, MUTED, caps=False)
        c.d.polygon([((cx + 3) * K, 4 * K), ((cx + 8.5) * K, 6 * K), ((cx + 4) * K, 10 * K)], fill=MUTED)


def night_screen(path, hero_num, strip_num, gap, clock="22:15", reconnecting=False):
    # typical cheap-rate night: 1.2 kW from the grid, battery idle at 45%
    pv, grid, ess, soc = 0.0, 1.2, 0.0, 45
    home, shares = flows(pv, grid, ess)
    c = Canvas()
    topbar(c, clock, gap, reconnecting)
    hero(c, icon_house, HOMEV, f"{home:.1f}", dy=-14, num=hero_num)
    mix_bar(c, shares)
    battery(c, soc)
    strip(c, [(sun, SOLAR, f"{pv:.1f}", pv),
              (tower, IMPORT, f"{abs(grid):.1f}", grid),
              (batt_icon(soc), ess_col(ess), ess_txt(ess), ess)], num=strip_num)
    button(c, 16, 117, 151, 40, icon_bolt, "Charge", STORM, False)
    button(c, 175, 117, 151, 40, icon_clock, "Free hour", FREE, False)
    if path:
        c.save(path)
    return c


def crop_compare(out, rows):
    """rows: list of (label, canvas). Crops the top-right status area at 4x."""
    x0, y0, x1, y1 = 200, 0, 480, 28
    S = 4
    pad, lab = 20, 40
    w, h = (x1 - x0) * S, (y1 - y0) * S
    img = Image.new("RGB", (w + 2 * pad, len(rows) * (h + lab + pad) + pad), (30, 32, 38))
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(MONT["sb"], 24)
    for i, (label, c) in enumerate(rows):
        full = c.img.convert("RGB").resize((W * S, H * S), Image.LANCZOS)
        y = pad + i * (h + lab + pad)
        d.text((pad, y), label, font=f, fill=(220, 225, 235))
        img.paste(full.crop((x0 * S, y0 * S, x1 * S, y1 * S)), (pad, y + lab))
    img.save(out)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    P = lambda n: os.path.join(out, n)
    files = [P("offpeak-0-now.png"), P("offpeak-A-hero-grey.png"),
             P("offpeak-B-all-grey.png"), P("offpeak-B-peak-0715.png")]
    now = night_screen(files[0], TEXT, TEXT, OLD_GAP)
    night_screen(files[1], OFFPEAK_NUM, TEXT, NEW_GAP)
    new = night_screen(files[2], OFFPEAK_NUM, OFFPEAK_NUM, NEW_GAP)
    night_screen(files[3], TEXT, TEXT, NEW_GAP, clock="07:15")  # back to white at 7am
    sheet(files, P("offpeak-overview.png"))
    crop_compare(P("offpeak-topbar.png"), [
        ("now: 61px gap", now),
        ("halved: 30px gap", new),
        ("halved, reconnecting (spinner in the gap)", night_screen(None, TEXT, TEXT, NEW_GAP, reconnecting=True)),
        ("halved, narrow clock 11:11", night_screen(None, TEXT, TEXT, NEW_GAP, clock="11:11")),
    ])
    print("\n".join(files))
