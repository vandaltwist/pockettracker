#include "ui/modules/moonset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/helpers.h"
#include "ui/modules/granular_ui.h"

namespace pt::ui {

namespace {

int row_count(int col) {
    switch (col) {
        case 0: return (int)(sizeof(GRAIN)  / sizeof(GRAIN[0]));
        case 1: return (int)(sizeof(ROAM)   / sizeof(ROAM[0]));
        case 2: return (int)(sizeof(MOTION) / sizeof(MOTION[0]));
        default: return 0;
    }
}

bool is_state(int line) { return line == 0; }

std::string pct(int v) { return std::to_string(v) + "%"; }
std::string ms(int v) { return std::to_string(v) + "ms"; }
std::string hz(int v) { return std::to_string(v) + "Hz"; }
std::string semis(int v) { return (v >= 0 ? "+" : "") + std::to_string(v) + "st"; }

}  // namespace

int moonset_row_count(int col) { return row_count(col); }

std::string moonset_value(const songcore::Instrument::GranularParams& g, int col, int line, int sliceMethod, int sliceIndex) {
    if (line < 0 || line >= row_count(col)) return "";
    switch (col) {
        case 0: {  // GRAIN
            const auto& gr = g.grain;
            switch (line) {
                case 0: return gr.state ? "ON" : "OFF";
                case 1:
                    if (sliceMethod != 0) return "SL" + std::to_string(sliceIndex + 1);
                    return pct(gr.position);
                case 2: return ms(gr.size);
                case 3: // DENSITY
                    if (gr.time == 1) return DIVS[std::clamp(gr.density_div, 0, (int)(sizeof(DIVS)/sizeof(DIVS[0]))-1)].name;
                    return hz(gr.density);
                case 4: return pct(gr.spray);
                case 5: return semis(gr.pitch);
                case 6: return gr.time ? "BPM" : "HZ";
                case 7: return std::to_string(gr.window);
                case 8: return pct(gr.spread);
                case 9: return pct(gr.attack);
                case 10: return pct(gr.decay);
                case 11: return std::to_string(gr.pan);
                case 12: return std::to_string(gr.jitter) + "ct";
                default: return "";
            }
        }
        case 1: {  // ROAM
            const auto& r = g.roam;
            switch (line) {
                case 0: return r.state ? "ON" : "OFF";
                case 1: return std::to_string(r.mode);
                case 2: return pct(r.range);
                case 3: return pct(r.wander);
                case 4: // SPEED
                    if (g.grain.time == 1) return DIVS[std::clamp(r.speed_div, 0, (int)(sizeof(DIVS)/sizeof(DIVS[0]))-1)].name;
                    return std::to_string(r.speed) + "x";
                case 5: return pct(r.smooth);
                case 6: return std::to_string(r.bpm);
                case 7: return r.freeze ? "ON" : "OFF";
                case 8: return std::to_string(r.dcurve);
                case 9: return std::to_string(r.step_rate) + "x";
                case 10: return pct(r.inertia);
                case 11: return pct(r.tlock);
                default: return "";
            }
        }
        case 2: {  // MOTION
            const auto& m = g.motion;
            switch (line) {
                case 0: return m.state ? "ON" : "OFF";
                case 1: return pct(m.jump);
                case 2: return pct(m.hold);
                case 3: return pct(m.rev);
                case 4: return pct(m.elastic);
                case 5: return pct(m.chaos);
                case 6: return std::to_string(m.stutter) + "x";
                case 7: return pct(m.tapestop);
                case 8: return pct(m.tunnel);
                case 9: return pct(m.probability);
                case 10: return pct(m.lfo_depth);
                case 11: return pct(m.step_glide);
                case 12: return std::to_string(m.trigger) + "%";
                case 13: return std::to_string(m.spin);
                default: return "";
            }
        }
    }
    return "";
}

CursorContext moonset_cell_context(const songcore::Instrument::GranularParams& g, int col, int line, int sliceMethod) {
    if (line < 0 || line >= row_count(col)) return cc::none();
    if (is_state(line)) {
        const bool st = (col == 0) ? g.grain.state : (col == 1) ? g.roam.state : g.motion.state;
        return cc::toggle_binary(st);
    }
    switch (col) {
        case 0: {  // GRAIN
            switch (line) {
                case 1:
                    if (sliceMethod != 0) return cc::read_only();
                    return cc::hex_byte(g.grain.position, 0, 100);
                case 2: return cc::hex_byte(g.grain.size / 10, 0, 400);  // stored ms, displayed /10? keep simple
                case 3: // DENSITY
                    if (g.grain.time == 1) return cc::hex_byte(g.grain.density_div, 0, (int)(sizeof(DIVS)/sizeof(DIVS[0]))-1);
                    return cc::hex_byte(g.grain.density, 0, 1000);
                case 4: return cc::hex_byte(g.grain.spray, 0, 100);
                case 5: return cc::hex_byte(g.grain.pitch + 48, 0, 96);
                case 6: return cc::toggle_binary(g.grain.time); // TIME enum 0/1
                case 7: return cc::hex_byte(g.grain.window, 0, 4);
                case 8: return cc::hex_byte(g.grain.spread, 0, 100);
                case 9: return cc::hex_byte(g.grain.attack, 0, 100);
                case 10: return cc::hex_byte(g.grain.decay, 0, 100);
                case 11: return cc::hex_byte(g.grain.pan + 100, 0, 200);
                case 12: return cc::hex_byte(g.grain.jitter, 0, 1200);
                default: return cc::none();
            }
        }
        case 1: {  // ROAM
            switch (line) {
                case 1: return cc::hex_byte(g.roam.mode, 0, 3);
                case 2: return cc::hex_byte(g.roam.range, 0, 100);
                case 3: return cc::hex_byte(g.roam.wander, 0, 100);
                case 4: // SPEED
                    if (g.grain.time == 1) return cc::hex_byte(g.roam.speed_div, 0, (int)(sizeof(DIVS)/sizeof(DIVS[0]))-1);
                    return cc::hex_byte(g.roam.speed, 0, 400);
                case 5: return cc::hex_byte(g.roam.smooth, 0, 100);
                case 6: return cc::hex_byte(g.roam.bpm, 20, 300);
                case 7: return cc::toggle_binary(g.roam.freeze);
                case 8: return cc::hex_byte(g.roam.dcurve, 0, 3);
                case 9: return cc::hex_byte(g.roam.step_rate, 1, 16);
                case 10: return cc::hex_byte(g.roam.inertia, 0, 100);
                case 11: return cc::hex_byte(g.roam.tlock, 0, 100);
                default: return cc::none();
            }
        }
        case 2: {  // MOTION
            switch (line) {
                case 1: return cc::hex_byte(g.motion.jump, 0, 100);
                case 2: return cc::hex_byte(g.motion.hold, 0, 100);
                case 3: return cc::hex_byte(g.motion.rev, 0, 100);
                case 4: return cc::hex_byte(g.motion.elastic, 0, 100);
                case 5: return cc::hex_byte(g.motion.chaos, 0, 100);
                case 6: return cc::hex_byte(g.motion.stutter, 1, 16);
                case 7: return cc::hex_byte(g.motion.tapestop, 0, 100);
                case 8: return cc::hex_byte(g.motion.tunnel, 0, 100);
                case 9: return cc::hex_byte(g.motion.probability, 0, 100);
                case 10: return cc::hex_byte(g.motion.lfo_depth, 0, 100);
                case 11: return cc::hex_byte(g.motion.step_glide, 0, 100);
                case 12: return cc::hex_byte(g.motion.trigger, 0, 100);
                case 13: return cc::hex_byte(g.motion.spin, 0, 100);
                default: return cc::none();
            }
        }
    }
    return cc::none();
}

bool moonset_apply(songcore::Instrument::GranularParams& g, int col, int line, int v, int sliceMethod) {
    if (line < 0 || line >= row_count(col)) return false;
    if (is_state(line)) {
        bool& st = (col == 0) ? g.grain.state : (col == 1) ? g.roam.state : g.motion.state;
        st = (v != 0);
        return true;
    }
    switch (col) {
        case 0:
            switch (line) {
                case 1:
                    if (sliceMethod != 0) return false;
                    g.grain.position = v; break;
                case 2: g.grain.size = v * 10; break;
                case 3:
                    if (g.grain.time == 1) g.grain.density_div = v; else g.grain.density = v; break;
                case 4: g.grain.spray = v; break;
                case 5: g.grain.pitch = v - 48; break;
                case 6: g.grain.time = v; break;
                case 7: g.grain.window = v; break;
                case 8: g.grain.spread = v; break;
                case 9: g.grain.attack = v; break;
                case 10: g.grain.decay = v; break;
                case 11: g.grain.pan = v - 100; break;
                case 12: g.grain.jitter = v; break;
                default: return false;
            }
            return true;
        case 1:
            switch (line) {
                case 1: g.roam.mode = v; break;
                case 2: g.roam.range = v; break;
                case 3: g.roam.wander = v; break;
                case 4:
                    if (g.grain.time == 1) g.roam.speed_div = v; else g.roam.speed = v; break;
                case 5: g.roam.smooth = v; break;
                case 6: g.roam.bpm = v; break;
                case 7: g.roam.freeze = v; break;
                case 8: g.roam.dcurve = v; break;
                case 9: g.roam.step_rate = v; break;
                case 10: g.roam.inertia = v; break;
                case 11: g.roam.tlock = v; break;
                default: return false;
            }
            return true;
        case 2:
            switch (line) {
                case 1: g.motion.jump = v; break;
                case 2: g.motion.hold = v; break;
                case 3: g.motion.rev = v; break;
                case 4: g.motion.elastic = v; break;
                case 5: g.motion.chaos = v; break;
                case 6: g.motion.stutter = v; break;
                case 7: g.motion.tapestop = v; break;
                case 8: g.motion.tunnel = v; break;
                case 9: g.motion.probability = v; break;
                case 10: g.motion.lfo_depth = v; break;
                case 11: g.motion.step_glide = v; break;
                case 12: g.motion.trigger = v; break;
                case 13: g.motion.spin = v; break;
                default: return false;
            }
            return true;
    }
    return false;
}

void draw_moonset_granular(Canvas& c, int x, int gy,
                           const songcore::Instrument::GranularParams& g,
                           int cursorLine, int cursorCol, const Theme& t,
                           int sliceMethod, int sliceIndex) {
    constexpr int FS = 2, CS = 1, RH = 18, VX = 109;
    static const char* titles[3] = {"GRAIN", "ROAM", "MOTION"};
    constexpr int COLX[3] = {7, 221, 435};
    for (int col = 0; col < 3; ++col) {
        const int cx = x + COLX[col];
        c.draw_text(titles[col], cx + 8, gy + 0 + TEXT_PADDING, t.textTitle, CHAR_SPACING, FS);
        const int n = row_count(col);
        for (int line = 0; line < n; ++line) {
            const int yy = gy + 22 + line * RH;
            const bool cur = (col == cursorCol && line == cursorLine);
            const char* label = (col == 0) ? GRAIN[line].label
                             : (col == 1) ? ROAM[line].label
                                          : MOTION[line].label;
            c.draw_text(label, cx + 8, yy + TEXT_PADDING, cur ? cursor_mark_ink(t) : t.textParam,
                        CHAR_SPACING, FS);
            const std::string val = moonset_value(g, col, line, sliceMethod, sliceIndex);
            if (cur) {
                const int vw = Canvas::text_width(val, CS, FS);
                c.fill_rect(cx + VX - CHAR_SPACING, yy, vw + 2 * CHAR_SPACING, RH, t.rowCursor);
            }
            c.draw_text(val, cx + VX, yy + TEXT_PADDING, cur ? cursor_cell_ink(t) : t.textValue,
                        CS, FS);
        }
    }
}

}  // namespace pt::ui
