#include <pebble.h>
#include <math.h>

// ---- Layout constants (tuned for emery 200x228; computed from bounds at runtime) ----
#define TICK_OUTER_INSET        2   // minor ticks start this far in from the edge
#define TICK_LEN                6   // minor tick length
#define MINOR_PER_HOUR          3   // minor ticks between adjacent hour numerals
#define CENTER_CIRCLE_R        17

static Window *s_window;
static Layer *s_face_layer;
static bool s_bt_connected = true;

// ---- weather model ----
// Compositional: a sun OR moon is ALWAYS present; a cloud and precip build on top.
typedef enum { CLOUD_NONE = 0, CLOUD_SMALL = 1, CLOUD_LARGE = 2 } WxCloud;
typedef enum { PRECIP_NONE = 0, PRECIP_RAIN = 1, PRECIP_SNOW = 2, PRECIP_STORM = 3 } WxPrecip;

typedef struct {
  bool     night;   // moon instead of sun
  WxCloud  cloud;   // none / small (partly) / large (overcast)
  WxPrecip precip;  // none / rain / snow / storm  (forces a large cloud)
  uint8_t  count;   // precip intensity: number of rays/flakes/bolts (0..4)
} WxCell;

// per clock position k=0..11 (k=0 is the 12 o'clock spot, k*30 degrees)
static WxCell s_wx[12];
static int    s_wx_temp[12];
static bool   s_wx_valid[12];

static int    s_cur_temp;        // current temperature (°F)
static bool   s_cur_temp_valid;
static WxCell s_cur_wx;          // current weather (for the center icon)
static int    s_start_k = -1;    // clock slot of the first hour in the chain (from JS)
static int    s_moon_phase = 5;  // 0=new, 5=full (index into the 10 moon bitmaps)
static int    s_sunrise_k = -1, s_sunset_k = -1;       // slots that show a time
static char   s_sunrise_str[8] = "", s_sunset_str[8] = "";
// display mode: 0 = inner numerals (temp on each icon),
//               1 = hash marks + bigger icons (temp on each icon),
//               2 = temps on the inner ring (icons uncovered, spotted moons)
//               3 = none: clean inner ring, bigger icons (like hash, no ticks)
enum { MODE_NUMERALS = 0, MODE_HASH = 1, MODE_TEMPS = 2, MODE_NONE = 3 };
static int    s_display_mode = MODE_NUMERALS;      // setting: default inner-ring style

// Tide: a blue "tank" drawn behind each inner-ring slot, filled to that hour's
// tide level. s_tide[k] is 0..100 per clock slot; shown only when the user
// enables it and the phone found a nearby tide station.
static uint8_t s_tide[12];
static bool    s_tide_valid = false;   // phone has tide data (nearby station)
static bool    s_show_tide = false;    // setting: draw the tide tanks
static bool    s_show_date = true;     // setting: show date badge at 5 o'clock
static bool    s_show_battery = true;  // setting: show battery badge always
static int     s_battery_threshold = 15; // setting: also show battery when under this %

// ---- helpers ----
static GPoint point_on_circle(GPoint center, int radius, int32_t angle) {
  return GPoint(
    center.x + (sin_lookup(angle) * radius / TRIG_MAX_RATIO),
    center.y - (cos_lookup(angle) * radius / TRIG_MAX_RATIO)
  );
}

// Project a ray from center at `angle` onto a rectangle of half-extents (hw, hh).
static GPoint point_on_rect(GPoint center, int hw, int hh, int32_t angle) {
  int32_t dx = sin_lookup(angle);
  int32_t dy = -cos_lookup(angle);
  int32_t adx = dx < 0 ? -dx : dx;
  int32_t ady = dy < 0 ? -dy : dy;
  // s = distance (px) to the nearer of the vertical / horizontal edges
  int64_t sx = adx ? (int64_t)hw * TRIG_MAX_RATIO / adx : INT64_MAX;
  int64_t sy = ady ? (int64_t)hh * TRIG_MAX_RATIO / ady : INT64_MAX;
  int64_t s = sx < sy ? sx : sy;
  return GPoint(
    center.x + (int)(s * dx / TRIG_MAX_RATIO),
    center.y + (int)(s * dy / TRIG_MAX_RATIO)
  );
}

// ---- weather icons (Option A): a celestial layer + an optional cloud overlay
// composited at runtime, so the moon phase shows everywhere from one source.
static GBitmap *s_sun_ring, *s_sun_ctr;          // full sun (clear day + behind day clouds)
static GBitmap *s_ov_ring[3], *s_ov_ctr[3];      // cloud overlays: 0 partly,1 overcast,2 precip(bare dark)
static GBitmap *s_flake_ring, *s_flake_ctr;      // snowflake sprite, stamped per precip count
static GBitmap *s_moon_ring, *s_moon_ctr;        // phase moon (one size), current phase
static int      s_loaded_phase = -1;

