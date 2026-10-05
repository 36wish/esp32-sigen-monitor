"""Mockups: how many kWh a free hour session took from the grid.

The plant keeps lifetime counters at 0.01 kWh (read-only, checked live):
30216 total imported, 30200 total charged into the battery, 30228 total load.
Snapshot them when free hour starts and subtract, so the number matches the
plant's own statistics and survives a dropped link.

During free hour (running total):
  A  in the header, after the FREE HOUR title (the plant IP is hidden)
  B  stacked beside the hero number, right-aligned to the battery column
  C  in the countdown row, replacing the draining bar
After it ends:
  D  a summary card over the home screen, with the split into battery/house
  E  a recap beside the hero tile on the home screen, until the next session

C and D are implemented, and drawn by dashboard.py (free_header(),
summary_card()); A, B and E were rejected and still show the old draining bar.

Usage: python free_kwh.py <out_dir>
"""
import sys, os
from PIL import Image
from dashboard import *

# 36 min into a free hour that started at 20:00
LIVE = dict(pv=0.2, grid=6.8, ess=4.5, soc=54, clock="20:36", left_s=23 * 60 + 41)
SO_FAR = dict(grid=3.9, batt=2.6, house=1.3)
# the whole session, seen just after it ended
DONE = dict(pv=0.0, grid=1.1, ess=0.0, soc=88, clock="21:02")
TOTAL = dict(grid=6.4, batt=4.3, house=2.1)


def old_countdown(c, left_s):
    """The countdown row before C: a bar draining to the top of the hour."""
    bar_w = FREE_W - COUNTDOWN_W - 16
    c.rrect(MIX_X, MIX_Y + 6, MIX_X + bar_w, MIX_Y + MIX_H, 9, fill=CARD2)
    c.grad_rrect(MIX_X, MIX_Y + 6, MIX_X + max(18, bar_w * left_s / 3600), MIX_Y + MIX_H, 9,
                 mix((255, 255, 255), FREE, 0.35), FREE, vertical=False)
    mm, ss = divmod(left_s, 60)
    cx = MIX_X + FREE_W - COUNTDOWN_W
    for i, ch in enumerate(f"{mm:02d}{ss:02d}"):
        if i == 2:
            c.text(cx + CD_COLON_W / 2, MIX_Y + MIX_H, ":", 44, FREE, "sb", "ms")
            cx += CD_COLON_W
        c.text(cx + CD_DIGIT_W / 2, MIX_Y + MIX_H, ch, 44, FREE, "sb", "ms")
        cx += CD_DIGIT_W


def free_base(c, s, header=True, countdown=True, hero_text=None):
    """The free hour screen as it was before C, with the top bar and countdown
    optional. Returns where the FREE HOUR title ends."""
    home, _ = flows(s["pv"], s["grid"], s["ess"])
    hero(c, tower, IMPORT, max(0, s["grid"]), num=OFFPEAK if is_offpeak(s["clock"]) else TEXT,
         tile=FREE_TILE, text=hero_text)
    if header:
        top_bar(c, s["clock"])
    title_end = close_and_title(c)
    if countdown:
        old_countdown(c, s["left_s"])
    battery(c, s["soc"])
    strip(c, [(sun, SOLAR, f"{s['pv']:.1f}", s["pv"]),
              (icon_house, HOMEV, f"{home:.1f}", home),
              ess_cell(s["soc"], s["ess"])])
    return title_end


def close_and_title(c):
    bx, by, bw, bh = CLOSE
    icon_button(c, bx, by, bw, bh, lambda cc, x, y, s, col, bg: icon_close(cc, x, y, 20, TEXT), TEXT)
    x, y, t = FREE_TILE
    c.text(x + t + 12, y + t / 2, "FREE HOUR", 14, FREE, "b", "lm", spacing=2)
    return x + t + 12 + c.textw("FREE HOUR", 14, "b") + 2 * 8 + 8


def kwh(c, x, base, v, size, col, unit_size=16, anchor="l"):
    """'3.9 kWh' on a baseline; anchor 'r' right-aligns the whole thing at x."""
    num = f"{v:.1f}"
    nw = c.textw(num, size, "sb")
    uw = c.textw("kWh", unit_size, "r")
    if anchor == "r":
        x -= nw + 4 + uw
    c.text(x, base, num, size, col, "sb", "ls")
    c.text(x + nw + 4, base, "kWh", unit_size, MUTED, "r", "ls")
    return nw + 4 + uw


def a_header(path):
    c = Canvas()
    x = free_base(c, LIVE, header=False)
    # where the plant IP was: the running total, then the clock as usual
    c.text(x, FREE_TILE[1] + FREE_TILE[2] / 2, "·", 14, DIM, "b", "lm")
    kwh(c, x + 14, FREE_TILE[1] + FREE_TILE[2] / 2 + 7, SO_FAR["grid"], 22, TEXT, 14)
    c.text(442, 13, LIVE["clock"], 14, MUTED, "r", "rm")
    icon_wifi(c, 460, 11, 14, MUTED)
    c.save(path)


def b_beside_hero(path, hero_text=None):
    c = Canvas()
    free_base(c, LIVE, hero_text=hero_text)
    right = BATT_X - 14
    c.text(right, 62, "SO FAR", 12, MUTED, "b", "rs", spacing=2)
    kwh(c, right, 96, SO_FAR["grid"], 32, FREE, anchor="r")
    c.save(path)


def c_countdown_row(path):
    free_screen(path, **LIVE, kwh=SO_FAR["grid"])


def d_summary(path):
    summary_screen(path, DONE, "20:03", "21:00", TOTAL["grid"], TOTAL["batt"], TOTAL["house"])


def e_recap(path):
    c = home_canvas(**DONE)
    x, y, t = TILE
    tx = x + t + 12
    c.text(tx, y + t / 2, "FREE HOUR", 14, mix(FREE, BG, 0.75), "b", "lm", spacing=2)
    tx += c.textw("FREE HOUR", 14, "b") + 18 + 8
    kwh(c, tx, y + t / 2 + 7, TOTAL["grid"], 22, TEXT, 14)
    c.save(path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    files = [P(n) for n in ("free-hour-before.png", "A-header.png", "B-beside-hero.png",
                            "B-beside-hero-widest.png", "C-countdown-row.png",
                            "D-summary.png", "E-home-recap.png")]
    before = Canvas()
    free_base(before, LIVE)
    before.save(files[0])
    a_header(files[1])
    b_beside_hero(files[2])
    b_beside_hero(files[3], hero_text="9.4")
    c_countdown_row(files[4])
    d_summary(files[5])
    e_recap(files[6])
    sheet(files, P("overview.png"))
    print("\n".join(files))
