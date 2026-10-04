#include "widgets.h"

#include <initializer_list>
#include <math.h>
#include "theme.h"

static const int BTN_DROP = 3;  // how far a button sinks when pressed in

// ── building blocks ──────────────────────────────────────────────────────

lv_obj_t *makeBox(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

lv_obj_t *makeFilledBox(lv_obj_t *parent, uint32_t col, int radius) {
  lv_obj_t *o = makeBox(parent);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  return o;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t col, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  return l;
}

lv_obj_t *makeAlignedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t col,
                           int x, int y, int w, lv_text_align_t align) {
  lv_obj_t *l = makeLabel(parent, font, col, "");
  lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_align(l, align, 0);
  return l;
}

lv_obj_t *makeValueRow(lv_obj_t *parent, int x, int y) {
  lv_obj_t *row = makeBox(parent);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_pos(row, x, y);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  return row;
}

lv_obj_t *makeBar(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, w, h);
  lv_bar_set_range(bar, 0, 1000);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, h / 2, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
  return bar;
}

lv_color_t tint(uint32_t col, uint32_t base, uint8_t amount) {
  return lv_color_mix(lv_color_hex(col), lv_color_hex(base), amount);
}

lv_color_t tileColor(uint32_t col) {
  return tint(col, COL_CARD, 41);
}

void setVisible(lv_obj_t *obj, bool visible) {
  if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else         lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *makeButton(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *b = makeBox(parent);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, tint(COL_TEXT, COL_CARD2, 18), 0);
  lv_obj_set_style_bg_grad_color(b, lv_color_hex(COL_CARD2), 0);
  lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_shadow_color(b, lv_color_hex(COL_SHADOW), 0);
  lv_obj_set_style_shadow_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(b, 2, 0);  // LVGL skips shadows narrower than 2px
  lv_obj_set_style_shadow_offset_y(b, BTN_DROP, 0);
  for (lv_style_selector_t st : { LV_STATE_PRESSED, LV_STATE_CHECKED }) {
    lv_obj_set_style_translate_y(b, BTN_DROP, st);
    lv_obj_set_style_shadow_opa(b, LV_OPA_TRANSP, st);
  }
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 8, 0);
  return b;
}

// ── vector icons ─────────────────────────────────────────────────────────
// lv_line keeps a pointer to its points, so point arrays are static.

static lv_obj_t *iconLine(lv_obj_t *icon, const lv_point_precise_t *pts, uint32_t n, int width) {
  lv_obj_t *l = lv_line_create(icon);
  lv_line_set_points(l, pts, n);
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  return l;
}

static lv_obj_t *makeIconBox(lv_obj_t *tile) {
  lv_obj_t *icon = makeBox(tile);
  lv_obj_set_size(icon, ICON_SIZE, ICON_SIZE);
  lv_obj_center(icon);
  return icon;
}

lv_obj_t *makeSunIcon(lv_obj_t *tile) {
  static lv_point_precise_t rays[8][2];
  lv_obj_t *icon = makeIconBox(tile);
  lv_obj_t *core = makeFilledBox(icon, 0, LV_RADIUS_CIRCLE);
  lv_obj_set_size(core, 12, 12);
  lv_obj_center(core);
  for (int i = 0; i < 8; i++) {
    float a = i * (float)M_PI / 4;
    rays[i][0] = { (lv_value_precise_t)lroundf(14 + cosf(a) * 9.5f), (lv_value_precise_t)lroundf(14 + sinf(a) * 9.5f) };
    rays[i][1] = { (lv_value_precise_t)lroundf(14 + cosf(a) * 12.5f), (lv_value_precise_t)lroundf(14 + sinf(a) * 12.5f) };
    iconLine(icon, rays[i], 2, 2);
  }
  return icon;
}

lv_obj_t *makeGridIcon(lv_obj_t *tile) {
  // transmission tower: A-frame legs, two cross-arms, X bracing
  static const lv_point_precise_t legs[]   = { {7, 26}, {14, 2}, {21, 26} };
  static const lv_point_precise_t armTop[] = { {5, 9}, {23, 9} };
  static const lv_point_precise_t armLow[] = { {7, 16}, {21, 16} };
  static const lv_point_precise_t brace1[] = { {11, 9}, {19, 16} };
  static const lv_point_precise_t brace2[] = { {17, 9}, {9, 16} };
  lv_obj_t *icon = makeIconBox(tile);
  iconLine(icon, legs, 3, 2);
  iconLine(icon, armTop, 2, 2);
  iconLine(icon, armLow, 2, 2);
  iconLine(icon, brace1, 2, 1);
  iconLine(icon, brace2, 2, 1);
  return icon;
}