// Only the current phase's moons live in RAM; reload on change.
static void load_moon_phase(int p) {
  if (p < 0 || p > 9) p = 5;
  if (p == s_loaded_phase) return;
  if (s_moon_ring) gbitmap_destroy(s_moon_ring);
  if (s_moon_ctr)  gbitmap_destroy(s_moon_ctr);
  // ring moon set depends on the mode: clean 42 (numerals), clean 46 (hash),
  // or the spotted 42 (temps mode, where the moon isn't covered by a temp).
  const uint32_t mr_sm[10] = { RESOURCE_ID_MOON_RING_0, RESOURCE_ID_MOON_RING_1, RESOURCE_ID_MOON_RING_2,
    RESOURCE_ID_MOON_RING_3, RESOURCE_ID_MOON_RING_4, RESOURCE_ID_MOON_RING_5, RESOURCE_ID_MOON_RING_6,
    RESOURCE_ID_MOON_RING_7, RESOURCE_ID_MOON_RING_8, RESOURCE_ID_MOON_RING_9 };
  const uint32_t mr_xl[10] = { RESOURCE_ID_MOON_XL_0, RESOURCE_ID_MOON_XL_1, RESOURCE_ID_MOON_XL_2,
    RESOURCE_ID_MOON_XL_3, RESOURCE_ID_MOON_XL_4, RESOURCE_ID_MOON_XL_5, RESOURCE_ID_MOON_XL_6,
    RESOURCE_ID_MOON_XL_7, RESOURCE_ID_MOON_XL_8, RESOURCE_ID_MOON_XL_9 };
  const uint32_t mr_sp[10] = { RESOURCE_ID_MOON_SP_0, RESOURCE_ID_MOON_SP_1, RESOURCE_ID_MOON_SP_2,
    RESOURCE_ID_MOON_SP_3, RESOURCE_ID_MOON_SP_4, RESOURCE_ID_MOON_SP_5, RESOURCE_ID_MOON_SP_6,
    RESOURCE_ID_MOON_SP_7, RESOURCE_ID_MOON_SP_8, RESOURCE_ID_MOON_SP_9 };
  // Clock Numbers + Hashes share the bigger XL (spotted) moon; Temperature
  // uses the 42px spotted moon to match its smaller icons.
  const uint32_t *mr = (s_display_mode == MODE_TEMPS) ? mr_sp : mr_xl;
  (void)mr_sm;
  const uint32_t mc[10] = { RESOURCE_ID_MOON_CTR_0, RESOURCE_ID_MOON_CTR_1, RESOURCE_ID_MOON_CTR_2,
    RESOURCE_ID_MOON_CTR_3, RESOURCE_ID_MOON_CTR_4, RESOURCE_ID_MOON_CTR_5, RESOURCE_ID_MOON_CTR_6,
    RESOURCE_ID_MOON_CTR_7, RESOURCE_ID_MOON_CTR_8, RESOURCE_ID_MOON_CTR_9 };
  s_moon_ring = gbitmap_create_with_resource(mr[p]);
  s_moon_ctr  = gbitmap_create_with_resource(mc[p]);
  s_loaded_phase = p;
}

static void load_weather_icons(void) {
  // Clock Numbers + Hashes use the bigger 46px XL ring icons; Temperature 42px.
  bool big = (s_display_mode != MODE_TEMPS);
  s_sun_ring = gbitmap_create_with_resource(
      big ? RESOURCE_ID_WX_CLEAR_DAY_XL : RESOURCE_ID_WX_CLEAR_DAY);
  s_sun_ctr  = gbitmap_create_with_resource(RESOURCE_ID_WX_CLEAR_DAY_LG);
  if (big) {
    s_ov_ring[0] = gbitmap_create_with_resource(RESOURCE_ID_OV_PARTLY_XL);
    s_ov_ring[1] = gbitmap_create_with_resource(RESOURCE_ID_OV_OVERCAST_XL);
    s_ov_ring[2] = gbitmap_create_with_resource(RESOURCE_ID_OV_PRECIP_XL);
    s_flake_ring = gbitmap_create_with_resource(RESOURCE_ID_FLAKE_XL);
  } else {
    s_ov_ring[0] = gbitmap_create_with_resource(RESOURCE_ID_OV_PARTLY_RING);
    s_ov_ring[1] = gbitmap_create_with_resource(RESOURCE_ID_OV_OVERCAST_RING);
    s_ov_ring[2] = gbitmap_create_with_resource(RESOURCE_ID_OV_PRECIP_RING);
    s_flake_ring = gbitmap_create_with_resource(RESOURCE_ID_FLAKE_RING);
  }
  s_ov_ctr[0] = gbitmap_create_with_resource(RESOURCE_ID_OV_PARTLY_CTR);
  s_ov_ctr[1] = gbitmap_create_with_resource(RESOURCE_ID_OV_OVERCAST_CTR);
  s_ov_ctr[2] = gbitmap_create_with_resource(RESOURCE_ID_OV_PRECIP_CTR);
  s_flake_ctr = gbitmap_create_with_resource(RESOURCE_ID_FLAKE_CTR);
  load_moon_phase(s_moon_phase);
}

static void unload_weather_icons(void) {
  // NULL each pointer after destroying it: the toggle reloads via
  // load_weather_icons()/load_moon_phase(), which free any non-NULL pointer
  // first — a dangling (freed-but-non-NULL) pointer would double-free + crash.
  if (s_sun_ring) { gbitmap_destroy(s_sun_ring); s_sun_ring = NULL; }
  if (s_sun_ctr)  { gbitmap_destroy(s_sun_ctr);  s_sun_ctr = NULL; }
  for (int i = 0; i < 3; i++) {
    if (s_ov_ring[i]) { gbitmap_destroy(s_ov_ring[i]); s_ov_ring[i] = NULL; }
    if (s_ov_ctr[i])  { gbitmap_destroy(s_ov_ctr[i]);  s_ov_ctr[i] = NULL; }
  }
  if (s_flake_ring) { gbitmap_destroy(s_flake_ring); s_flake_ring = NULL; }
  if (s_flake_ctr)  { gbitmap_destroy(s_flake_ctr);  s_flake_ctr = NULL; }
  if (s_moon_ring) { gbitmap_destroy(s_moon_ring); s_moon_ring = NULL; }
  if (s_moon_ctr)  { gbitmap_destroy(s_moon_ctr);  s_moon_ctr = NULL; }
}

static int wx_state(WxCell w) {
  if (w.precip == PRECIP_RAIN)  return 3;
  if (w.precip == PRECIP_SNOW)  return 4;
  if (w.precip == PRECIP_STORM) return 5;
  if (w.cloud == CLOUD_NONE)    return 0;
  if (w.cloud == CLOUD_SMALL)   return 1;
  return 2;  // overcast
}

static void blit_icon(GContext *ctx, GPoint c, GBitmap *bmp) {
  if (!bmp) return;
  GRect b = gbitmap_get_bounds(bmp);
  GRect dst = GRect(c.x - b.size.w / 2, c.y - b.size.h / 2, b.size.w, b.size.h);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);  // honor transparency
  graphics_draw_bitmap_in_rect(ctx, bmp, dst);
}

// Behind a precip cloud the celestial is clipped just below the cloud's top
// (~40% of the icon): the crown shows above the cloud, the clip edge hides
// behind it (no gap), and the sun's lower side/bottom rays -- which would
// otherwise poke out past the narrower cloud edges -- are cut off.
#define CROWN_FRAC_PCT 40

