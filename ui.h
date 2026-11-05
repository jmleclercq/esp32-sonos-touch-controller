#pragma once
// UI for the Sonos touch controller (480x320 landscape, ST7796 + GT911).
// Partial redraw: each call to ui_draw() paints ONE full-width zone (with its own
// background), so the mipi_spi driver only transmits that band of the screen.
#include <string>
#include <cstdio>
#include "esphome/core/color.h"
#include "esphome/core/hal.h"
#include "esphome/components/display/display.h"
#include "esphome/components/image/image.h"

using esphome::Color;
using esphome::display::BaseFont;
using esphome::display::Display;
using esphome::display::TextAlign;

enum : uint32_t {
  Z_ALL = 1, Z_HEAD = 2, Z_TITLE = 4, Z_ARTIST = 8, Z_ALBUM = 16, Z_PROG = 32, Z_CTRL = 64, Z_VOL = 128, Z_ART = 256
};

struct Room {
  std::string entity, name, state, title, artist, album, pic;
  float vol = 0, pos = 0, dur = 0;
  bool muted = false;
  uint32_t pos_ts = 0;
};

static Room rooms[8];
static int n_rooms = 0;
static int cur_room = 0;
static int screen_mode = 0;  // 0 = player, 1 = room picker
static uint32_t dirty = Z_ALL;
static uint32_t draw_zone = Z_ALL;
static uint32_t title_t0 = 0;
static bool scroll_title = false, scroll_artist = false, scroll_album = false;
static bool vol_drag = false;
static float vol_local = 0;
static uint32_t vol_busy = 0;
static uint32_t last_touch = 0;   // millis() of last user touch (screen sleep)
static int light_stage = 0;       // 0 awake, 1 dimmed, 2 backlight off
static float awake_b = 1.0f;      // backlight brightness to restore on wake
static bool woke_touch = false;   // this touch only woke the screen: do not act on it
static bool art_ok = false;      // a downloaded cover is available for the current room
static std::string art_loaded_url, art_pending_url;
static uint32_t mute_busy = 0;   // ignore HA mute echo right after a local toggle
static bool art_reload = true;   // request: (re)download the cover of the current room
static int pressed = 0;     // 1 prev, 2 play/pause, 3 next
static int ui_action = 0;   // set by ui_touch(): 1 prev, 2 play/pause, 3 next, 4 mute toggle
static std::string clock_str;
static int ui_page = 0;  // index of the display band currently being rendered (reset before each update)
static const int PAGE_H = 80;  // mipi_spi renders 320 lines in 4 bands (buffer_size 33%)

static const Color C_BG(14, 17, 23), C_SURF(27, 33, 46), C_SURF2(38, 46, 64), C_TEXT(242, 244, 248),
    C_SUB(139, 147, 167), C_ACC(61, 220, 151), C_ACC_D(36, 140, 96), C_TRACK(42, 49, 68), C_RED(224, 80, 80);

static const int W = 480;
// zone rows
static const int HEAD_Y = 0, HEAD_H = 46;
static const int TITLE_Y = 48, TITLE_H = 44;
static const int ARTIST_Y = 92, ARTIST_H = 30;
static const int ALBUM_Y = 122, ALBUM_H = 26;
static const int PROG_Y = 156, PROG_H = 48;
static const int CTRL_Y = 206, CTRL_H = 70;
static const int VOL_Y = 280, VOL_H = 40;
static const int TX = 136, TW = 324;               // text column (right of the cover)
static const int ART_X = 20, ART_Y = 52, ART_S = 96;  // cover
static const int VX = 64, VW = 352, VY = 300;

static void room_changed(int idx, uint32_t bits) {
  if (screen_mode == 1) dirty |= Z_ALL;
  else if (idx == cur_room) dirty |= bits;
}

static bool room_online(const Room &r) { return !r.state.empty() && r.state != "unavailable" && r.state != "unknown"; }
static bool room_playing(const Room &r) { return r.state == "playing"; }

static float room_pos(const Room &r) {
  float p = r.pos;
  if (room_playing(r)) p += (esphome::millis() - r.pos_ts) / 1000.0f;
  if (r.dur > 0 && p > r.dur) p = r.dur;
  return p < 0 ? 0 : p;
}

