#include "dashboard.h"

#include <initializer_list>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include "display.h"
#include "settings.h"
#include "theme.h"
#include "widgets.h"

// Layout (480x320 landscape), all in absolute pixel coordinates:
//   top right        plant IP  hh:mm  wifi
//   hero (left)      icon tile, big kW number, source-mix bar
//   mode buttons     Charge over Free hour, between the hero and the battery
//   battery (right)  large battery graphic with SOC
//   bottom strip     solar | grid | battery: icon tile + bar, big value below
//
// Free hour isn't a separate LVGL screen: setFreeHourMode() hides and shows
// widgets and moves the hero tile.

// ── layout ───────────────────────────────────────────────────────────────

// Gap between the end of the plant IP and the start of the clock text; the
// reconnect spinner sits centred in it.
static const int IP_GAP = 30;
static const int SPINNER_SIZE = 16;

// Battery graphic geometry (top-right). 108px wide still fits "100%".
static const int BATT_X = 360, BATT_Y = 34, BATT_W = 108, BATT_H = 170;
static const int BATT_BORDER     = 2;
static const int BATT_INSET      = 5;  // between the border and the fill
static const int BATT_INNER_H    = BATT_H - 2 * BATT_BORDER;
static const int BATT_FILL_MAX_H = BATT_INNER_H - 2 * BATT_INSET;

// Flow glow: soft light bands travelling through the battery fill, up while
// charging and down while discharging, faster the more power flows. Each band
// is two gradient boxes, transparent -> peak -> transparent. The peak is
// bright enough to show on the light yellow and red fills too.
static const int      GLOW_W      = 52;   // band height (even: two halves)
static const lv_opa_t GLOW_PEAK   = 166;  // 65%
static const int      GLOW_BANDS  = 1;    // one wave at a time: the next starts once this one has crossed the fill
static const float   GLOW_SPEED_BASE = 12, GLOW_SPEED_PER_KW = 16, GLOW_SPEED_MAX = 90;  // px/s
static const uint32_t GLOW_TICK_MS   = 33;

// Source-mix bar geometry, bottom-aligned with the battery graphic.
static const int MIX_X   = 16;
static const int MIX_W   = 286;
static const int MIX_H   = 36;
static const int MIX_Y   = BATT_Y + BATT_H - MIX_H;
static const int MIX_GAP = 3;

// Hero: icon tile in the top-left corner, the number under it on a baseline at
// y=152 (font_hero_144: line height 136, baseline 14 up from the bottom).
// Under the free-hour header the tile shifts right to make room for the close button.
static const int HERO_X = 16, HERO_Y = 30;
static const int TILE_SIZE = 32;
static const int TILE_X = 16, TILE_Y = 4;
static const int FREE_TILE_X = 58, FREE_TILE_Y = 6;

// Mode buttons: icon-only, stacked in a column between the hero and the
// battery, together spanning the battery's height.
static const int MODE_BTN_X = 314, MODE_BTN_W = 40, MODE_BTN_H = 79;
static const int CHARGE_BTN_Y = BATT_Y, FREE_BTN_Y = 122;

// Free-hour countdown row, in the mix bar's slot, which runs on across the
// (hidden) button column to the battery: a grid tile and the kWh imported so
// far on the left, "mm:ss" to the top of the hour right-aligned. font_num_44
// digits are proportional ("1" is 17px, "4" is 30px), so each countdown digit
// gets its own fixed cell; otherwise the text changes width every second and
// jumps around. Whole kWh from 10 up, so the total never reaches the countdown.
static const int CD_DIGIT_W  = 30;  // widest digit in font_num_44
static const int CD_COLON_W  = 11;
static const int COUNTDOWN_W = 4 * CD_DIGIT_W + CD_COLON_W;
static const int FREE_W      = BATT_X - 14 - MIX_X;
static const int FREE_KWH_TILE = 34;

// Free-hour summary card, centred over the home screen when free hour ends.
static const int SUM_W = 300, SUM_H = 244;

// Bottom strip: three equal sections inside a 1px border.
static const int STRIP_X = 10, STRIP_Y = 212, STRIP_W = 460, STRIP_H = 100;
static const int SECTION_W = (STRIP_W - 2) / 3;

// ── widgets ──────────────────────────────────────────────────────────────
static lv_obj_t *timeLabel, *ipLabel, *reconnectIcon;
static lv_obj_t *overlay, *overlayLabel;

static lv_obj_t *heroTile, *heroHouseIcon, *heroGridIcon;
static lv_obj_t *heroValue;
static lv_obj_t *mixBar, *mixSolar, *mixGrid, *mixBatt;

static lv_obj_t *battNub, *battBody, *battFill;
static lv_obj_t *socRow, *socValue, *socPctLabel;

// One flow-glow band inside battFill: rising half above the crest, falling half below.
struct GlowBand {
  lv_obj_t *rise, *fall;
  lv_opa_t  opa;  // peak opacity it's drawn with now
};
static GlowBand glowBands[GLOW_BANDS];

// One section of the bottom strip: icon tile with a bar beside it, value underneath.
struct StripSection {
  lv_obj_t *tile;
  lv_obj_t *icon;
  lv_obj_t *value;
  lv_obj_t *bar;
  lv_obj_t *altIcon;  // grid section only: house icon shown during free hour
};
static StripSection secSolar, secGrid, secBatt;

// Mode buttons (home screen) and the free-hour header/countdown.
static lv_obj_t *chargeBtn, *chargeIcon, *freeBtn, *freeIcon;
static lv_obj_t *closeBtn, *freeTitle, *freeKwhTile, *freeKwhRow, *freeKwhValue, *freeCountdown;
static lv_obj_t *cdDigits[4];  // m m : s s, inside freeCountdown