static void blit_icon_crown(GContext *ctx, GPoint c, GBitmap *bmp) {
  if (!bmp) return;
  GRect b = gbitmap_get_bounds(bmp);
  int crown_h = b.size.h * CROWN_FRAC_PCT / 100;
  if (crown_h < 1) crown_h = 1;
  GBitmap *sub = gbitmap_create_as_sub_bitmap(bmp, GRect(0, 0, b.size.w, crown_h));
  if (!sub) { blit_icon(ctx, c, bmp); return; }
  GRect dst = GRect(c.x - b.size.w / 2, c.y - b.size.h / 2, b.size.w, crown_h);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, sub, dst);
  gbitmap_destroy(sub);
}

// Blit a bitmap centered at an arbitrary screen point (not the icon center).
static void blit_at(GContext *ctx, GBitmap *bmp, int cx, int cy) {
  if (!bmp) return;
  GRect b = gbitmap_get_bounds(bmp);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp,
      GRect(cx - b.size.w / 2, cy - b.size.h / 2, b.size.w, b.size.h));
}

// n precip marks in the band below the cloud mass. Rays/bolts are drawn with a
// black outline (like the hands); snow stamps the flake sprite. `S` = icon size.
static void draw_precip_marks(GContext *ctx, GPoint c, int S, int st, int n, bool center) {
  int left = c.x - S / 2, top = c.y - S / 2;
  int cloud_bottom = top + S * 80 / 100;   // cloud mass ends ~0.80 down the icon
  int bottom = top + S - 1;
  int lo = (st == 4) ? 14 : 24, hi = (st == 4) ? 86 : 76;   // snow spreads wider
  for (int i = 0; i < n; i++) {
    int xpct = (n == 1) ? 50 : lo + (hi - lo) * i / (n - 1);
    int x = left + S * xpct / 100;
    if (st == 4) {                                       // snow: stamp a flake
      int cy = cloud_bottom + (bottom - cloud_bottom) * 45 / 100;
      blit_at(ctx, center ? s_flake_ctr : s_flake_ring, x, cy);
    } else if (st == 3) {                                // rain: blue diagonal ray
      int run = S * 9 / 100;
      int y0 = cloud_bottom - S * 10 / 100, y1 = bottom - S * 2 / 100;
      int iw = S * 60 / 1000; if (iw < 2) iw = 2;   // blue core
      int ow = iw + 2;                               // black edge: 1px each side, all sizes
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, (uint8_t)ow);
      graphics_draw_line(ctx, GPoint(x, y0), GPoint(x - run, y1));
      graphics_context_set_stroke_color(ctx, GColorPictonBlue);
      graphics_context_set_stroke_width(ctx, (uint8_t)iw);
      graphics_draw_line(ctx, GPoint(x, y0), GPoint(x - run, y1));
    } else {                                             // storm: yellow bolt
      int bw = S * 30 / 100, bh = S * 42 / 100;
      int bx = x - bw / 2, by = cloud_bottom - S * 12 / 100;
      static const int nx[7] = {520, 180, 460, 300, 840, 540, 700};   // *1000
      static const int ny[7] = {0, 560, 560, 1000, 400, 400, 0};
      GPoint pts[7];
      for (int j = 0; j < 7; j++) pts[j] = GPoint(nx[j] * bw / 1000, ny[j] * bh / 1000);
      GPathInfo info = { .num_points = 7, .points = pts };
      GPath *gp = gpath_create(&info);
      gpath_move_to(gp, GPoint(bx, by));
      graphics_context_set_fill_color(ctx, GColorYellow);
      gpath_draw_filled(ctx, gp);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, 1);
      gpath_draw_outline(ctx, gp);
      gpath_destroy(gp);
    }
  }
}

// Weather glyph = celestial (sun/moon, clipped to a crown for precip) + a cloud
// overlay, plus C-drawn precip marks whose count tracks the precip probability.
static void draw_wx(GContext *ctx, GPoint c, WxCell w, bool center) {
  int st = wx_state(w);   // 0 clear,1 partly,2 overcast,3 rain,4 snow,5 storm
  GBitmap *cel = w.night ? (center ? s_moon_ctr : s_moon_ring)
                         : (center ? s_sun_ctr  : s_sun_ring);
  bool precip = (st >= 3);
  if (precip) blit_icon_crown(ctx, c, cel);
  else        blit_icon(ctx, c, cel);

  GBitmap *ov = NULL;
  if (st == 1)      ov = center ? s_ov_ctr[0] : s_ov_ring[0];   // partly
  else if (st == 2) ov = center ? s_ov_ctr[1] : s_ov_ring[1];   // overcast
  else if (precip)  ov = center ? s_ov_ctr[2] : s_ov_ring[2];   // bare dark cloud
  if (ov) blit_icon(ctx, c, ov);

  if (precip && w.count > 0 && ov) {
    GRect b = gbitmap_get_bounds(ov);
    draw_precip_marks(ctx, c, b.size.w, st, w.count, center);
  }
}