// Drawn on a 32px grid and scaled down to ICON_SIZE.
lv_obj_t *makeHouseIcon(lv_obj_t *tile) {
  auto k = [](float v) { return (int32_t)lroundf(v * ICON_SIZE / 32); };
  static lv_point_precise_t roof[3];  // shared by every house icon
  roof[0] = { k(3), k(16) };
  roof[1] = { k(16), k(4) };
  roof[2] = { k(29), k(16) };
  lv_obj_t *icon = makeIconBox(tile);
  iconLine(icon, roof, 3, 3);
  lv_obj_t *walls = makeBox(icon);
  lv_obj_set_size(walls, k(18), k(16));
  lv_obj_set_pos(walls, k(7), k(14));
  lv_obj_set_style_radius(walls, 2, 0);
  lv_obj_set_style_border_width(walls, 3, 0);
  lv_obj_set_style_border_side(walls, (lv_border_side_t)(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT |
                                                         LV_BORDER_SIDE_BOTTOM), 0);
  lv_obj_t *door = makeFilledBox(icon, 0, 1);
  lv_obj_set_size(door, k(6), k(9));
  lv_obj_set_pos(door, k(13), k(21));
  return icon;
}

// Battery outline with a level box inside, kept in the icon's user data for
// setBattIconLevel().
lv_obj_t *makeBattIcon(lv_obj_t *tile) {
  lv_obj_t *icon = makeIconBox(tile);
  lv_obj_t *nub = makeFilledBox(icon, 0, 1);
  lv_obj_set_size(nub, 6, 3);
  lv_obj_set_pos(nub, 11, 2);
  lv_obj_t *body = makeBox(icon);
  lv_obj_set_size(body, 12, 21);
  lv_obj_set_pos(body, 8, 5);
  lv_obj_set_style_radius(body, 3, 0);
  lv_obj_set_style_border_width(body, 2, 0);
  lv_obj_t *level = makeFilledBox(icon, 0, 1);
  lv_obj_set_size(level, 6, 12);
  lv_obj_set_pos(level, 11, 11);
  lv_obj_set_user_data(icon, level);
  return icon;
}

// The level grows up from the inside bottom of the body (y = 23).
void setBattIconLevel(lv_obj_t *battIcon, int soc) {
  lv_obj_t *level = (lv_obj_t *)lv_obj_get_user_data(battIcon);
  int h = 1 + 11 * soc / 100;
  lv_obj_set_height(level, h);
  lv_obj_set_y(level, 23 - h);
}

lv_obj_t *makeStormIcon(lv_obj_t *parent) {
  // thundercloud: three round puffs on a flat base, a bolt dropping out of it
  static const lv_point_precise_t bolt[] = { {18, 18}, {12, 24}, {19, 24}, {13, 30} };
  static const int16_t puffs[][3] = { {5, 6, 10}, {11, 2, 14}, {19, 7, 9} };  // x, y, diameter
  lv_obj_t *icon = makeBox(parent);
  lv_obj_set_size(icon, 32, 32);
  for (const auto &p : puffs) {
    lv_obj_t *puff = makeFilledBox(icon, 0, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(puff, p[0], p[1]);
    lv_obj_set_size(puff, p[2], p[2]);
  }
  lv_obj_t *base = makeFilledBox(icon, 0, 3);
  lv_obj_set_pos(base, 4, 11);
  lv_obj_set_size(base, 25, 6);
  iconLine(icon, bolt, 4, 3);
  return icon;
}

lv_obj_t *makeClockIcon(lv_obj_t *parent) {
  static const lv_point_precise_t hands[] = { {12, 6}, {12, 12}, {16, 14} };
  lv_obj_t *icon = makeBox(parent);
  lv_obj_set_size(icon, 24, 24);
  lv_obj_t *face = makeBox(icon);
  lv_obj_set_size(face, 20, 20);
  lv_obj_set_pos(face, 2, 2);
  lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(face, 2, 0);
  iconLine(icon, hands, 3, 2);
  return icon;
}

lv_obj_t *makeCloseIcon(lv_obj_t *parent) {
  static const lv_point_precise_t a[] = { {5, 5}, {19, 19} };
  static const lv_point_precise_t b[] = { {5, 19}, {19, 5} };
  lv_obj_t *icon = makeBox(parent);
  lv_obj_set_size(icon, 24, 24);
  iconLine(icon, a, 2, 2);
  iconLine(icon, b, 2, 2);
  return icon;
}

// Lines take the colour as line colour, boxes as fill and border.
void setIconColor(lv_obj_t *icon, uint32_t col) {
  lv_color_t c = lv_color_hex(col);
  uint32_t n = lv_obj_get_child_count(icon);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *ch = lv_obj_get_child(icon, i);
    if (lv_obj_check_type(ch, &lv_line_class)) {
      lv_obj_set_style_line_color(ch, c, 0);
    } else {
      lv_obj_set_style_bg_color(ch, c, 0);
      lv_obj_set_style_border_color(ch, c, 0);
    }
  }
}