// Free-hour summary: a dimmed layer over the home screen (a tap closes it)
// holding the card.
static lv_obj_t *summary, *sumTotal, *sumTimes, *sumBattIcon, *sumBattValue, *sumHouseValue;

// Pending release (home screen): the Free hour button stays pressed in with a
// red badge, and a red title sits beside the hero tile.
static lv_obj_t *pendingBadge, *pendingTitle;

// ── state ────────────────────────────────────────────────────────────────
static bool         haveData = false;  // once true, reconnects keep the last readings on screen
static PlantReading reading;           // the latest, so a mode switch can redraw straight away
static bool         freeHour = false;
static bool         releasePending = false;  // free hour is over but the plant refused the release
static uint32_t     freeHourEndMs = 0;
static int          shownSecsLeft = -1;
static time_t       freeStartedAt = 0, freeEndedAt = 0;  // wall clock, for the summary card

// Flow glow (see GLOW_*): set by showBattery(), run by glowTick().
static float    glowSpeed = 0;     // px/s, positive = up (charging), 0 = still
static float    glowPhase = 0;     // px, 0..fill height
static int      glowFillH = 0;
static int      glowShownPx = -1;   // phase in whole px last placed, -1 = hidden
static bool     glowDirty = true;   // fill changed: place again

// Earlier than this, the clock hasn't been set by NTP yet.
static const time_t CLOCK_SYNCED_AFTER = 1700000000;

static void onChargeToggled(lv_event_t *e);
static void onFreeHourOpen(lv_event_t *e);
static void onFreeHourClose(lv_event_t *e);
static void onSummaryClose(lv_event_t *e);
static void glowTick(lv_timer_t *t);

// ── strip sections ───────────────────────────────────────────────────────

static void setBarKw(lv_obj_t *bar, float kw) {
  float frac = fabsf(kw) / BAR_MAX_KW;
  if (frac > 1) frac = 1;
  int32_t v = (int32_t)(frac * 1000);
  if (fabsf(kw) > IDLE_KW && v < 25) v = 25;  // keep a visible nub for small flows
  lv_bar_set_value(bar, v, LV_ANIM_ON);
}

static void setSectionColor(StripSection &s, uint32_t col) {
  lv_obj_set_style_bg_color(s.tile, tileColor(col), 0);
  setIconColor(s.icon, col);
  lv_obj_set_style_bg_color(s.bar, lv_color_hex(col), LV_PART_INDICATOR);
}

static StripSection makeSection(lv_obj_t *strip, int x, lv_obj_t *(*makeIcon)(lv_obj_t *), uint32_t col) {
  StripSection s = {};
  // top row: icon tile with the bar beside it; big value underneath
  s.tile = makeFilledBox(strip, COL_CARD, 9);
  lv_obj_set_size(s.tile, 34, 34);
  lv_obj_set_pos(s.tile, x + 12, 12);
  s.icon = makeIcon(s.tile);

  s.bar = makeBar(strip, x + 56, 26, SECTION_W - 56 - 14, 6);

  // sized so the widest realistic value ("-10.5 kW") still fits the section
  lv_obj_t *row = makeValueRow(strip, x + 12, 48);
  s.value = makeLabel(row, &font_num_44, COL_TEXT, "--");
  lv_obj_t *unit = makeLabel(row, &lv_font_montserrat_16, COL_MUTED, "kW");
  lv_obj_set_style_pad_left(unit, 5, 0);
  lv_obj_set_style_pad_bottom(unit, 1, 0);  // baseline-align with the 44px value
  setSectionColor(s, col);
  return s;
}

// ── building ─────────────────────────────────────────────────────────────