// Draw text with a 1px black outline so it stays legible over the hands, the
// icons, or the black face without needing a filled background behind it.
static void draw_text_outlined(GContext *ctx, const char *txt, GFont f, GRect box, GColor fill) {
  graphics_context_set_text_color(ctx, GColorBlack);
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx == 0 && dy == 0) continue;
      graphics_draw_text(ctx, txt, f,
                         GRect(box.origin.x + dx, box.origin.y + dy, box.size.w, box.size.h),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
  }
  graphics_context_set_text_color(ctx, fill);
  graphics_draw_text(ctx, txt, f, box, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// A short string (temp or sunrise/sunset time) centered at c, outlined in black.
// Times use the same bold font as temps so they match in weight/brightness.
static void draw_badge_circle(GContext *ctx, GPoint c, const char *txt, GColor txt_col) {
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GSize sz = graphics_text_layout_get_content_size(
      txt, f, GRect(0, 0, 60, 40), GTextOverflowModeFill, GTextAlignmentCenter);
  draw_text_outlined(ctx, txt, f, GRect(c.x - 30, c.y - sz.h / 2 - 4, 60, sz.h), txt_col);
}

// Hourly ring: each position is a bigger weather icon with its temp on a
// circle in the middle (same treatment as the center). Icons hug the screen
// edge (placed on the true clock radial, inset by ~half the 42px icon).
#define ICON_EDGE_INSET 22
static void draw_icon_ring(GContext *ctx, GPoint center, int hw, int hh) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  // Clock Numbers + Hashes -> 46px icons (half 23); Temperature -> 42px
  int inset = (s_display_mode == MODE_TEMPS) ? ICON_EDGE_INSET : 23;
  // start slot comes from the companion (location-correct); fall back to watch time
  int start_k = (s_start_k >= 0) ? s_start_k : ((t->tm_hour + 1) % 12);
  for (int k = 0; k < 12; k++) {
    if (!s_wx_valid[k]) continue;
    int32_t angle = TRIG_MAX_ANGLE * k / 12;
    GPoint iconp = point_on_rect(center, hw - inset, hh - inset, angle);

    draw_wx(ctx, iconp, s_wx[k], false);

    // In temps mode the temperature lives on the inner ring, so leave the icon
    // uncovered here. Modes 0/1 keep the temp (or sunrise/sunset) on the icon.
    if (s_display_mode == MODE_TEMPS) continue;

    char buf[8];
    GColor col = GColorWhite;
    if (k == s_sunrise_k && s_sunrise_str[0]) {
      strncpy(buf, s_sunrise_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    } else if (k == s_sunset_k && s_sunset_str[0]) {
      strncpy(buf, s_sunset_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    } else {
      snprintf(buf, sizeof(buf), "%d", s_wx_temp[k]);
      if (k == start_k) col = GColorGreen;
    }
    draw_badge_circle(ctx, iconp, buf, col);
  }
}

// Hour numerals 1-12 on the INNER ring.
#define NUM_RING_PCT 52          // pulled in slightly from the icons
#define TEMP_ICON_INSET 32       // mode 2: temps sit this far inside their icon
                                 // (rectangle-aware: top/bottom icons are farther
                                 // out, so their temps sit farther out too)
static void draw_hour_numerals(GContext *ctx, GPoint center, int hw, int hh) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  for (int h = 1; h <= 12; h++) {
    int32_t angle = TRIG_MAX_ANGLE * h / 12;
    GPoint edge = point_on_rect(center, hw, hh, angle);
    GPoint p = GPoint(center.x + (edge.x - center.x) * NUM_RING_PCT / 100,
                      center.y + (edge.y - center.y) * NUM_RING_PCT / 100);
    char buf[3];
    snprintf(buf, sizeof(buf), "%d", h);
    GSize sz = graphics_text_layout_get_content_size(
        buf, font, GRect(0, 0, 40, 40), GTextOverflowModeFill, GTextAlignmentCenter);
    draw_text_outlined(ctx, buf, font, GRect(p.x - 20, p.y - sz.h / 2 - 3, 40, sz.h), GColorWhite);
  }
}

// Temps on the INNER ring (mode 2): the hourly temperature (or sunrise/sunset
// time) where the numerals would be, with 12/3/6/9 bold. This frees the outer
// icons so the (spotted) moons show uncovered.
static void draw_temp_ring(GContext *ctx, GPoint center, int hw, int hh) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int start_k = (s_start_k >= 0) ? s_start_k : ((t->tm_hour + 1) % 12);
  GFont bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont reg  = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  for (int k = 0; k < 12; k++) {
    if (!s_wx_valid[k]) continue;
    int32_t angle = TRIG_MAX_ANGLE * k / 12;
    int32_t dxk = sin_lookup(angle), dyk = -cos_lookup(angle);
    // sit a constant gap inside this slot's icon -> follows the rectangle, so
    // top/bottom temps land farther out than the narrow 3/9 sides (no overlap).
    GPoint ic = point_on_rect(center, hw - ICON_EDGE_INSET, hh - ICON_EDGE_INSET, angle);
    int icon_dist = ((ic.x - center.x) * dxk + (ic.y - center.y) * dyk) / TRIG_MAX_RATIO;
    int temp_dist = icon_dist - TEMP_ICON_INSET;
    GPoint p = GPoint(center.x + dxk * temp_dist / TRIG_MAX_RATIO,
                      center.y + dyk * temp_dist / TRIG_MAX_RATIO);
    char buf[8];
    GColor col = GColorWhite;
    if (k == s_sunrise_k && s_sunrise_str[0]) {
      strncpy(buf, s_sunrise_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    } else if (k == s_sunset_k && s_sunset_str[0]) {
      strncpy(buf, s_sunset_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    } else {
      snprintf(buf, sizeof(buf), "%d", s_wx_temp[k]);
      if (k == start_k) col = GColorGreen;
    }
    GFont font = (k % 3 == 0) ? bold : reg;   // 12/3/6/9 bold
    GSize sz = graphics_text_layout_get_content_size(
        buf, font, GRect(0, 0, 40, 40), GTextOverflowModeFill, GTextAlignmentCenter);
    draw_text_outlined(ctx, buf, font, GRect(p.x - 20, p.y - sz.h / 2 - 3, 40, sz.h), col);
  }
}

// Inner-ring hash marks (numerals-off mode): major at 12/3/6/9, minor elsewhere.
// Each tick's outer tip sits a CONSTANT gap inside its icon, so the icon->hash
// spacing is uniform all the way around (not a scaled rectangle, which bunched
// at the corners). Geometry mirrors the off-mode icon ring (46px, inset 23).
#define HASH_ICON_INSET 23   // matches draw_icon_ring off-mode inset
#define HASH_ICON_HALF  23   // half of the 46px icon
#define HASH_GAP         2   // px between icon inner edge and hash tip (3/9 look)
static void draw_hash_ticks(GContext *ctx, GPoint center, int hw, int hh) {
  for (int k = 0; k < 12; k++) {
    int32_t angle = TRIG_MAX_ANGLE * k / 12;
    int32_t dx = sin_lookup(angle), dy = -cos_lookup(angle);
    bool major = (k % 3 == 0);                 // 12, 3, 6, 9
    int len = major ? 13 : 7;
    // distance from center to the icon center along this radial
    GPoint ic = point_on_rect(center, hw - HASH_ICON_INSET, hh - HASH_ICON_INSET, angle);
    int icon_dist = ((ic.x - center.x) * dx + (ic.y - center.y) * dy) / TRIG_MAX_RATIO;
    int tip = icon_dist - HASH_ICON_HALF - HASH_GAP;   // outer end of the tick
    int mid_dist = tip - len / 2;
    GPoint mid = GPoint(center.x + dx * mid_dist / TRIG_MAX_RATIO,
                        center.y + dy * mid_dist / TRIG_MAX_RATIO);
    GPoint p1 = GPoint(mid.x - len * dx / (2 * TRIG_MAX_RATIO),
                       mid.y - len * dy / (2 * TRIG_MAX_RATIO));
    GPoint p2 = GPoint(mid.x + len * dx / (2 * TRIG_MAX_RATIO),
                       mid.y + len * dy / (2 * TRIG_MAX_RATIO));
    // black underlay = outline, so the tick reads over the hands like the digits
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, major ? 6 : 4);
    graphics_draw_line(ctx, p1, p2);
    graphics_context_set_stroke_color(ctx, major ? GColorWhite : GColorLightGray);
    graphics_context_set_stroke_width(ctx, major ? 4 : 2);
    graphics_draw_line(ctx, p1, p2);
  }
}

// Minute hand stops just inside the icon ring (icons sit at ICON_EDGE_INSET 22
// + half the 42px icon ≈ 43 from the edge), so it passes through the numerals
// but short of the icons.
#define HOUR_HAND_FRAC  50   // hour hand: fixed circular length (% of min half)
#define MIN_EDGE_INSET  32   // minute hand: dynamic, reaching further toward the edge
static void draw_hands(GContext *ctx, GPoint center, int half, int hw, int hh) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int32_t min_angle = TRIG_MAX_ANGLE * t->tm_min / 60;
  int32_t hour_angle = TRIG_MAX_ANGLE * ((t->tm_hour % 12) * 60 + t->tm_min) / (12 * 60);

  // white when tides on + connected, original blue otherwise, red when disconnected
  GColor hand_col = !s_bt_connected ? GColorRed : (s_show_tide ? GColorWhite : GColorPictonBlue);

  GPoint hour_end = point_on_circle(center, half * HOUR_HAND_FRAC / 100, hour_angle);
  GPoint min_end  = point_on_rect(center, hw - MIN_EDGE_INSET, hh - MIN_EDGE_INSET, min_angle);

  // Draw each hand as outline-then-fill, minute LAST. Drawing the minute hand's
  // black outline on top of the hour hand cuts a clean dark edge between them,
  // so the two same-colored hands stay distinct where they overlap.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 12);
  graphics_draw_line(ctx, center, hour_end);
  graphics_context_set_stroke_color(ctx, hand_col);
  graphics_context_set_stroke_width(ctx, 8);
  graphics_draw_line(ctx, center, hour_end);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 12);
  graphics_draw_line(ctx, center, min_end);
  graphics_context_set_stroke_color(ctx, hand_col);
  graphics_context_set_stroke_width(ctx, 8);
  graphics_draw_line(ctx, center, min_end);
}

