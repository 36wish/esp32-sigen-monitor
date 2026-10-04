"""Mockups: free hour has ended but the plant refused the remote EMS release,
so it's still charging from the grid while plant.cpp retries every 60 s.
Three ways to show that on the home screen, next to the normal home screen.

Usage: python pending.py <out_dir>
"""
import sys, os
from dashboard import *

# Just after a free hour that ended at 21:00: battery still charging from the grid.
STATE = dict(pv=0.0, grid=6.8, ess=4.5, soc=61, clock="21:01")


def base(c, s, free_btn=True):
    """Home screen without the Free hour button, so a variant can redraw it."""
    home, shares = flows(s["pv"], s["grid"], s["ess"])
    top_bar(c, s["clock"])
    hero(c, icon_house, HOMEV, home, num=OFFPEAK if is_offpeak(s["clock"]) else TEXT)
    mix_bar(c, shares)
    icon_button(c, MODE_BTN_X, CHARGE_BTN_Y, MODE_BTN_W, MODE_BTN_H, icon_storm, STORM, False, 32)
    if free_btn:
        icon_button(c, MODE_BTN_X, FREE_BTN_Y, MODE_BTN_W, MODE_BTN_H, icon_clock, FREE)
    battery(c, s["soc"])
    gcol = IMPORT if s["grid"] > 0.05 else (EXPORT if s["grid"] < -0.05 else MUTED)
    strip(c, [(sun, SOLAR, f"{s['pv']:.1f}", s["pv"]),
              (red_tower, gcol, f"{abs(s['grid']):.1f}", s["grid"]),
              ess_cell(s["soc"], s["ess"])])


def normal(path):
    c = Canvas()
    base(c, STATE)
    c.save(path)


def a_button(path):
    """A: the Free hour button stays pressed in, with a red badge. Tapping it
    sends the release again straight away."""
    c = Canvas()
    base(c, STATE, free_btn=False)
    icon_button(c, MODE_BTN_X, FREE_BTN_Y, MODE_BTN_W, MODE_BTN_H, icon_clock, FREE, on=True)
    bx, by = MODE_BTN_X + MODE_BTN_W - 3, FREE_BTN_Y + 3 + 3
    c.circle(bx, by, 7, fill=BG)
    c.circle(bx, by, 5, fill=IMPORT)
    c.save(path)


def b_title(path):
    """B: a red title beside the hero tile, where FREE HOUR sits during free hour."""
    c = Canvas()
    base(c, STATE)
    x, y, t = TILE
    c.text(x + t + 12, y + t / 2, "STILL GRID CHARGING", 14, IMPORT, "b", "lm", spacing=2)
    c.save(path)


def c_banner(path):
    """C: a banner in the mix bar's slot, with the time to the next retry."""
    c = Canvas()
    base(c, STATE)
    x0, y0, x1, y1 = MIX_X, MIX_Y, MIX_X + MIX_W, MIX_Y + MIX_H
    c.rrect(x0, y0, x1, y1, 10, fill=mix(IMPORT, CARD, 0.16), outline=IMPORT, width=2)
    icon_grid(c, x0 + 22, (y0 + y1) / 2, 20, IMPORT)
    c.text(x0 + 40, (y0 + y1) / 2, "Still charging from grid", 14, TEXT, "sb", "lm")
    c.text(x1 - 12, (y0 + y1) / 2, "retry 0:42", 12, MUTED, "r", "rm")
    c.save(path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    files = [P(n) for n in ("home-normal.png", "pending-A-button.png", "pending-B-title.png",
                            "pending-C-banner.png")]
    normal(files[0])
    a_button(files[1])
    b_title(files[2])
    c_banner(files[3])
    sheet(files, P("overview.png"))
    print("\n".join(files))