static float room_vol(const Room &r) {
  float v = (vol_drag || esphome::millis() - vol_busy < 1500) ? vol_local : r.vol;
  return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static void map_touch(int rx, int ry, int &x, int &y) {
  // GT911 axes are swapped vs. the display (calibrated on 4 corners)
  x = (int) (30 + (ry - 20) * 1.5f);
  y = (int) (30 + (421 - rx) * 0.655f);
  if (x < 0) x = 0;
  if (x > 479) x = 479;
  if (y < 0) y = 0;
  if (y > 319) y = 319;
}

static void rrect(Display &it, int x, int y, int w, int h, int r, Color c) {
  it.filled_rectangle(x + r, y, w - 2 * r, h, c);
  it.filled_rectangle(x, y + r, r, h - 2 * r, c);
  it.filled_rectangle(x + w - r, y + r, r, h - 2 * r, c);
  it.filled_circle(x + r, y + r, r, c);
  it.filled_circle(x + w - r - 1, y + r, r, c);
  it.filled_circle(x + r, y + h - r - 1, r, c);
  it.filled_circle(x + w - r - 1, y + h - r - 1, r, c);
}

static void marquee(Display &it, int x, int y, int w, int h, BaseFont *f, Color col, const std::string &s,
                    bool &scroll) {
  int x1, y1, tw, th;
  it.get_text_bounds(x, y, s.c_str(), f, TextAlign::TOP_LEFT, &x1, &y1, &tw, &th);
  it.start_clipping(x, y - 2, x + w, y + h);
  if (tw <= w) {
    scroll = false;
    it.printf(x, y, f, col, TextAlign::TOP_LEFT, "%s", s.c_str());
  } else {
    scroll = true;
    const int gap = 70;
    int t = (int) (esphome::millis() - title_t0);
    int off = t > 2000 ? ((t - 2000) / 22) % (tw + gap) : 0;
    it.printf(x - off, y, f, col, TextAlign::TOP_LEFT, "%s", s.c_str());
    it.printf(x - off + tw + gap, y, f, col, TextAlign::TOP_LEFT, "%s", s.c_str());
  }
  it.end_clipping();
}

// ---------------------------------------------------------------- zones
static void z_head(Display &it, BaseFont *fs) {
  const Room &r = rooms[cur_room];
  it.filled_rectangle(0, HEAD_Y, W, HEAD_H, C_BG);
  rrect(it, 14, 7, 200, 32, 16, C_SURF);
  Color dot = !room_online(r) ? C_RED : (room_playing(r) ? C_ACC : C_SUB);
  it.filled_circle(31, 23, 5, dot);
  std::string nm = r.name.empty() ? std::string("Sonos") : r.name;
  it.start_clipping(44, 7, 190, 39);
  it.printf(44, 13, fs, C_TEXT, TextAlign::TOP_LEFT, "%s", nm.c_str());
  it.end_clipping();
  if (n_rooms > 1) it.filled_triangle(192, 20, 202, 20, 197, 27, C_SUB);
  if (!clock_str.empty()) it.printf(W - 16, 13, fs, C_SUB, TextAlign::TOP_RIGHT, "%s", clock_str.c_str());
}

static void z_title(Display &it, BaseFont *ft) {
  const Room &r = rooms[cur_room];
  it.filled_rectangle(TX - 6, TITLE_Y, W - TX + 6, TITLE_H, C_BG);
  std::string t = r.title;
  if (!room_online(r)) t = "Hors ligne";
  else if (t.empty() || t == "unknown") t = "Rien en lecture";
  marquee(it, TX, TITLE_Y + 4, TW, 38, ft, C_TEXT, t, scroll_title);
}

static void z_artist(Display &it, BaseFont *fa) {
  const Room &r = rooms[cur_room];
  it.filled_rectangle(TX - 6, ARTIST_Y, W - TX + 6, ARTIST_H, C_BG);
  if (r.artist.empty() || r.artist == "unknown") { scroll_artist = false; return; }
  marquee(it, TX, ARTIST_Y + 2, TW, 28, fa, C_ACC, r.artist, scroll_artist);
}

static void z_album(Display &it, BaseFont *fsm) {
  const Room &r = rooms[cur_room];
  it.filled_rectangle(TX - 6, ALBUM_Y, W - TX + 6, ALBUM_H, C_BG);
  if (r.album.empty() || r.album == "unknown") { scroll_album = false; return; }
  marquee(it, TX, ALBUM_Y + 2, TW, 24, fsm, C_SUB, r.album, scroll_album);
}

static void z_art(Display &it, esphome::image::Image *art) {
  const Room &r = rooms[cur_room];
  it.filled_rectangle(ART_X - 2, ART_Y - 2, ART_S + 4, ART_S + 4, C_BG);
  if (art_ok && art != nullptr) {
    it.image(ART_X, ART_Y, art);
    return;
  }
  // placeholder: rounded tile with a music note
  rrect(it, ART_X, ART_Y, ART_S, ART_S, 14, C_SURF);
  Color c = room_playing(r) ? C_ACC : C_SUB;
  int cx = ART_X + ART_S / 2, cy = ART_Y + ART_S / 2;
  it.filled_circle(cx - 8, cy + 14, 9, c);
  it.filled_rectangle(cx - 1, cy - 22, 4, 38, c);
  it.filled_triangle(cx + 3, cy - 22, cx + 20, cy - 12, cx + 3, cy - 8, c);
}

static void z_prog(Display &it, BaseFont *ftiny) {
  const Room &r = rooms[cur_room];
  it.filled_rectangle(0, PROG_Y, W, PROG_H, C_BG);
  float pos = room_pos(r), dur = r.dur;
  float frac = dur > 0 ? pos / dur : 0;
  const int bx = 20, bw = 440, by = PROG_Y + 12;
  rrect(it, bx, by, bw, 6, 3, C_TRACK);
  int fw = (int) (bw * frac);
  if (fw > 6) rrect(it, bx, by, fw, 6, 3, C_ACC);
  if (dur > 0) {
    it.filled_circle(bx + fw, by + 3, 8, C_BG);
    it.filled_circle(bx + fw, by + 3, 6, C_TEXT);
  }
  int ps = (int) pos, ds = (int) dur;
  it.printf(bx, PROG_Y + 28, ftiny, C_SUB, TextAlign::TOP_LEFT, "%d:%02d", ps / 60, ps % 60);
  if (dur > 0) it.printf(bx + bw, PROG_Y + 28, ftiny, C_SUB, TextAlign::TOP_RIGHT, "%d:%02d", ds / 60, ds % 60);
}

static void z_ctrl(Display &it) {
  const Room &r = rooms[cur_room];
  it.filled_rectangle(0, CTRL_Y, W, CTRL_H, C_BG);
  const int cy = CTRL_Y + 35;
  bool playing = room_playing(r);
  // previous
  it.filled_circle(150, cy, 27, pressed == 1 ? C_TRACK : C_SURF2);
  it.filled_rectangle(139, cy - 9, 3, 18, C_TEXT);
  it.filled_triangle(143, cy, 158, cy - 10, 158, cy + 10, C_TEXT);
  // next
  it.filled_circle(330, cy, 27, pressed == 3 ? C_TRACK : C_SURF2);
  it.filled_triangle(337, cy, 322, cy - 10, 322, cy + 10, C_TEXT);
  it.filled_rectangle(338, cy - 9, 3, 18, C_TEXT);
  // play / pause
  it.filled_circle(240, cy, 34, pressed == 2 ? C_ACC_D : C_ACC);
  if (playing) {
    rrect(it, 229, cy - 13, 7, 26, 2, C_BG);
    rrect(it, 244, cy - 13, 7, 26, 2, C_BG);
  } else {
    it.filled_triangle(232, cy - 15, 232, cy + 15, 256, cy, C_BG);
  }
}

static void z_vol(Display &it, BaseFont *ftiny) {
  const Room &r = rooms[cur_room];
  it.filled_rectangle(0, VOL_Y, W, VOL_H, C_BG);
  float v = room_vol(r);
  rrect(it, VX, VY - 3, VW, 6, 3, C_TRACK);
  int fw = (int) (VW * v);
  if (fw > 6) rrect(it, VX, VY - 3, fw, 6, 3, C_ACC);
  it.filled_circle(VX + fw, VY, 11, C_BG);
  it.filled_circle(VX + fw, VY, 9, C_TEXT);
  // speaker icon (tap = mute); crossed out when muted
  Color ic = r.muted ? C_RED : C_SUB;
  it.filled_rectangle(22, VY - 4, 5, 8, ic);
  it.filled_triangle(27, VY - 4, 27, VY + 4, 35, VY + 10, ic);
  it.filled_triangle(27, VY - 4, 35, VY - 10, 35, VY + 10, ic);
  if (r.muted) {
    it.line(20, VY - 11, 42, VY + 11, C_RED);
    it.line(21, VY - 11, 43, VY + 11, C_RED);
  }
  it.printf(W - 16, VY - 8, ftiny, C_SUB, TextAlign::TOP_RIGHT, "%d", (int) (v * 100 + 0.5f));
}

static void z_rooms(Display &it, BaseFont *ft, BaseFont *fa, BaseFont *ftiny) {
  it.fill(C_BG);
  it.printf(20, 10, ft, C_TEXT, TextAlign::TOP_LEFT, "Pièces");
  // close (X)
  it.line(448, 16, 468, 36, C_SUB);
  it.line(449, 16, 469, 36, C_SUB);
  it.line(468, 16, 448, 36, C_SUB);
  it.line(469, 16, 449, 36, C_SUB);
  for (int i = 0; i < n_rooms && i < 5; i++) {
    int y = 56 + 50 * i;
    const Room &r = rooms[i];
    rrect(it, 14, y, 452, 44, 12, i == cur_room ? C_SURF2 : C_SURF);
    if (i == cur_room) rrect(it, 14, y + 8, 4, 28, 2, C_ACC);
    Color dot = !room_online(r) ? C_RED : (room_playing(r) ? C_ACC : C_SUB);
    it.filled_circle(38, y + 22, 6, dot);
    it.start_clipping(54, y, 460, y + 44);
    std::string nm = r.name.empty() ? r.entity : r.name;
    it.printf(54, y + 4, fa, C_TEXT, TextAlign::TOP_LEFT, "%s", nm.c_str());
    std::string sub = room_online(r) ? (room_playing(r) && !r.title.empty() ? r.title : std::string("En pause")) : "Hors ligne";
    it.printf(54, y + 27, ftiny, C_SUB, TextAlign::TOP_LEFT, "%s", sub.c_str());
    it.end_clipping();
  }
}

// ---------------------------------------------------------------- entry point
static void zone_rows(uint32_t z, int &y0, int &y1) {
  y0 = 1000; y1 = -1;
  auto add = [&](int a, int h) { if (a < y0) y0 = a; if (a + h > y1) y1 = a + h; };
  if (z & Z_HEAD) add(HEAD_Y, HEAD_H);
  if (z & Z_ART) add(ART_Y - 2, ART_S + 4);
  if (z & Z_TITLE) add(TITLE_Y, TITLE_H);
  if (z & Z_ARTIST) add(ARTIST_Y, ARTIST_H);
  if (z & Z_ALBUM) add(ALBUM_Y, ALBUM_H);
  if (z & Z_PROG) add(PROG_Y, PROG_H);
  if (z & Z_CTRL) add(CTRL_Y, CTRL_H);
  if (z & Z_VOL) add(VOL_Y, VOL_H);
}

static void ui_draw(Display &it, BaseFont *ft, BaseFont *fa, BaseFont *fs, BaseFont *ftiny,
                    esphome::image::Image *art) {
  uint32_t z = draw_zone;
  int page = ui_page++;
  if (screen_mode == 0 && z != Z_ALL) {
    // mipi_spi aborts the whole update at the first band where nothing was drawn, so touch a (background
    // coloured) pixel in every band the zone does not cover; it keeps the bounding box to 1 pixel.
    int y0, y1;
    zone_rows(z, y0, y1);
    int p0 = page * PAGE_H, p1 = p0 + PAGE_H;
    if (y1 <= p0 || y0 >= p1) {
      it.draw_pixel_at(0, p0, C_BG);
      return;
    }
  }
  if (screen_mode == 1) {
    if (z == Z_ALL) z_rooms(it, ft, fa, ftiny);
    return;
  }
  if (z == Z_ALL) {
    it.fill(C_BG);
    z = Z_HEAD | Z_TITLE | Z_ARTIST | Z_ALBUM | Z_PROG | Z_CTRL | Z_VOL | Z_ART;
  }
  if (z & Z_HEAD) z_head(it, fs);
  if (z & Z_ART) z_art(it, art);
  if (z & Z_TITLE) z_title(it, ft);
  if (z & Z_ARTIST) z_artist(it, fa);
  if (z & Z_ALBUM) z_album(it, fs);
  if (z & Z_PROG) z_prog(it, ftiny);
  if (z & Z_CTRL) z_ctrl(it);
  if (z & Z_VOL) z_vol(it, ftiny);
}

// ---------------------------------------------------------------- touch
static void vol_from_x(int x) {
  float v = (x - VX) / (float) VW;
  vol_local = v < 0 ? 0 : (v > 1 ? 1 : v);
  vol_busy = esphome::millis();
  dirty |= Z_VOL;
}

static void ui_touch(int x, int y) {
  ui_action = 0;
  if (woke_touch) return;
  if (screen_mode == 1) {
    if (y < 52 && x > 400) {
      screen_mode = 0; dirty = Z_ALL;
    } else if (y >= 56) {
      int i = (y - 56) / 50;
      if (i < n_rooms && (y - 56) % 50 < 44) {
        cur_room = i; screen_mode = 0; dirty = Z_ALL; title_t0 = esphome::millis(); art_ok = false; art_reload = true;
      }
    }
    return;
  }
  if (y < HEAD_H && x < 220 && n_rooms > 1) {
    screen_mode = 1; dirty = Z_ALL;
    return;
  }
  if (y >= CTRL_Y && y < CTRL_Y + CTRL_H) {
    if (x >= 110 && x < 200) pressed = 1;
    else if (x >= 200 && x <= 280) pressed = 2;
    else if (x > 280 && x <= 370) pressed = 3;
    if (pressed) { ui_action = pressed; dirty |= Z_CTRL; }
  } else if (y >= VOL_Y - 4 && x < 52) {
    ui_action = 4;
  } else if (y >= VOL_Y - 4) {
    vol_drag = true;
    vol_from_x(x);
  }
}

static void ui_release() {
  if (pressed) { pressed = 0; dirty |= Z_CTRL; }
}