// Center: the current-weather icon (large) with the current temp outlined in
// its middle (18pt, matches the temp ring).
static void draw_center_weather(GContext *ctx, GPoint center) {
  draw_wx(ctx, center, s_cur_wx, true);
  if (s_cur_temp_valid) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", s_cur_temp);
    GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GSize sz = graphics_text_layout_get_content_size(
        buf, f, GRect(0, 0, 60, 40), GTextOverflowModeFill, GTextAlignmentCenter);
    draw_text_outlined(ctx, buf, f, GRect(center.x - 30, center.y - sz.h / 2 - 4, 60, sz.h), GColorWhite);
  }
}

// Battery/date sit on the 5 & 7 o'clock radials (the roomiest gap between the
// center icon and the inner ring), outlined so they read over the hands.
#define BADGE_R 40   // distance from center to the label
static void draw_disc_badge(GContext *ctx, GPoint center, int slot, const char *txt) {
  int32_t angle = TRIG_MAX_ANGLE * slot / 12;
  GPoint c = point_on_circle(center, BADGE_R, angle);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GSize sz = graphics_text_layout_get_content_size(
      txt, f, GRect(0, 0, 60, 30), GTextOverflowModeFill, GTextAlignmentCenter);
  draw_text_outlined(ctx, txt, f, GRect(c.x - 30, c.y - sz.h / 2 - 2, 60, sz.h + 4), GColorWhite);
}

static void draw_day_badge(GContext *ctx, GPoint center) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char buf[3];
  snprintf(buf, sizeof(buf), "%d", t->tm_mday);
  draw_disc_badge(ctx, center, 5, buf);   // 5 o'clock
}

static void draw_battery_badge(GContext *ctx, GPoint center) {
  BatteryChargeState bs = battery_state_service_peek();
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", bs.charge_percent);
  draw_disc_badge(ctx, center, 7, buf);   // 7 o'clock
}

// ---- tide tanks (very back layer) ----
// tide 0..100 -> fill % of the tank, in the agreed increments.
static int tide_fill_pct(int v) {
  if (v <= 15) return 15;
  if (v <= 35) return 35;
  if (v <= 65) return 65;
  if (v <= 85) return 85;
  return 100;
}

// Quarter-circle corner cutoffs for radius 4.
// Index [d-1] where d = distance from corner center (1..4).
// eff_bottom = bottom - CORNER_CUT[d-1].  Derived: bottom - 4 + floor(sqrt(16-d^2))
static const int8_t CORNER_CUT[4] = {1, 1, 2, 4};
#define CORNER_R 4

