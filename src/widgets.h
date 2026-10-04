#pragma once

#include <lvgl.h>

// LVGL building blocks, and vector icons drawn from lines and boxes (rather
// than images) so they can be recoloured at runtime with setIconColor().

// ── building blocks ──────────────────────────────────────────────────────

// Unstyled rectangle we fully control (no theme padding/borders/scrolling).
lv_obj_t *makeBox(lv_obj_t *parent);
lv_obj_t *makeFilledBox(lv_obj_t *parent, uint32_t col, int radius);
lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t col, const char *text);
// Fixed-width label so changing text never needs re-alignment.
lv_obj_t *makeAlignedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t col,
                           int x, int y, int w, lv_text_align_t align);
// Row that bottom-aligns a value label and a smaller unit label after it.
lv_obj_t *makeValueRow(lv_obj_t *parent, int x, int y);
// Rounded bar on a dark track, range 0..1000.
lv_obj_t *makeBar(lv_obj_t *parent, int x, int y, int w, int h);
// Raised button: light-to-dark gradient over a hard drop shadow. While pressed
// (and while checked, for toggles) it sinks onto where its shadow was.
// Children are laid out in a centred row.
lv_obj_t *makeButton(lv_obj_t *parent, int x, int y, int w, int h);

// `col` mixed over `base`; amount 255 = all `col`.
lv_color_t tint(uint32_t col, uint32_t base, uint8_t amount);
// Icon tile background: the icon's colour, faint over the card.
lv_color_t tileColor(uint32_t col);
void setVisible(lv_obj_t *obj, bool visible);

// ── vector icons ─────────────────────────────────────────────────────────

// Tile icons, ICON_SIZE square, centred in the tile.
static const int ICON_SIZE = 28;
lv_obj_t *makeSunIcon(lv_obj_t *tile);
lv_obj_t *makeGridIcon(lv_obj_t *tile);
lv_obj_t *makeHouseIcon(lv_obj_t *tile);
lv_obj_t *makeBattIcon(lv_obj_t *tile);
void setBattIconLevel(lv_obj_t *battIcon, int soc);

// Button icons: storm 32x32, the others 24x24.
lv_obj_t *makeStormIcon(lv_obj_t *parent);
lv_obj_t *makeClockIcon(lv_obj_t *parent);
lv_obj_t *makeCloseIcon(lv_obj_t *parent);

void setIconColor(lv_obj_t *icon, uint32_t col);
