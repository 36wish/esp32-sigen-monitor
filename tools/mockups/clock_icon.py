"""Mockups: a bolder clock for the Free hour button (and the free-hour summary
card, which uses the same makeClockIcon()). Each look is drawn at its
firmware box size, centred where the box is: button close-ups beside the
Charge button, normal and pressed in, the summary header, and full screens.

Usage: python clock_icon.py <out_dir>
"""
import sys, os
from PIL import Image, ImageDraw
import dashboard as D
from dashboard import *


def clock_look(box, r, ring, hand, minute, hour, solid=False):
    """r: face radius, ring/hand: stroke widths, minute/hour: hand ends from
    the centre. solid fills the face and cuts the hands out in the background."""
    def look(c, cx, cy, s, col, bg=None):
        bg = bg or CARD
        if solid:
            c.circle(cx, cy, r, fill=col)
        else:
            c.circle(cx, cy, r, outline=col, width=ring)
        c.line([(cx + minute[0], cy + minute[1]), (cx, cy), (cx + hour[0], cy + hour[1])],
               bg if solid else col, hand)
    look.box = box
    return look


LOOKS = [
    ("now",          clock_look(24, 10, 2, 2, (0, -6), (4, 2))),
    ("A-3px",        clock_look(24, 11, 3, 3, (0, -6), (4, 2))),
    ("B-3px-28",     clock_look(28, 13, 3, 3, (0, -7.5), (5, 2.5))),
    ("C-heavy-28",   clock_look(28, 13, 4, 3, (0, -6), (4.5, 2))),
    ("D-solid-28",   clock_look(28, 13, 0, 3, (0, -7.5), (5, 2.5), solid=True)),
    ("E-solid-24",   clock_look(24, 11, 0, 2.5, (0, -6.5), (4.5, 2), solid=True)),
]

STATE = dict(pv=4.2, grid=-1.3, ess=0.5, soc=76, clock="14:20")


def btn_bg(accent, on):
    """Button colour at the icon's centre (half way down the gradient)."""
    if on:
        return mix(mix(accent, BG, 0.18), mix(accent, BG, 0.50), 0.5)
    return mix(mix(TEXT, CARD2, 0.07), CARD2, 0.5)


def use(look, on):
    """Draw the clock with `look`; on = Free hour button pressed in."""
    D.icon_clock = look
    def buttons(c, charge_on):
        icon_button(c, MODE_BTN_X, CHARGE_BTN_Y, MODE_BTN_W, MODE_BTN_H, icon_storm, STORM, charge_on, 32)
        icon_button(c, MODE_BTN_X, FREE_BTN_Y, MODE_BTN_W, MODE_BTN_H,
                    lambda c, x, y, s, col, bg: look(c, x, y, s, col, btn_bg(FREE, on)), FREE, on)
    D.mode_buttons = buttons


def home(look, on=False):
    use(look, on)
    return home_canvas(**STATE)


def summary(look):
    use(look, False)
    # the icon's box sits at (18, 16) in the card, so a bigger box moves its centre
    d = (look.box - 24) / 2
    D.icon_clock = lambda c, x, y, s, col, bg=None: look(c, x + d, y + d, s, col, bg)
    c = home_canvas(pv=0.0, grid=1.1, ess=0.0, soc=88, clock="21:02")
    summary_card(c, "20:03", "21:00", 6.4, 4.3, 2.1, 88)
    return c


def closeups(out):
    bx = ((MODE_BTN_X - 6) * K, (CHARGE_BTN_Y - 6) * K,
          (MODE_BTN_X + MODE_BTN_W + 5) * K, (FREE_BTN_Y + MODE_BTN_H + 8) * K)
    sx = (SUM_X * K, SUM_Y * K, (SUM_X + 190) * K, (SUM_Y + 56) * K)
    bw, bh = bx[2] - bx[0], bx[3] - bx[1]
    sw, sh = sx[2] - sx[0], sx[3] - sx[1]
    pad, lab = 30, 80
    colw = max(2 * bw, sw)
    S = Image.new("RGB", (len(LOOKS) * (colw + pad) + pad, lab + bh + pad + sh + 2 * pad), (30, 32, 38))
    d = ImageDraw.Draw(S)
    f = R.font(44 / K, "sb")
    for i, (name, look) in enumerate(LOOKS):
        x = pad + i * (colw + pad)
        d.text((x, pad), name, font=f, fill=(220, 225, 235))
        for j, on in enumerate((False, True)):
            S.paste(home(look, on).img.crop(bx).convert("RGB"), (x + j * bw, pad + lab))
        S.paste(summary(look).img.crop(sx).convert("RGB"), (x, pad + lab + bh + pad))
    S.save(out)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    P = lambda n: os.path.join(out, n)
    files = []
    for name, look in LOOKS:
        files.append(P(f"{name}.png")); home(look).save(files[-1])
        files.append(P(f"{name}-summary.png")); summary(look).save(files[-1])
    closeups(P("closeups.png"))
    sheet([f for f in files if not f.endswith("-summary.png")], P("overview.png"))
    print("\n".join(files + [P("closeups.png")]))