// Rocky harbor wall texture: mostly DarkGray with sparse LightGray highlights
// and Black crevices, using a position hash so it's stable and non-repeating.
static GColor rock_color(int x, int y) {
  int h = ((x * 7) ^ (y * 13)) & 0xF;   // 0..15
  if (h == 0)  return GColorLightGray;   //  6% highlight
  if (h >= 13) return GColorBlack;       // 19% crevice
  return GColorDarkGray;                 // 75% stone face
}

// A blue tank centered at c: rounded bottom corners, wavy top,
// rocky harbor border, sub-surface shadow wave.
static void draw_tide_cell(GContext *ctx, GPoint c, int S, int pct, int32_t phase) {
  int x0 = c.x - S / 2, x1 = c.x + S / 2;
  int top = c.y - S / 2, bottom = c.y + S / 2;
  int amp = S / 8; if (amp < 2) amp = 2;
  int mean = bottom - (pct * S) / 100;
  int width = x1 - x0; if (width < 1) width = 1;
  int sub = amp + 3;

  graphics_context_set_stroke_width(ctx, 1);

  // 1. Blue water fill with rounded bottom corners
  graphics_context_set_stroke_color(ctx, GColorBlueMoon);
  for (int x = x0; x <= x1; x++) {
    // Rounded corner: raise the effective bottom near left/right edges
    int eff_bottom = bottom;
    int dl = x - x0, dr = x1 - x;
    if (dl < CORNER_R) {
      int d = CORNER_R - dl;
      eff_bottom = bottom - CORNER_CUT[d - 1];
    } else if (dr < CORNER_R) {
      int d = CORNER_R - dr;
      eff_bottom = bottom - CORNER_CUT[d - 1];
    }
    int32_t ang = (int32_t)(x - x0) * TRIG_MAX_ANGLE / width + phase;
    int surf = mean - (amp * sin_lookup(ang)) / TRIG_MAX_RATIO;
    if (surf < top)        surf = top;
    if (surf < eff_bottom) graphics_draw_line(ctx, GPoint(x, surf), GPoint(x, eff_bottom));
  }

  // 2. Sub-surface shadow wave (black), same curve shifted down
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int x = x0; x <= x1; x++) {
    int32_t ang = (int32_t)(x - x0) * TRIG_MAX_ANGLE / width + phase;
    int surf = mean - (amp * sin_lookup(ang)) / TRIG_MAX_RATIO;
    int sy = surf + sub;
    if (sy > top && sy < bottom) graphics_draw_pixel(ctx, GPoint(x, sy));
  }

  // 3. Rocky border: 2px wide — outer pixel + inner pixel encroaching on water
  // Side walls stop CORNER_R px above bottom to expose the rounded corners
  for (int y = top; y <= bottom - CORNER_R; y++) {
    graphics_context_set_stroke_color(ctx, rock_color(x0 - 1, y));
    graphics_draw_pixel(ctx, GPoint(x0 - 1, y));
    graphics_context_set_stroke_color(ctx, rock_color(x0, y));
    graphics_draw_pixel(ctx, GPoint(x0, y));
    graphics_context_set_stroke_color(ctx, rock_color(x1 + 1, y));
    graphics_draw_pixel(ctx, GPoint(x1 + 1, y));
    graphics_context_set_stroke_color(ctx, rock_color(x1, y));
    graphics_draw_pixel(ctx, GPoint(x1, y));
  }
  // Bottom wall: outer row + inner row encroaching on water
  for (int x = x0 + CORNER_R; x <= x1 - CORNER_R; x++) {
    graphics_context_set_stroke_color(ctx, rock_color(x, bottom + 1));
    graphics_draw_pixel(ctx, GPoint(x, bottom + 1));
    graphics_context_set_stroke_color(ctx, rock_color(x, bottom));
    graphics_draw_pixel(ctx, GPoint(x, bottom));
  }
}

#define TIDE_RING_PCT 50   // inner radius for the tanks (tune on emulator)
#define TIDE_CELL     22   // tank size (px)
static void draw_tide(GContext *ctx, GPoint center, int hw, int hh) {
  if (!s_show_tide || !s_tide_valid) return;
  for (int k = 0; k < 12; k++) {
    if (!s_wx_valid[k]) continue;
    int32_t angle = k * TRIG_MAX_ANGLE / 12;
    GPoint edge = point_on_rect(center, hw, hh, angle);
    GPoint p = GPoint(center.x + (edge.x - center.x) * TIDE_RING_PCT / 100,
                      center.y + (edge.y - center.y) * TIDE_RING_PCT / 100);
    int32_t phase = (int32_t)k * TRIG_MAX_ANGLE * 5 / 12;  // unique phase per cell
    draw_tide_cell(ctx, p, TIDE_CELL, tide_fill_pct(s_tide[k]), phase);
  }
}

static void face_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_antialiased(ctx, true);  // smooth curves/lines (avoids the blocky look)
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  int hw = bounds.size.w / 2;
  int hh = bounds.size.h / 2;
  int half = (hw < hh ? hw : hh);

  draw_tide(ctx, center, hw, hh);          // very back: behind even the hands
  draw_hands(ctx, center, half, hw, hh);   // everything else draws on top
  draw_icon_ring(ctx, center, hw, hh);     // outer ring: weather icons (temp on top in modes 0/1)
  if (s_display_mode == MODE_NUMERALS)
    draw_hour_numerals(ctx, center, hw, hh); // inner ring: hour numerals
  else if (s_display_mode == MODE_HASH)
    draw_hash_ticks(ctx, center, hw, hh);    // inner ring: major/minor hash marks
  else if (s_display_mode == MODE_TEMPS)
    draw_temp_ring(ctx, center, hw, hh);     // inner ring: temperatures (12/3/6/9 bold)
  // MODE_NONE: nothing drawn on the inner ring
  draw_center_weather(ctx, center);
  if (s_show_date)    draw_day_badge(ctx, center);
  {
    int pct = battery_state_service_peek().charge_percent;
    if (s_show_battery || (s_battery_threshold > 0 && pct <= s_battery_threshold))
      draw_battery_badge(ctx, center);
  }
}