// Plant IP, clock and wifi (top right). The clock is sized to its text and
// right-anchored; layoutTopBar() keeps the IP and spinner a fixed distance
// left of whatever it reads.
static void buildTopBar(lv_obj_t *scr) {
  timeLabel = makeLabel(scr, &lv_font_montserrat_14, COL_MUTED, "");
  lv_obj_align(timeLabel, LV_ALIGN_TOP_RIGHT, -38, 5);
  ipLabel = makeAlignedLabel(scr, &lv_font_montserrat_14, COL_MUTED, 0, 5, 130, LV_TEXT_ALIGN_RIGHT);
  reconnectIcon = makeAlignedLabel(scr, &lv_font_montserrat_14, COL_MUTED, 0, 5, SPINNER_SIZE,
                                   LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(reconnectIcon, LV_SYMBOL_REFRESH);
  lv_obj_set_height(reconnectIcon, SPINNER_SIZE);
  lv_obj_set_style_transform_pivot_x(reconnectIcon, SPINNER_SIZE / 2, 0);
  lv_obj_set_style_transform_pivot_y(reconnectIcon, SPINNER_SIZE / 2, 0);
  lv_obj_add_flag(reconnectIcon, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *wifi = makeLabel(scr, &lv_font_montserrat_14, COL_MUTED, LV_SYMBOL_WIFI);
  lv_obj_align(wifi, LV_ALIGN_TOP_RIGHT, -12, 5);
}

static void layoutTopBar() {
  lv_obj_update_layout(timeLabel);  // clock width follows its digits
  lv_obj_align_to(ipLabel, timeLabel, LV_ALIGN_OUT_LEFT_TOP, -IP_GAP, 0);
  lv_obj_align_to(reconnectIcon, timeLabel, LV_ALIGN_OUT_LEFT_TOP, -(IP_GAP - SPINNER_SIZE) / 2, 0);
}

// Current house load (grid import during free hour; setFreeHourMode() swaps
// the tile's icons).
static void buildHero(lv_obj_t *scr) {
  heroTile = makeFilledBox(scr, COL_CARD, 10);
  lv_obj_set_size(heroTile, TILE_SIZE, TILE_SIZE);
  lv_obj_set_pos(heroTile, TILE_X, TILE_Y);
  lv_obj_set_style_bg_color(heroTile, tileColor(COL_HOME), 0);
  heroHouseIcon = makeHouseIcon(heroTile);
  setIconColor(heroHouseIcon, COL_HOME);
  heroGridIcon = makeGridIcon(heroTile);
  setIconColor(heroGridIcon, COL_IMPORT);
  lv_obj_add_flag(heroGridIcon, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *row = makeValueRow(scr, HERO_X, HERO_Y);
  heroValue = makeLabel(row, &font_hero_144, COL_TEXT, "-.-");
  lv_obj_t *unit = makeLabel(row, &lv_font_montserrat_24, COL_MUTED, "kW");
  lv_obj_set_style_pad_left(unit, 8, 0);
  lv_obj_set_style_pad_bottom(unit, 9, 0);  // baseline-align with the 144px value
}

// Source mix: segmented bar, always solar | grid | battery left to right.
// setMixShares() sets the segment widths in pixels.
static void buildMixBar(lv_obj_t *scr) {
  mixBar = makeFilledBox(scr, COL_CARD2, 10);
  lv_obj_set_pos(mixBar, MIX_X, MIX_Y);
  lv_obj_set_size(mixBar, MIX_W, MIX_H);
  lv_obj_set_flex_flow(mixBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(mixBar, MIX_GAP, 0);
  mixSolar = makeFilledBox(mixBar, COL_SOLAR_HI, 10);
  lv_obj_set_style_bg_grad_color(mixSolar, lv_color_hex(COL_SOLAR), 0);
  lv_obj_set_style_bg_grad_dir(mixSolar, LV_GRAD_DIR_HOR, 0);
  mixGrid = makeFilledBox(mixBar, COL_IMPORT, 10);
  mixBatt = makeFilledBox(mixBar, COL_DISCHARGE, 10);
  for (lv_obj_t *seg : { mixSolar, mixGrid, mixBatt }) {
    lv_obj_set_size(seg, 0, LV_PCT(100));
    lv_obj_add_flag(seg, LV_OBJ_FLAG_HIDDEN);
  }
}

// Home screen mode buttons: Charge (a toggle) over Free hour.
static void buildModeButtons(lv_obj_t *scr) {
  chargeBtn = makeButton(scr, MODE_BTN_X, CHARGE_BTN_Y, MODE_BTN_W, MODE_BTN_H);
  lv_obj_add_flag(chargeBtn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_style_bg_color(chargeBtn, tint(COL_RESERVE, COL_BG, 46), LV_STATE_CHECKED);
  lv_obj_set_style_bg_grad_color(chargeBtn, tint(COL_RESERVE, COL_BG, 128), LV_STATE_CHECKED);
  lv_obj_set_style_border_color(chargeBtn, lv_color_hex(COL_RESERVE), LV_STATE_CHECKED);
  lv_obj_set_style_border_width(chargeBtn, 2, LV_STATE_CHECKED);
  chargeIcon = makeStormIcon(chargeBtn);
  setIconColor(chargeIcon, COL_RESERVE);
  lv_obj_add_event_cb(chargeBtn, onChargeToggled, LV_EVENT_VALUE_CHANGED, nullptr);

  // not checkable: it's only shown pressed in while a release is pending
  freeBtn = makeButton(scr, MODE_BTN_X, FREE_BTN_Y, MODE_BTN_W, MODE_BTN_H);
  lv_obj_set_style_bg_color(freeBtn, tint(COL_FREE, COL_BG, 46), LV_STATE_CHECKED);
  lv_obj_set_style_bg_grad_color(freeBtn, tint(COL_FREE, COL_BG, 128), LV_STATE_CHECKED);
  lv_obj_set_style_border_color(freeBtn, lv_color_hex(COL_FREE), LV_STATE_CHECKED);
  lv_obj_set_style_border_width(freeBtn, 2, LV_STATE_CHECKED);
  freeIcon = makeClockIcon(freeBtn);
  setIconColor(freeIcon, COL_FREE);
  lv_obj_add_event_cb(freeBtn, onFreeHourOpen, LV_EVENT_CLICKED, nullptr);
}

// Shown while the plant still has remote EMS after a free hour: a red badge on
// the Free hour button's corner (on the screen, so the button doesn't clip it)
// and a red title beside the hero tile, where FREE HOUR sits during free hour.
static void buildPendingRelease(lv_obj_t *scr) {
  const int BADGE = 10, RING = 2;  // dot diameter, and a background ring round it
  const int cx = MODE_BTN_X + MODE_BTN_W - 3, cy = FREE_BTN_Y + 6;  // on the sunk button's corner
  pendingBadge = makeFilledBox(scr, COL_IMPORT, LV_RADIUS_CIRCLE);
  lv_obj_set_size(pendingBadge, BADGE + 2 * RING, BADGE + 2 * RING);
  lv_obj_set_pos(pendingBadge, cx - BADGE / 2 - RING, cy - BADGE / 2 - RING);
  lv_obj_set_style_border_color(pendingBadge, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(pendingBadge, RING, 0);

  pendingTitle = makeLabel(scr, &lv_font_montserrat_14, COL_IMPORT, "STILL GRID CHARGING");
  lv_obj_set_style_text_letter_space(pendingTitle, 2, 0);
  lv_obj_set_pos(pendingTitle, TILE_X + TILE_SIZE + 12, TILE_Y + (TILE_SIZE - 16) / 2);

  for (lv_obj_t *o : { pendingBadge, pendingTitle }) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Free-hour header and countdown row, hidden until free hour starts: the close
// button in the corner, then the hero tile (moved there by setFreeHourMode()),
// then the title. The kWh so far and the countdown take the mix bar's slot.
static void buildFreeHour(lv_obj_t *scr) {
  closeBtn = makeButton(scr, 10, 4, 38, 36);
  setIconColor(makeCloseIcon(closeBtn), COL_TEXT);
  lv_obj_set_ext_click_area(closeBtn, 10);  // small target in the corner, resistive touch
  lv_obj_add_event_cb(closeBtn, onFreeHourClose, LV_EVENT_CLICKED, nullptr);

  freeTitle = makeLabel(scr, &lv_font_montserrat_14, COL_FREE, "FREE HOUR");
  lv_obj_set_style_text_letter_space(freeTitle, 2, 0);
  lv_obj_set_pos(freeTitle, FREE_TILE_X + TILE_SIZE + 12, FREE_TILE_Y + (TILE_SIZE - 16) / 2);

  // values' baseline on the mix bar's bottom edge (font_num_44: line height 42, baseline 4)
  freeKwhTile = makeFilledBox(scr, COL_CARD, 9);
  lv_obj_set_style_bg_color(freeKwhTile, tileColor(COL_IMPORT), 0);
  lv_obj_set_size(freeKwhTile, FREE_KWH_TILE, FREE_KWH_TILE);
  lv_obj_set_pos(freeKwhTile, MIX_X, MIX_Y + MIX_H - FREE_KWH_TILE);
  setIconColor(makeGridIcon(freeKwhTile), COL_IMPORT);

  freeKwhRow = makeValueRow(scr, MIX_X + FREE_KWH_TILE + 12, MIX_Y + MIX_H - (42 - 4));
  freeKwhValue = makeLabel(freeKwhRow, &font_num_44, COL_TEXT, "-.-");
  lv_obj_t *unit = makeLabel(freeKwhRow, &lv_font_montserrat_16, COL_MUTED, "kWh");
  lv_obj_set_style_pad_left(unit, 5, 0);
  lv_obj_set_style_pad_bottom(unit, 1, 0);  // baseline-align with the 44px value

  freeCountdown = makeBox(scr);
  lv_obj_set_size(freeCountdown, COUNTDOWN_W, 42);
  lv_obj_set_pos(freeCountdown, MIX_X + FREE_W - COUNTDOWN_W, MIX_Y + MIX_H - (42 - 4));
  for (int i = 0, x = 0; i < 4; i++, x += CD_DIGIT_W) {
    if (i == 2) {
      lv_label_set_text(makeAlignedLabel(freeCountdown, &font_num_44, COL_FREE, x, 0, CD_COLON_W,
                                         LV_TEXT_ALIGN_CENTER), ":");
      x += CD_COLON_W;
    }
    cdDigits[i] = makeAlignedLabel(freeCountdown, &font_num_44, COL_FREE, x, 0, CD_DIGIT_W,
                                   LV_TEXT_ALIGN_CENTER);
  }

  for (lv_obj_t *o : { closeBtn, freeTitle, freeKwhTile, freeKwhRow, freeCountdown })
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// One "where it went" cell of the summary card: icon tile, label, kWh value.
static lv_obj_t *makeSummaryCell(lv_obj_t *card, int x, lv_obj_t *(*makeIcon)(lv_obj_t *), uint32_t col,
                                 const char *label, lv_obj_t **icon) {
  lv_obj_t *tile = makeFilledBox(card, COL_CARD, 9);
  lv_obj_set_style_bg_color(tile, tileColor(col), 0);
  lv_obj_set_size(tile, 34, 34);
  lv_obj_set_pos(tile, x, 156);
  *icon = makeIcon(tile);
  setIconColor(*icon, col);
  lv_obj_set_pos(makeLabel(card, &lv_font_montserrat_14, COL_MUTED, label), x + 44, 154);
  lv_obj_t *row = makeValueRow(card, x + 44, 164);
  lv_obj_t *value = makeLabel(row, &lv_font_montserrat_24, COL_TEXT, "");
  lv_obj_t *unit = makeLabel(row, &lv_font_montserrat_14, COL_MUTED, "kWh");
  lv_obj_set_style_pad_left(unit, 4, 0);
  lv_obj_set_style_pad_bottom(unit, 2, 0);  // baseline-align with the 24px value
  return value;
}

// Shown when a free hour ends: the kWh it took from the grid and when it ran,
// then how much went into the battery and the house. Hidden until then. Only
// fonts the rest of the screen already uses: flash is nearly full, and
// Montserrat 48 alone would take ~95 KB.
static void buildFreeSummary(lv_obj_t *scr) {
  summary = makeFilledBox(scr, COL_BG, 0);
  lv_obj_set_size(summary, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(summary, LV_OPA_80, 0);
  lv_obj_add_flag(summary, LV_OBJ_FLAG_CLICKABLE);  // the card's parts aren't, so a tap anywhere closes it
  lv_obj_add_event_cb(summary, onSummaryClose, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *card = makeFilledBox(summary, COL_CARD, 16);
  lv_obj_set_size(card, SUM_W, SUM_H);
  lv_obj_set_style_border_color(card, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_center(card);

  lv_obj_t *icon = makeClockIcon(card);
  setIconColor(icon, COL_FREE);
  lv_obj_set_pos(icon, 18, 16);
  lv_obj_t *title = makeLabel(card, &lv_font_montserrat_14, COL_FREE, "FREE HOUR DONE");
  lv_obj_set_style_text_letter_space(title, 2, 0);
  lv_obj_set_pos(title, 50, 20);

  // total and times centred; aligned (not placed) so they re-centre when the text changes
  lv_obj_t *row = makeValueRow(card, 0, 0);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 55);
  sumTotal = makeLabel(row, &font_num_44, COL_TEXT, "");
  lv_obj_t *unit = makeLabel(row, &lv_font_montserrat_24, COL_MUTED, "kWh");
  lv_obj_set_style_pad_left(unit, 6, 0);
  sumTimes = makeLabel(card, &lv_font_montserrat_16, COL_MUTED, "");
  lv_obj_align(sumTimes, LV_ALIGN_TOP_MID, 0, 108);

  lv_obj_t *div = makeFilledBox(card, COL_BORDER, 0);
  lv_obj_set_pos(div, 20, 142);
  lv_obj_set_size(div, SUM_W - 40, 1);
  lv_obj_t *houseIcon;
  sumBattValue  = makeSummaryCell(card, 20, makeBattIcon, COL_CHARGE, "Battery", &sumBattIcon);
  sumHouseValue = makeSummaryCell(card, 160, makeHouseIcon, COL_HOME, "House", &houseIcon);

  lv_obj_t *hint = makeLabel(card, &lv_font_montserrat_14, COL_MUTED, "tap to close");
  lv_obj_set_style_text_color(hint, tint(COL_MUTED, COL_CARD, 128), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

  lv_obj_add_flag(summary, LV_OBJ_FLAG_HIDDEN);
}

static void buildBattery(lv_obj_t *scr) {
  battNub = makeFilledBox(scr, COL_BORDER, 4);
  lv_obj_set_pos(battNub, BATT_X + (BATT_W - 44) / 2, BATT_Y - 9);
  lv_obj_set_size(battNub, 44, 11);

  battBody = makeFilledBox(scr, COL_CARD, 18);
  lv_obj_set_pos(battBody, BATT_X, BATT_Y);
  lv_obj_set_size(battBody, BATT_W, BATT_H);
  lv_obj_set_style_border_color(battBody, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(battBody, BATT_BORDER, 0);

  battFill = makeFilledBox(battBody, COL_BATT_HI, 12);
  lv_obj_set_width(battFill, BATT_W - 2 * BATT_BORDER - 2 * BATT_INSET);
  lv_obj_set_height(battFill, 0);
  lv_obj_align(battFill, LV_ALIGN_BOTTOM_MID, 0, -BATT_INSET);
  lv_obj_set_style_bg_grad_dir(battFill, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_shadow_width(battFill, 18, 0);
  lv_obj_set_style_shadow_opa(battFill, LV_OPA_40, 0);
  lv_obj_set_style_clip_corner(battFill, true, 0);  // keeps the glow inside the rounded fill

  for (GlowBand &b : glowBands) {
    b.rise = makeFilledBox(battFill, 0xffffff, 0);
    b.fall = makeFilledBox(battFill, 0xffffff, 0);
    for (lv_obj_t *o : { b.rise, b.fall }) {
      lv_obj_set_size(o, LV_PCT(100), GLOW_W / 2);
      lv_obj_set_style_bg_grad_color(o, lv_color_hex(0xffffff), 0);
      lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_main_opa(b.rise, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(b.fall, LV_OPA_TRANSP, 0);
    b.opa = LV_OPA_TRANSP;
  }
  lv_timer_create(glowTick, GLOW_TICK_MS, NULL);

  socRow = makeValueRow(battBody, 0, 0);
  socValue = makeLabel(socRow, &font_num_44, COL_TEXT, "--");
  socPctLabel = makeLabel(socRow, &lv_font_montserrat_16, COL_TEXT, "%");
  lv_obj_set_style_pad_left(socPctLabel, 2, 0);
  lv_obj_set_style_pad_bottom(socPctLabel, 1, 0);  // baseline-align with the 44px value
  lv_obj_center(socRow);
}

static void buildStrip(lv_obj_t *scr) {
  lv_obj_t *strip = makeFilledBox(scr, COL_CARD, 14);
  lv_obj_set_pos(strip, STRIP_X, STRIP_Y);
  lv_obj_set_size(strip, STRIP_W, STRIP_H);
  lv_obj_set_style_border_color(strip, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(strip, 1, 0);

  secSolar = makeSection(strip, 0, makeSunIcon, COL_SOLAR);
  secGrid  = makeSection(strip, SECTION_W, makeGridIcon, COL_IDLE);
  setIconColor(secGrid.icon, COL_IMPORT);  // icon stays red to match the mix bar
  secGrid.altIcon = makeHouseIcon(secGrid.tile);
  setIconColor(secGrid.altIcon, COL_HOME);
  lv_obj_add_flag(secGrid.altIcon, LV_OBJ_FLAG_HIDDEN);
  secBatt  = makeSection(strip, SECTION_W * 2, makeBattIcon, COL_IDLE);
  for (int i = 1; i <= 2; i++) {
    lv_obj_t *div = makeFilledBox(strip, COL_BORDER, 0);
    lv_obj_set_pos(div, SECTION_W * i, 14);
    lv_obj_set_size(div, 1, 70);
  }
}

// Full-screen status overlay, shown while (re)connecting/scanning. A themed
// lv_obj rather than makeBox(), so it's clickable and swallows taps.
static void buildOverlay(lv_obj_t *scr) {
  overlay = lv_obj_create(scr);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_radius(overlay, 0, 0);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(overlay);

  overlayLabel = makeLabel(overlay, &lv_font_montserrat_16, COL_TEXT, "Starting...");
  lv_label_set_long_mode(overlayLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(overlayLabel, LV_PCT(80));
  lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(overlayLabel);
}

// Widgets are created back to front: later ones draw on top.
void buildUi() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildTopBar(scr);
  buildHero(scr);
  buildMixBar(scr);
  buildModeButtons(scr);
  buildPendingRelease(scr);
  buildFreeHour(scr);
  buildBattery(scr);
  buildStrip(scr);
  buildFreeSummary(scr);
  buildOverlay(scr);
  layoutTopBar();
}

// ── connection status ────────────────────────────────────────────────────

static void spinCb(void *obj, int32_t angle) {
  lv_obj_set_style_transform_rotation((lv_obj_t *)obj, angle, 0);
}

// Small spinning arrow beside the plant IP while we reconnect.
static void showReconnecting(bool on) {
  if (on == !lv_obj_has_flag(reconnectIcon, LV_OBJ_FLAG_HIDDEN)) return;
  if (on) {
    lv_obj_remove_flag(reconnectIcon, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, reconnectIcon);
    lv_anim_set_exec_cb(&a, spinCb);
    lv_anim_set_values(&a, 0, 3600);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  } else {
    lv_anim_delete(reconnectIcon, spinCb);
    lv_obj_add_flag(reconnectIcon, LV_OBJ_FLAG_HIDDEN);
  }
}

void showStatus(const String &text) {
  if (haveData) {
    showReconnecting(true);
    return;
  }
  lv_label_set_text(overlayLabel, text.c_str());
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hideStatus() {
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  showReconnecting(false);
}

void showPlantIp(IPAddress ip) {
  lv_label_set_text(ipLabel, ip.toString().c_str());
}

// ── live values ──────────────────────────────────────────────────────────

// Sizes the source-mix segments in pixels. (LVGL's flex_grow divides the free
// space with integer maths, which left a lone 100% segment ~36px short.)
static void setMixShares(float solar, float grid, float batt) {
  const float MIN_SHARE = 0.005f;  // smaller shares get no segment
  lv_obj_t *segs[3] = { mixSolar, mixGrid, mixBatt };
  float shares[3] = { solar, grid, batt };
  int visible = 0;
  for (float sh : shares) if (sh > MIN_SHARE) visible++;
  int avail = MIX_W - MIX_GAP * (visible > 0 ? visible - 1 : 0);
  int used = 0, seen = 0;
  for (int i = 0; i < 3; i++) {
    if (shares[i] <= MIN_SHARE) {
      lv_obj_add_flag(segs[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    // last visible segment takes the remainder so the bar always ends flush
    int w = (++seen == visible) ? avail - used : (int)lroundf(avail * shares[i]);
    if (w < 4) w = 4;
    used += w;
    lv_obj_set_width(segs[i], w);
    lv_obj_remove_flag(segs[i], LV_OBJ_FLAG_HIDDEN);
  }
}

// Big number: house load, or grid import during free hour. Always 1 dp.
static void showHero(float homeKw) {
  float kw = freeHour ? fmaxf(reading.gridKw, 0) : homeKw;
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", kw);
  lv_label_set_text(heroValue, buf);
}

// Where the house load is coming from.
static void showSourceMix(float homeKw) {
  float fromSolar = fminf(homeKw, fmaxf(reading.pvKw, 0));
  float fromBatt  = fminf(homeKw - fromSolar, fmaxf(-reading.essKw, 0));
  float fromGrid  = fmaxf(homeKw - fromSolar - fromBatt, 0);
  // a source that reads "0.0" in the strip gets no segment; the rest fill the bar
  if (fromSolar < IDLE_KW) fromSolar = 0;
  if (fromBatt  < IDLE_KW) fromBatt  = 0;
  if (fromGrid  < IDLE_KW) fromGrid  = 0;
  float total = fromSolar + fromBatt + fromGrid;
  if (homeKw > IDLE_KW && total > 0) setMixShares(fromSolar / total, fromGrid / total, fromBatt / total);
  else                               setMixShares(0, 0, 0);
}

// ── flow glow ────────────────────────────────────────────────────────────

static void hideGlow() {
  if (glowShownPx < 0) return;
  glowShownPx = -1;
  for (GlowBand &b : glowBands) {
    lv_obj_add_flag(b.rise, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(b.fall, LV_OBJ_FLAG_HIDDEN);
  }
}

// The wave crosses the whole fill, then starts again at the other end.
static float glowPeriod() { return fmaxf(glowFillH, 1); }

// Places the wave for the current phase: its crest runs from the bottom of
// the fill (phase 0) to the top (phase = fill height).
static void placeGlow() {
  int px = (int)glowPhase;
  if (px == glowShownPx && !glowDirty) return;
  glowShownPx = px;
  glowDirty = false;
  // a band fades in and out across the fill's top and bottom edges rather
  // than being cut off there; shorter on a low fill, or it never shows at all
  float edge = fminf(10, glowFillH / 5.0f);
  for (int k = 0; k < GLOW_BANDS; k++) {
    GlowBand &b = glowBands[k];
    int crest = glowFillH - px + k * glowFillH;
    float fade = edge > 0 ? fminf(crest, glowFillH - crest) / edge : 0;
    lv_opa_t opa = (lv_opa_t)(GLOW_PEAK * fminf(fmaxf(fade, 0), 1));
    setVisible(b.rise, opa > 0);
    setVisible(b.fall, opa > 0);
    if (opa == 0) continue;
    lv_obj_set_y(b.rise, crest - GLOW_W / 2);
    lv_obj_set_y(b.fall, crest);
    if (opa != b.opa) {
      b.opa = opa;
      lv_obj_set_style_bg_grad_opa(b.rise, opa, 0);
      lv_obj_set_style_bg_main_opa(b.fall, opa, 0);
    }
  }
}

// LVGL timer: moves the bands on by however long it's been since the last tick.
static void glowTick(lv_timer_t *t) {
  static uint32_t last = lv_tick_get();
  uint32_t now = lv_tick_get();
  float dt = (now - last) / 1000.0f;
  last = now;
  if (glowSpeed == 0 || screenIsOff()) {
    hideGlow();
    return;
  }
  float period = glowPeriod();
  glowPhase = fmodf(glowPhase + glowSpeed * dt, period);
  if (glowPhase < 0) glowPhase += period;
  placeGlow();
}

// Glow speed from the battery power.
static void setGlow(int fillH) {
  float kw = reading.essKw;
  glowSpeed = fabsf(kw) < IDLE_KW ? 0
            : copysignf(fminf(GLOW_SPEED_BASE + GLOW_SPEED_PER_KW * fabsf(kw), GLOW_SPEED_MAX), kw);
  glowFillH = fillH;
  glowDirty = true;
}

static void showBattery(int soc) {
  uint32_t col = soc >= 50 ? COL_BATT_HI : (soc >= 20 ? COL_BATT_MID : COL_BATT_LOW);
  int fillH = BATT_FILL_MAX_H * soc / 100;
  lv_obj_set_height(battFill, fillH);
  lv_obj_set_style_bg_color(battFill, tint(0xffffff, col, 90), 0);
  lv_obj_set_style_bg_grad_color(battFill, tint(col, 0x000000, 204), 0);
  lv_obj_set_style_shadow_color(battFill, lv_color_hex(col), 0);
  setGlow(fillH);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", soc);
  lv_label_set_text(socValue, buf);
  // centre the number in the fill when it fits, otherwise in the empty space above it
  bool inFill = fillH >= 52;
  int centreY = inFill ? BATT_INNER_H - BATT_INSET - fillH / 2
                       : (BATT_INSET + BATT_INNER_H - BATT_INSET - fillH) / 2;
  uint32_t textCol = inFill ? COL_ON_FILL : COL_TEXT;
  lv_obj_set_style_text_color(socValue, lv_color_hex(textCol), 0);
  lv_obj_set_style_text_color(socPctLabel, lv_color_hex(textCol), 0);
  lv_obj_align(socRow, LV_ALIGN_CENTER, 0, centreY - BATT_INNER_H / 2);
}

static void showStrip(float homeKw, int soc) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", reading.pvKw);
  lv_label_set_text(secSolar.value, buf);
  setBarKw(secSolar.bar, reading.pvKw);

  if (freeHour) {
    // grid and house swap places: the house load takes the grid's strip section
    snprintf(buf, sizeof(buf), "%.1f", homeKw);
    lv_label_set_text(secGrid.value, buf);
    setSectionColor(secGrid, COL_HOME);
    lv_obj_set_style_text_color(secGrid.value, lv_color_hex(COL_TEXT), 0);
    setBarKw(secGrid.bar, homeKw);
  } else {
    float gridKw = reading.gridKw;
    bool importing = gridKw > IDLE_KW, exporting = gridKw < -IDLE_KW;
    // sign only when exporting, so an idle grid reads "0.0" rather than "-0.0"
    snprintf(buf, sizeof(buf), "%.1f", exporting ? gridKw : fabsf(gridKw));
    lv_label_set_text(secGrid.value, buf);
    setSectionColor(secGrid, importing ? COL_IMPORT : (exporting ? COL_EXPORT : COL_IDLE));
    setIconColor(secGrid.icon, COL_IMPORT);  // icon stays red to match the mix bar
    lv_obj_set_style_text_color(secGrid.value, lv_color_hex(gridKw < -IDLE_KW ? COL_MUTED : COL_TEXT), 0);
    setBarKw(secGrid.bar, gridKw);
  }

  // no sign when the battery is idle, so it reads "0.0" rather than "+0.0"/"-0.0"
  float essKw = reading.essKw;
  if (fabsf(essKw) < IDLE_KW) snprintf(buf, sizeof(buf), "0.0");
  else snprintf(buf, sizeof(buf), "%+.1f", essKw);
  lv_label_set_text(secBatt.value, buf);
  uint32_t flowCol = essKw > IDLE_KW ? COL_CHARGE : (essKw < -IDLE_KW ? COL_DISCHARGE : COL_IDLE);
  setSectionColor(secBatt, flowCol);
  lv_obj_set_style_text_color(secBatt.value, lv_color_hex(essKw < -IDLE_KW ? COL_MUTED : COL_TEXT), 0);
  setBattIconLevel(secBatt.icon, soc);
  setBarKw(secBatt.bar, essKw);
}

// Redraws every value from `reading`, placed for the current mode.
static void updateUi() {
  float homeKw = reading.pvKw + reading.gridKw - reading.essKw;
  if (homeKw < 0) homeKw = 0;
  int soc = (int)lroundf(reading.socPct);
  if (soc < 0) soc = 0;
  if (soc > 100) soc = 100;

  showHero(homeKw);
  showSourceMix(homeKw);
  showBattery(soc);
  showStrip(homeKw, soc);
}

void showReading(const PlantReading &r) {
  reading = r;
  haveData = true;
  hideStatus();
  updateUi();
}

// ── clock ────────────────────────────────────────────────────────────────

void updateClock() {
  static int lastMinute = -1;
  time_t now = time(nullptr);
  if (now < CLOCK_SYNCED_AFTER) return;
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_min == lastMinute) return;
  lastMinute = t.tm_min;
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  lv_label_set_text(timeLabel, buf);
  layoutTopBar();

  bool offPeak = t.tm_hour >= OFFPEAK_START_HOUR || t.tm_hour < OFFPEAK_END_HOUR;
  lv_obj_set_style_text_color(heroValue, lv_color_hex(offPeak ? COL_OFFPEAK : COL_TEXT), 0);
}

// ── modes ────────────────────────────────────────────────────────────────
// Charge: a toggle that stays pressed in until it's pressed again.
// Free hour: swaps grid import into the hero and house load into the strip,
// and counts down to the top of the hour, then returns home by itself (or
// via the close button).

void showChargeOn(bool on) {
  if (on) lv_obj_add_state(chargeBtn, LV_STATE_CHECKED);
  else    lv_obj_remove_state(chargeBtn, LV_STATE_CHECKED);
  setIconColor(chargeIcon, on ? COL_TEXT : COL_RESERVE);
  // battery outline picks up the Charge colour so the mode shows across the room
  lv_color_t edge = on ? tint(COL_RESERVE, COL_BG, 191) : lv_color_hex(COL_BORDER);
  lv_obj_set_style_border_color(battBody, edge, 0);
  lv_obj_set_style_bg_color(battNub, edge, 0);
}

bool chargeButtonOn() {
  return lv_obj_has_state(chargeBtn, LV_STATE_CHECKED);
}

static void onChargeToggled(lv_event_t *e) {
  bool on = chargeButtonOn();
  showChargeOn(on);
  Serial.printf("Charge mode %s\n", on ? "on" : "off");
  applyChargeMode(on);
}

static void showSecsLeft(int secs) {
  if (secs == shownSecsLeft) return;
  shownSecsLeft = secs;
  int digits[4] = { secs / 600, secs / 60 % 10, secs % 60 / 10, secs % 10 };
  for (int i = 0; i < 4; i++) {
    char d[2] = { (char)('0' + digits[i]), 0 };
    lv_label_set_text(cdDigits[i], d);
  }
}

// The pending-release look only shows on the home screen.
static void showPendingRelease() {
  bool show = releasePending && !freeHour;
  setVisible(pendingBadge, show);
  setVisible(pendingTitle, show);
  if (show) lv_obj_add_state(freeBtn, LV_STATE_CHECKED);
  else      lv_obj_remove_state(freeBtn, LV_STATE_CHECKED);
  setIconColor(freeIcon, show ? COL_TEXT : COL_FREE);
}

void showReleasePending(bool on) {
  if (on == releasePending) return;
  releasePending = on;
  showPendingRelease();
}

static void setFreeHourMode(bool on) {
  freeHour = on;
  for (lv_obj_t *o : { chargeBtn, freeBtn, mixBar }) setVisible(o, !on);
  for (lv_obj_t *o : { closeBtn, freeTitle, freeKwhTile, freeKwhRow, freeCountdown }) setVisible(o, on);
  showPendingRelease();

  // hero tile moves right of the close button and shows the grid; strip shows the house
  lv_obj_set_pos(heroTile, on ? FREE_TILE_X : TILE_X, on ? FREE_TILE_Y : TILE_Y);
  lv_obj_set_style_bg_color(heroTile, tileColor(on ? COL_IMPORT : COL_HOME), 0);
  setVisible(heroHouseIcon, !on);
  setVisible(heroGridIcon, on);
  setVisible(secGrid.icon, !on);
  setVisible(secGrid.altIcon, on);

  if (haveData) updateUi();
}

// Counts down to the top of the current hour by the wall clock. While a
// release is pending, a tap sends it again instead.
static void onFreeHourOpen(lv_event_t *e) {
  if (releasePending) {
    Serial.println("Free hour button: sending the remote EMS release again");
    applyFreeHourMode(false);
    return;
  }
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < CLOCK_SYNCED_AFTER) {
    Serial.println("Free hour: clock not synced yet, ignoring");
    return;
  }
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  uint32_t msLeft = (3600 - (t.tm_min * 60 + t.tm_sec)) * 1000UL - tv.tv_usec / 1000;
  freeHourEndMs = millis() + msLeft;
  freeStartedAt = tv.tv_sec;
  shownSecsLeft = -1;
  lv_label_set_text(freeKwhValue, "-.-");  // until the plant's counters have been read
  showSecsLeft((msLeft + 999) / 1000);
  setFreeHourMode(true);
  Serial.printf("Free hour started, %lu s to go\n", (unsigned long)(msLeft / 1000));
  applyFreeHourMode(true);
}

static void endFreeHour() {
  freeEndedAt = time(nullptr);
  setFreeHourMode(false);
  Serial.println("Free hour ended");
  applyFreeHourMode(false);
}

static void onFreeHourClose(lv_event_t *e) {
  endFreeHour();
}

void updateCountdown() {
  if (!freeHour) return;
  int32_t msLeft = (int32_t)(freeHourEndMs - millis());
  if (msLeft <= 0) {
    endFreeHour();
    return;
  }
  showSecsLeft((msLeft + 999) / 1000);
}

// Whole kWh from 10 up, like the hero.
static void showKwh(lv_obj_t *label, float kwh) {
  char buf[16];
  snprintf(buf, sizeof(buf), kwh >= 9.95f ? "%.0f" : "%.1f", kwh);
  lv_label_set_text(label, buf);
}

void showFreeHourEnergy(const FreeHourEnergy &e) {
  if (freeHour) showKwh(freeKwhValue, e.gridKwh);
}

void showFreeHourSummary(const FreeHourEnergy &e) {
  showKwh(sumTotal, e.gridKwh);
  showKwh(sumBattValue, e.battKwh);
  showKwh(sumHouseValue, e.houseKwh);
  setBattIconLevel(sumBattIcon, (int)lroundf(reading.socPct));
  char from[8], to[8], buf[24];
  struct tm t;
  strftime(from, sizeof(from), "%H:%M", localtime_r(&freeStartedAt, &t));
  strftime(to, sizeof(to), "%H:%M", localtime_r(&freeEndedAt, &t));
  snprintf(buf, sizeof(buf), "%s - %s", from, to);
  lv_label_set_text(sumTimes, buf);
  lv_obj_remove_flag(summary, LV_OBJ_FLAG_HIDDEN);
}

static void onSummaryClose(lv_event_t *e) {
  lv_obj_add_flag(summary, LV_OBJ_FLAG_HIDDEN);
}