// Ask the phone companion for fresh weather. The companion JS is normally
// suspended in the background, but an inbound AppMessage wakes it.
static void request_weather(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK || !out) return;
  dict_write_uint8(out, MESSAGE_KEY_REQUEST_WEATHER, 1);
  app_message_outbox_send();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // refresh the rolling weather at :05 and :35 each hour
  if (tick_time->tm_min == 5 || tick_time->tm_min == 35) request_weather();
  if (s_face_layer) layer_mark_dirty(s_face_layer);
}

static void battery_handler(BatteryChargeState state) {
  if (s_face_layer) layer_mark_dirty(s_face_layer);
}

static void bt_handler(bool connected) {
  if (connected == s_bt_connected) return;     // ignore no-op events
  s_bt_connected = connected;
  if (!connected) vibes_double_pulse();        // buzz when the phone drops
  if (s_face_layer) layer_mark_dirty(s_face_layer);  // recolor the hands
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  s_face_layer = layer_create(bounds);
  layer_set_update_proc(s_face_layer, face_update_proc);
  layer_add_child(window_layer, s_face_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_face_layer);
  s_face_layer = NULL;
}

// ---- weather messages (from the companion JS) ----
// Icon int from JS: night*100 + state*10 + count (state 0..5, count 0..4).
static WxCell cell_from_code(int code) {
  WxCell w;
  w.night  = (code >= 100);
  code    %= 100;
  int st   = code / 10;
  w.count  = code % 10;
  w.cloud  = (st == 0) ? CLOUD_NONE : (st == 1) ? CLOUD_SMALL : CLOUD_LARGE;
  w.precip = (st == 3) ? PRECIP_RAIN : (st == 4) ? PRECIP_SNOW :
             (st == 5) ? PRECIP_STORM : PRECIP_NONE;
  if (w.precip == PRECIP_NONE) w.count = 0;
  return w;
}

static uint8_t cell_to_code(WxCell w) {
  return (w.night ? 100 : 0) + wx_state(w) * 10 + w.count;
}

// Persist the last weather so a relaunch shows it instantly (no placeholder flash).
#define PERSIST_WX  1
#define PERSIST_VER 4   // bumped: added per-slot tide data
typedef struct __attribute__((__packed__)) {
  uint8_t  ver;
  uint8_t  icon[12];
  int8_t   temp[12];
  uint16_t valid;
  uint8_t  cur_icon;
  int8_t   cur_temp;
  uint8_t  cur_valid;
  int8_t   start_k;
  uint8_t  moon_phase;
  int8_t   sunrise_k;
  int8_t   sunset_k;
  char     sunrise[8];
  char     sunset[8];
  uint8_t  tide[12];
  uint8_t  tide_valid;
} WxPersist;

static void save_weather(void) {
  WxPersist p;
  memset(&p, 0, sizeof(p));
  p.ver = PERSIST_VER;
  for (int k = 0; k < 12; k++) {
    p.icon[k] = cell_to_code(s_wx[k]);
    p.temp[k] = (int8_t)s_wx_temp[k];
    if (s_wx_valid[k]) p.valid |= (1 << k);
  }
  p.cur_icon = cell_to_code(s_cur_wx);
  p.cur_temp = (int8_t)s_cur_temp;
  p.cur_valid = s_cur_temp_valid ? 1 : 0;
  p.start_k = (int8_t)s_start_k;
  p.moon_phase = (uint8_t)s_moon_phase;
  p.sunrise_k = (int8_t)s_sunrise_k;
  p.sunset_k = (int8_t)s_sunset_k;
  strncpy(p.sunrise, s_sunrise_str, sizeof(p.sunrise) - 1);
  strncpy(p.sunset, s_sunset_str, sizeof(p.sunset) - 1);
  for (int k = 0; k < 12; k++) p.tide[k] = s_tide[k];
  p.tide_valid = s_tide_valid ? 1 : 0;
  persist_write_data(PERSIST_WX, &p, sizeof(p));
}

static void load_weather(void) {
  if (persist_exists(2)) {                          // setting: display mode
    s_display_mode = persist_read_int(2);
    if (s_display_mode < MODE_NUMERALS || s_display_mode > MODE_NONE) s_display_mode = MODE_NUMERALS;
  }
  if (persist_exists(3)) s_show_tide    = persist_read_bool(3);  // setting: show tide
  if (persist_exists(4)) s_show_date    = persist_read_bool(4);  // setting: show date
  if (persist_exists(5)) s_show_battery      = persist_read_bool(5);  // setting: show battery
  if (persist_exists(6)) s_battery_threshold = persist_read_int(6);   // setting: battery threshold
  if (!persist_exists(PERSIST_WX)) return;       // first launch: blank until JS sends
  WxPersist p;
  if (persist_read_data(PERSIST_WX, &p, sizeof(p)) < (int)sizeof(p)) return;
  if (p.ver != PERSIST_VER) return;
  for (int k = 0; k < 12; k++) {
    s_wx[k] = cell_from_code(p.icon[k]);
    s_wx_temp[k] = p.temp[k];
    s_wx_valid[k] = (p.valid >> k) & 1;
  }
  s_cur_wx = cell_from_code(p.cur_icon);
  s_cur_temp = p.cur_temp;
  s_cur_temp_valid = p.cur_valid;
  s_start_k = p.start_k;
  s_moon_phase = (p.moon_phase <= 9) ? p.moon_phase : 5;
  s_sunrise_k = p.sunrise_k;
  s_sunset_k = p.sunset_k;
  strncpy(s_sunrise_str, p.sunrise, sizeof(s_sunrise_str) - 1);
  strncpy(s_sunset_str, p.sunset, sizeof(s_sunset_str) - 1);
  for (int k = 0; k < 12; k++) s_tide[k] = p.tide[k];
  s_tide_valid = p.tide_valid ? 1 : 0;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  const uint32_t ICON_KEYS[12] = {
    MESSAGE_KEY_ICON_0, MESSAGE_KEY_ICON_1, MESSAGE_KEY_ICON_2,  MESSAGE_KEY_ICON_3,
    MESSAGE_KEY_ICON_4, MESSAGE_KEY_ICON_5, MESSAGE_KEY_ICON_6,  MESSAGE_KEY_ICON_7,
    MESSAGE_KEY_ICON_8, MESSAGE_KEY_ICON_9, MESSAGE_KEY_ICON_10, MESSAGE_KEY_ICON_11,
  };
  const uint32_t TEMP_KEYS[12] = {
    MESSAGE_KEY_TEMP_0, MESSAGE_KEY_TEMP_1, MESSAGE_KEY_TEMP_2,  MESSAGE_KEY_TEMP_3,
    MESSAGE_KEY_TEMP_4, MESSAGE_KEY_TEMP_5, MESSAGE_KEY_TEMP_6,  MESSAGE_KEY_TEMP_7,
    MESSAGE_KEY_TEMP_8, MESSAGE_KEY_TEMP_9, MESSAGE_KEY_TEMP_10, MESSAGE_KEY_TEMP_11,
  };
  for (int k = 0; k < 12; k++) {
    Tuple *ti = dict_find(iter, ICON_KEYS[k]);
    if (ti) { s_wx[k] = cell_from_code(ti->value->int32); s_wx_valid[k] = true; }
    Tuple *tt = dict_find(iter, TEMP_KEYS[k]);
    if (tt) { s_wx_temp[k] = tt->value->int32; }
  }
  Tuple *ci = dict_find(iter, MESSAGE_KEY_CUR_ICON);
  if (ci) s_cur_wx = cell_from_code(ci->value->int32);
  Tuple *ct = dict_find(iter, MESSAGE_KEY_CUR_TEMP);
  if (ct) { s_cur_temp = ct->value->int32; s_cur_temp_valid = true; }
  Tuple *sk = dict_find(iter, MESSAGE_KEY_START_K);
  if (sk) s_start_k = sk->value->int32;

  Tuple *mp = dict_find(iter, MESSAGE_KEY_MOON_PHASE);
  if (mp) { s_moon_phase = mp->value->int32;
            if (s_moon_phase < 0 || s_moon_phase > 9) s_moon_phase = 5;
            load_moon_phase(s_moon_phase); }

  // Only reset sunrise/sunset when a new weather payload arrives (identified by
  // START_K). Settings-only messages don't carry these keys and must not clobber them.
  if (dict_find(iter, MESSAGE_KEY_START_K)) {
    s_sunrise_k = -1; s_sunset_k = -1;
    s_sunrise_str[0] = '\0'; s_sunset_str[0] = '\0';
  }
  Tuple *srk = dict_find(iter, MESSAGE_KEY_SUNRISE_K);
  if (srk) s_sunrise_k = srk->value->int32;
  Tuple *ssk = dict_find(iter, MESSAGE_KEY_SUNSET_K);
  if (ssk) s_sunset_k = ssk->value->int32;
  Tuple *srs = dict_find(iter, MESSAGE_KEY_SUNRISE_STR);
  if (srs) { strncpy(s_sunrise_str, srs->value->cstring, sizeof(s_sunrise_str) - 1); }
  Tuple *sss = dict_find(iter, MESSAGE_KEY_SUNSET_STR);
  if (sss) { strncpy(s_sunset_str, sss->value->cstring, sizeof(s_sunset_str) - 1); }

  Tuple *dm = dict_find(iter, MESSAGE_KEY_DISPLAY_MODE);
  if (dm) {
    int want = dm->value->int32;
    if (want < MODE_NUMERALS || want > MODE_NONE) want = MODE_NUMERALS;
    if (want != s_display_mode) {        // icon/moon set depends on this -> reload
      s_display_mode = want;
      persist_write_int(2, s_display_mode);
      unload_weather_icons();
      s_loaded_phase = -1;               // force the moon to reload for the new mode
      load_weather_icons();
    }
  }

  Tuple *st = dict_find(iter, MESSAGE_KEY_SHOW_TIDE);
  if (st) {
    s_show_tide = st->value->uint32 ? true : false;
    persist_write_bool(3, s_show_tide);
  }
  Tuple *sd = dict_find(iter, MESSAGE_KEY_SHOW_DATE);
  if (sd) {
    s_show_date = sd->value->uint32 ? true : false;
    persist_write_bool(4, s_show_date);
  }
  Tuple *sb = dict_find(iter, MESSAGE_KEY_SHOW_BATTERY);
  if (sb) {
    s_show_battery = sb->value->uint32 ? true : false;
    persist_write_bool(5, s_show_battery);
  }
  Tuple *bt = dict_find(iter, MESSAGE_KEY_BATTERY_THRESHOLD);
  if (bt) {
    s_battery_threshold = bt->value->int32;
    persist_write_int(6, s_battery_threshold);
  }

  const uint32_t TIDE_KEYS[12] = {
    MESSAGE_KEY_TIDE_0, MESSAGE_KEY_TIDE_1, MESSAGE_KEY_TIDE_2,  MESSAGE_KEY_TIDE_3,
    MESSAGE_KEY_TIDE_4, MESSAGE_KEY_TIDE_5, MESSAGE_KEY_TIDE_6,  MESSAGE_KEY_TIDE_7,
    MESSAGE_KEY_TIDE_8, MESSAGE_KEY_TIDE_9, MESSAGE_KEY_TIDE_10, MESSAGE_KEY_TIDE_11,
  };
  for (int k = 0; k < 12; k++) {
    Tuple *tt = dict_find(iter, TIDE_KEYS[k]);
    if (tt) s_tide[k] = tt->value->uint32;
  }
  Tuple *tv = dict_find(iter, MESSAGE_KEY_TIDE_VALID);
  if (tv) s_tide_valid = tv->value->uint32 ? true : false;

  save_weather();   // remember for next launch
  if (s_face_layer) layer_mark_dirty(s_face_layer);
}

static void prv_init(void) {
  load_weather();          // restore last-known weather (sets phase) before loading icons
  load_weather_icons();
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  // Raw Bluetooth link: fires immediately on connect/disconnect and matches the
  // connected status the watch itself shows (the pebble_app connection lagged).
  s_bt_connected = bluetooth_connection_service_peek();
  bluetooth_connection_service_subscribe(bt_handler);

  app_message_register_inbox_received(inbox_received);
  app_message_open(512, 64);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  unload_weather_icons();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
