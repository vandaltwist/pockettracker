#include "ui/modules/moonset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/helpers.h"
#include "ui/modules/confirm_dialog.h"
#include "ui/modules/granular_ui.h"
#include "ui/modules/rec_viz.h"

namespace pt::ui {

namespace {

// A bounds-safe `list[i]`: a slip shows a wrong label rather than reading past the end.
const std::string& at(const std::vector<std::string>& v, int i) {
    static const std::string kEmpty;
    if (v.empty()) return kEmpty;
    return v[static_cast<size_t>(std::clamp(i, 0, static_cast<int>(v.size()) - 1))];
}

/** A vertical 1px rule from (x, y0), `h` tall. */
void v_line(Canvas& c, int x, int y0, int h, Argb color, int thickness = 1) {
    if (h <= 0) return;   // a zero-length butt-capped line draws nothing; neither does this
    c.fill_rect(x, y0, thickness, h, color);
}

}  // namespace

// ─── The vocabularies ────────────────────────────────────────────────────────────────────────────

const std::vector<std::string>& MoonsetModule::source_values() {
    static const std::vector<std::string> v{"LEFT", "RIGHT", "STEREO", "MONO"};
    return v;
}
const std::vector<std::string>& MoonsetModule::rate_values() {
    static const std::vector<std::string> v{"HIGH", "NORM", "LOFI"};
    return v;
}
std::vector<std::string> MoonsetModule::bit_depth_choices(int source_bits) {
    std::vector<std::string> v;
    for (const int bits : {32, 24, 16, 8})
        if (bits <= source_bits) v.push_back(std::to_string(bits));
    if (v.empty()) v.push_back("8");
    return v;
}
const std::vector<std::string>& MoonsetModule::duration_values() {
    static const std::vector<std::string> v{"4 BAR", "2 BAR", "1 BAR", "1/2",
                                            "1/4",   "1/8",   "1/16",  "1/32"};
    return v;
}
double MoonsetModule::duration_beats(int duration_index) {
    const int i = std::clamp(duration_index, 0, static_cast<int>(duration_values().size()) - 1);
    return 16.0 / static_cast<double>(1 << i);   // 4 BAR = 16 beats, halving to 1/32 = 0.125
}
const std::vector<std::string>& MoonsetModule::fx_types() {
    static const std::vector<std::string> v{"OTT", "DUST", "DRIVE", "EQ", "SYNC"};
    return v;
}
const std::vector<std::string>& MoonsetModule::sync_types() {
    static const std::vector<std::string> v{"RPITCH", "TSTRETCH"};
    return v;
}
const std::vector<std::string>& MoonsetModule::slice_methods() {
    static const std::vector<std::string> v{"TRANSIENT", "DIVIDE", "OFF", "MANUAL"};
    return v;
}
const std::vector<std::string>& MoonsetModule::ops_row1() {
    static const std::vector<std::string> v{"CROP", "COPY", "CUT", "DUPL", "PASTE", "DEL"};
    return v;
}
const std::vector<std::string>& MoonsetModule::ops_row2() {
    static const std::vector<std::string> v{"NORM", "FADE+", "FADE-", "SLNC", "REV", "UNDO"};
    return v;
}

// ⚠️ MOONSET packs the rows tighter than the sample editor: the granular block starts at y=428, so
// the sample rows must end above it. Row 11 always keeps its slot (slicing OFF leaves a gap rather
// than moving every row below it).
int MoonsetModule::content_y(int row) {
    switch (row) {
        case 8:  return 243;
        case 10: return 268;
        case 11: return 289;
        case 13: return 310;
        case 14: return 331;
        case 16: return 357;
        case 18: return 378;
        case 19: return 399;
        default: return (row - 8) * 20 + 240;
    }
}

// ─── The sparse row map ──────────────────────────────────────────────────────────────────────────

int MoonsetModule::row_above(int row, int slice_method) {
    switch (row) {
        case 0:  return 0;
        case 1:  return 19;   // wrap to the last row
        case 5:  return 2;    // RECORD row: above is PITCH/DURATION/BIT
        case 8:  return 5;    // from SELECTION, above is RECORD
        case 10: return 8;
        case 13: return (slice_method == SLICE_OFF) ? 10 : 11;
        case 16: return 14;
        case 18: return 16;
        default: return row - 1;
    }
}

int MoonsetModule::row_below(int row, int slice_method) {
    switch (row) {
        case 2:  return 5;    // from PITCH/DURATION/BIT, below is RECORD
        case 5:  return 8;    // RECORD row: below is SELECTION
        case 8:  return 10;
        case 10: return (slice_method == SLICE_OFF) ? 13 : 11;
        case 11: return 13;
        case 14: return 16;
        case 16: return 18;
        case 19: return 1;    // wrap to the first row
        default: return row + 1;
    }
}

int MoonsetModule::max_col_for_row(int row, int slice_method) {
    if (row == 1 || row == 2) return 2;
    if (row == 5) return 2;          // RECORD: REC / INPUT / SET (LOOP moved to Y)
    if (row >= 3 && row <= 7) return 1;          // the selection's two edges (START / END)
    if (row == 8) return 2;                      // START / END / LOOP checkbox
    if (row == 10) return slice_has_parameter(slice_method) ? 1 : 0;
    if (row == 11) return 1;
    if (row == 13 || row == 14) return 5;        // six ops per row
    if (row == 16) return 2;                     // TYPE / VALUE / APPLY
    if (row == 18) return 0;                     // NAME — the whole row is one cell
    if (row == 19) return (slice_method == SLICE_OFF) ? 2 : 3;   // CHOP only exists with slices
    return 0;
}

// ─── MoonsetState: the derived values ───────────────────────────────────────────────────────

int MoonsetState::manual_key_param() const {
    if (sliceMethod == MoonsetModule::SLICE_TRANSIENT) return sliceSensitivity;
    if (sliceMethod == MoonsetModule::SLICE_DIVIDE)    return sliceDivisions;
    return 0;   // MANUAL and OFF have no parameter for the markers to be keyed to
}

bool MoonsetState::manual_markers_live() const {
    // ⚠️ The stamp alone: an empty live list (MANUAL, every boundary deleted) must still override
    // the file's cue points.
    return manualKeyMethod == sliceMethod && manualKeyParam == manual_key_param();
}

const std::vector<int>& MoonsetState::method_markers() const {
    // Methods that compute rather than store read this empty list.
    static const std::vector<int> computed;

    switch (sliceMethod) {
        case MoonsetModule::SLICE_TRANSIENT: return transientMarkers;
        // OFF and MANUAL both answer with the file's cue points, so MANUAL edits what it carries.
        case MoonsetModule::SLICE_OFF:
        case MoonsetModule::SLICE_MANUAL:    return fileMarkers;
        // DIVIDE derives its boundaries from `sliceDivisions` on every read, so there is no list.
        default:                                  return computed;
    }
}

int MoonsetState::marker_count() const {
    return static_cast<int>(manual_markers_live() ? manualMarkers.size() : method_markers().size());
}

int64_t MoonsetState::marker_position(int k) const {
    const int n = marker_count();
    if (k < 0 || n == 0) return 0;
    const int i = std::min(k, n - 1);   // past the end: the boundary an unplaced one is born on
    return manual_markers_live() ? static_cast<int64_t>(manualMarkers[static_cast<size_t>(i)].frame)
                                 : static_cast<int64_t>(method_markers()[static_cast<size_t>(i)]);
}

int MoonsetState::slice_marker_index() const {
    if (sliceMethod == MoonsetModule::SLICE_OFF) return -1;
    return sliceIndex - 1;   // −1 at index 0: slice 00's left edge is the sample's own start
}

int MoonsetState::slice_index_ceiling() const {
    const int markers = marker_count();
    switch (sliceMethod) {
        // N markers make N + 1 slices …
        case MoonsetModule::SLICE_TRANSIENT: return markers;
        // … and MANUAL reaches one further, a slice born when dragged off its left neighbour.
        case MoonsetModule::SLICE_MANUAL:    return markers + 1;
        case MoonsetModule::SLICE_DIVIDE:
            return markers > 0 ? markers : std::max(sliceDivisions - 1, 0);
        default:                                  return 0;
    }
}

void MoonsetState::slice_bounds(int idx, int64_t& start, int64_t& end) const {
    const int64_t total = static_cast<int64_t>(totalFrames);
    if (sliceMethod == MoonsetModule::SLICE_OFF) {
        start = 0;
        end   = total;
        return;
    }

    // Marker `idx − 1` is slice `idx`'s left edge, marker `idx` its right.
    const int n = marker_count();
    if (n > 0) {
        // `marker_position` clamps past the end (MANUAL's free slot); the right edge does not —
        // past the last boundary the slice runs to the end.
        start = (idx == 0) ? 0 : marker_position(idx - 1);
        end   = (idx < n) ? marker_position(idx) : total;
        return;
    }

    if (sliceMethod == MoonsetModule::SLICE_DIVIDE) {
        const int64_t div = std::max(sliceDivisions, 1);
        start = (static_cast<int64_t>(idx) * total) / div;
        end   = std::min((static_cast<int64_t>(idx + 1) * total) / div, total);
        return;
    }

    start = 0;   // TRANSIENT before the detector has run, and MANUAL on a sample with no boundaries
    end   = total;
}

int64_t MoonsetState::effective_slice_position() const {
    int64_t start = 0, end = 0;
    slice_bounds(sliceIndex, start, end);
    return start;
}

int64_t MoonsetState::view_start() const {
    if (zoomLevel == 0 || totalFrames <= 0) return 0;

    const int64_t total   = static_cast<int64_t>(totalFrames);
    const int64_t visible = std::max<int64_t>(total >> zoomLevel, 1);

    const auto window_for = [&](int64_t center) {
        return std::clamp<int64_t>(center - visible / 2, 0, std::max<int64_t>(total - visible, 0));
    };

    // With nothing sounding, centre on the marker the cursor points at.
    int64_t center;
    if (cursorRow == 8 && cursorCol == 1) {
        center = selectionEnd;
    } else if (cursorRow == 11 && sliceMethod != MoonsetModule::SLICE_OFF) {
        center = effective_slice_position();
    } else {
        center = selectionStart;   // row 8 col 0, and every other row
    }
    const int64_t anchored = window_for(center);

    // A playhead pulls the window only when the selection does not fit; otherwise the view would
    // shake with nothing to reveal.
    if (playbackPosition >= 0.0f &&
        !(selectionStart >= anchored && selectionEnd <= anchored + visible)) {
        return window_for(static_cast<int64_t>(playbackPosition * static_cast<float>(total)));
    }
    return anchored;
}

int64_t MoonsetState::view_end() const {
    const int64_t total = static_cast<int64_t>(totalFrames);
    if (zoomLevel == 0 || totalFrames <= 0) return total;
    const int64_t visible = std::max<int64_t>(total >> zoomLevel, 1);
    return std::min<int64_t>(view_start() + visible, total);
}

std::string MoonsetState::duration_display() const {
    if (sampleRate <= 0 || totalFrames <= 0) return "--:--.--";

    // The pending pitch is counted in: DURATION says what SAVE will bake.
    const double  ratio      = std::pow(2.0, pitchSemitones / 12.0);
    const int64_t effFrames  = std::max<int64_t>(static_cast<int64_t>(totalFrames / ratio), 1);
    const int64_t ms         = (effFrames * 1000) / sampleRate;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%02d", static_cast<int>(ms / 60000),
                  static_cast<int>((ms % 60000) / 1000), static_cast<int>((ms % 1000) / 10));
    return std::string(buf);
}

int MoonsetState::source_bpm() const {
    if (sampleRate <= 0 || totalFrames <= 0) return 0;

    const double ratio   = std::pow(2.0, pitchSemitones / 12.0);
    const double seconds = static_cast<double>(totalFrames) / (ratio * sampleRate);
    if (seconds <= 0.0) return 0;

    const long bpm = std::lround(MoonsetModule::duration_beats(durationIndex) * 60.0 / seconds);
    return (bpm < 1 || bpm > 999) ? 0 : static_cast<int>(bpm);
}

std::string MoonsetState::bpm_display() const {
    const int bpm = source_bpm();
    return bpm > 0 ? std::to_string(bpm) + "BPM" : "---BPM";
}

void MoonsetState::reset_stretch() {
    const MoonsetState fresh;
    durationIndex  = fresh.durationIndex;
    stretchBpm     = fresh.stretchBpm;
    pitchSemitones = fresh.pitchSemitones;
    stretchGrainMs = fresh.stretchGrainMs;
}

bool MoonsetState::fx_armed() const {
    if (fxType == MoonsetModule::FX_EQ) return fxEqSlot >= 0;
    return fxType <= MoonsetModule::FX_DRIVE && fxValue > 0;
}

int MoonsetState::fx_param() const {
    return (fxType == MoonsetModule::FX_EQ) ? fxEqSlot : fxValue;
}

// ─── The draw ────────────────────────────────────────────────────────────────────────────────────

namespace {

// ⚠️ No row background on this screen: the cursor is the cell, and the label beside it takes
// `cursor_mark_ink`.

// Three label/value pairs across one row (rows 1 and 2).
void draw_label_3val(Canvas& c, int x, int ty, bool is_cur_row, int cur_col, const Theme& t,
                     const std::string& l1, const std::string& v1, int c1,
                     const std::string& l2, const std::string& v2, int c2,
                     const std::string& l3, const std::string& v3, int c3) {
    auto on          = [&](int col) { return is_cur_row && cur_col == col; };
    auto label_color = [&](int col) { return on(col) ? cursor_mark_ink(t) : t.textParam; };

    c.draw_text(l1, x + 10,  ty, label_color(c1), CHAR_SPACING, FONT_SCALE);
    draw_cursor_cell(c, v1, x + 110, ty, on(c1), t.textValue, t);
    c.draw_text(l2, x + 180, ty, label_color(c2), CHAR_SPACING, FONT_SCALE);
    draw_cursor_cell(c, v2, x + 335, ty, on(c2), t.textValue, t);
    c.draw_text(l3, x + 445, ty, label_color(c3), CHAR_SPACING, FONT_SCALE);
    draw_cursor_cell(c, v3, x + 535, ty, on(c3), t.textValue, t);
}

// One op row: six buttons, the one under the cursor lit.
void draw_ops_row(Canvas& c, int x, int y, const std::vector<std::string>& ops, bool is_cur,
                  int cur_col, const Theme& t) {
    const int ty   = y + TEXT_PADDING;
    const int colW = MoonsetModule::WIDTH / static_cast<int>(ops.size());   // ~106px
    for (size_t i = 0; i < ops.size(); ++i) {
        const int lx = x + static_cast<int>(i) * colW + 15;
        draw_cursor_cell(c, ops[i], lx, ty, is_cur && cur_col == static_cast<int>(i), t.textValue, t);
    }
}

}  // namespace

void MoonsetModule::draw(Canvas& c, int x, int y, const MoonsetState& s,
                         const songcore::Instrument::GranularParams& g, const Theme& t) const {
    c.fill_rect(x, y, WIDTH, HEIGHT, t.background);
    const int oy = y - s.pageScrollPx;

    // ── Header, then rows 1 and 2, then the waveform ─────────────────────────────────────────────
    {
        const int ty = oy + 5 + TEXT_PADDING;
        c.draw_text("MOONSET", x + 10, ty, t.textTitle, CHAR_SPACING, FONT_SCALE);
        // BPM sits left of the rate and length it is derived from; six columns cap it at 999.
        c.draw_text(s.bpm_display(), x + 245, ty, t.textParam, CHAR_SPACING, FONT_SCALE);
        c.draw_text(std::to_string(s.sampleRate) + "Hz", x + 360, ty, t.textParam, CHAR_SPACING, FONT_SCALE);
        c.draw_text(s.duration_display(), x + 492, ty, t.textParam, CHAR_SPACING, FONT_SCALE);
    }

    {   // Row 1 — ZOOM / SOURCE / RATE
        const int  ry  = oy + 20;
        const bool cur = (s.cursorRow == 1);
        // A mono sample reads MONO whatever the mode (the cell is read-only there).
        const std::string source = s.hasStereoData ? at(source_values(), s.sourceMode) : "MONO";
        draw_label_3val(c, x, ry + TEXT_PADDING, cur, s.cursorCol, t,
                        "ZOOM",   std::to_string(1 << s.zoomLevel) + "X",  0,
                        "SOURCE", source,                                  1,
                        "RATE",   at(rate_values(), s.rateMode),           2);
    }

    {   // Row 2 — PITCH / DURATION / BIT
        const int  ry  = oy + 40;
        const bool cur = (s.cursorRow == 2);
        const std::string pitch = (s.pitchSemitones >= 0 ? "+" : "") + std::to_string(s.pitchSemitones);
        draw_label_3val(c, x, ry + TEXT_PADDING, cur, s.cursorCol, t,
                        "PITCH",    pitch,                                  0,
                        "DURATION", at(duration_values(), s.durationIndex), 1,
                        "BIT",      std::to_string(s.bitDepth),             2);
    }

    {   // Row 5 — RECORD: REC toggle + INPUT device selector
        const int  ry  = oy + 60;
        const bool cur = (s.cursorRow == 5);
        const int ty = ry + TEXT_PADDING;
        const bool     onRecCell = (cur && s.cursorCol == 0);
        const uint32_t recInk = s.recordActive
            ? (s.recordLevel > 0.01f ? 0xFFFF0000u : 0xFFFFAA00u)   // red if sound, amber if silent
            : t.textValue;
        c.draw_text("RECORD", x + 10, ty, onRecCell ? cursor_mark_ink(t) : t.textParam,
                    CHAR_SPACING, FONT_SCALE);
        if (s.recordActive && !s.recordFailed && s.recordFrames >= s.recordSkipFrames) {
            // « REC » puis le temps COLLÉ et plus petit : la cellule reste dans sa place devant INPUT.
            char tb[24];
            std::snprintf(tb, sizeof(tb), "%.1fs", (s.recordFrames - s.recordSkipFrames) / 48000.0f);
            const std::string timeStr = tb;
            const int recX = x + 175, fs2 = FONT_SCALE - 1, cs2 = 1, gap = 3;
            const int wRec = Canvas::text_width("REC", CHAR_SPACING, FONT_SCALE);
            const int wAll = wRec + gap + Canvas::text_width(timeStr, cs2, fs2);
            if (onRecCell)
                c.fill_rect(recX - CHAR_SPACING, ty - TEXT_PADDING, wAll + 2 * CHAR_SPACING, ROW_HEIGHT,
                            t.rowCursor);
            const uint32_t ink = onRecCell ? cursor_cell_ink(t) : recInk;
            c.draw_text("REC", recX, ty, ink, CHAR_SPACING, FONT_SCALE);
            c.draw_text(timeStr, recX + wRec + gap, ty + (FONT_SCALE - fs2) * 5 / 2, ink, cs2, fs2);
        } else {
            const std::string recText = s.recordFailed ? std::string("FAILED")
                                      : s.recordActive ? std::string("COUNT-IN")
                                                       : std::string("REC");
            draw_cursor_cell(c, recText, x + 175, ty, onRecCell, recInk, t);
        }
        std::string inputName = s.recordInputDevice.empty() ? "DEF" : s.recordInputDevice.substr(0, 3);
        while (!inputName.empty() && inputName.back() == ' ') inputName.pop_back();   // "M1 MASTER" -> "M1"
        c.draw_text("INPUT", x + 335, ty, cur && s.cursorCol == 1 ? cursor_mark_ink(t) : t.textParam,
                    CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, inputName, x + 410, ty, cur && s.cursorCol == 1, t.textValue, t);
        c.draw_text("SET", x + 535, ty, cur && s.cursorCol == 2 ? cursor_mark_ink(t) : t.textParam,
                    CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, rec_viz().menuOpen() ? "ON" : "OFF", x + 590, ty,
                         cur && s.cursorCol == 2, t.textValue, t);
    }

    draw_waveform(c, x, oy + WAVEFORM_Y, s, t, &g);

    {   // Row 8 — the selection, in frames
        const int  ry  = oy + content_y(8);
        const bool cur = (s.cursorRow == 8);
        const int ty = ry + TEXT_PADDING;
        c.draw_text("SELECTION", x + 10, ty, cur ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING,
                    FONT_SCALE);
        draw_cursor_cell(c, hex_padded(s.selectionStart, 8), x + 180, ty, cur && s.cursorCol == 0,
                         t.textValue, t);
        draw_cursor_cell(c, hex_padded(s.selectionEnd, 8), x + 335, ty, cur && s.cursorCol == 1,
                         t.textValue, t);
        c.draw_text("LOOP", x + 490, ty, cur && s.cursorCol == 2 ? cursor_mark_ink(t) : t.textParam,
                    CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, s.loopPreview ? "ON" : "OFF", x + 545, ty, cur && s.cursorCol == 2,
                          t.textValue, t);
    }

    {   // Row 10 — the slice METHOD, and its one parameter (SENS or BY, depending)
        const int  ry  = oy + content_y(10);
        const bool cur = (s.cursorRow == 10);
        const int ty = ry + TEXT_PADDING;
        c.draw_text("SLICE", x + 10, ty, cur ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, at(slice_methods(), s.sliceMethod), x + 175, ty,
                         cur && s.cursorCol == 0, t.textValue, t);
        if (slice_has_parameter(s.sliceMethod)) {
            const bool        onVal = (cur && s.cursorCol == 1);
            const std::string lbl   = (s.sliceMethod == SLICE_TRANSIENT) ? "SENS" : "BY";
            const std::string val   = hex2(s.sliceMethod == SLICE_TRANSIENT ? s.sliceSensitivity
                                                                            : s.sliceDivisions);
            c.draw_text(lbl, x + 335, ty, onVal ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING, FONT_SCALE);
            draw_cursor_cell(c, val, x + 410, ty, onVal, t.textValue, t);
        }
    }

    if (s.sliceMethod != SLICE_OFF) {   // Row 11 — which slice, and where it starts
        const int  ry  = oy + content_y(11);
        const bool cur = (s.cursorRow == 11);
        const int ty = ry + TEXT_PADDING;

        // ⚠️ The row has no label, so the cursor's fill is the only sign the cursor is here.
        draw_cursor_cell(c, hex2(s.sliceIndex), x + 175, ty, cur && s.cursorCol == 0, t.textValue, t);
        if (s.sliceMethod == SLICE_TRANSIENT || s.sliceMethod == SLICE_MANUAL) {
            // How many the index can reach. DIVIDE draws none: its BY cell above says it.
            const int total = s.slice_index_ceiling() + 1;
            c.draw_text("/" + hex2(total), x + 175 + 2 * CHAR_W, ty, t.textParam, CHAR_SPACING, FONT_SCALE);
        }
        draw_cursor_cell(c, hex_padded(s.effective_slice_position(), 8), x + 335, ty, cur && s.cursorCol == 1,
                         t.textValue, t);
    }

    draw_ops_row(c, x, oy + content_y(13), ops_row1(), s.cursorRow == 13, s.cursorCol, t);
    draw_ops_row(c, x, oy + content_y(14), ops_row2(), s.cursorRow == 14, s.cursorCol, t);

    {   // Row 16 — the FX row. Its VALUE cell is the one cell in the app with three vocabularies.
        const int  ry  = oy + content_y(16);
        const bool cur = (s.cursorRow == 16);
        const int ty = ry + TEXT_PADDING;
        c.draw_text("EFFECT", x + 10, ty, cur ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, at(fx_types(), s.fxType), x + 180, ty, cur && s.cursorCol == 0,
                         t.textValue, t);

        const bool onVal = (cur && s.cursorCol == 1);
        if (s.fxType == FX_EQ) {
            // An EQ slot, drawn as every other EQ cell is: the number plus the ">" for its editor.
            draw_eq_cell(c, x + 290, ty, s.fxEqSlot, onVal, t);
        } else {
            const std::string val = (s.fxType == FX_SYNC) ? at(sync_types(), s.syncType)
                                                          : hex2(s.fxValue);
            draw_cursor_cell(c, val, x + 290, ty, onVal, t.textValue, t);
        }
        draw_cursor_cell(c, "APPLY", x + 440, ty, cur && s.cursorCol == 2, t.textValue, t);
    }

    {   // Row 18 — NAME. One cell, and no column: the row and the cell are the same thing.
        const int  ry  = oy + content_y(18);
        const bool cur = (s.cursorRow == 18);
        const int ty = ry + TEXT_PADDING;
        c.draw_text("NAME", x + 10, ty, cur ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, s.sampleName, x + 120, ty, cur, t.textValue, t);
    }

    {   // Row 19 — LOAD / SAVE / OVERWRITE, and CHOP when there are slices to chop
        const int  ry  = oy + content_y(19);
        const bool cur = (s.cursorRow == 19);
        const int ty = ry + TEXT_PADDING;
        auto btn = [&](const char* text, int bx, int i) {
            draw_cursor_cell(c, text, bx, ty, cur && s.cursorCol == i, t.textValue, t);
        };
        btn("LOAD",      x + 120, 0);
        btn("SAVE",      x + 230, 1);
        btn("OVERWRITE", x + 335, 2);
        if (s.sliceMethod != SLICE_OFF) btn("CHOP", x + 510, 3);
    }

    // The granular block, scrolled with the page (pageScrollPx): two rows peek under the sample
    // rows, seven fill the screen once the block takes the cursor.
    draw_moonset_granular(c, x, oy + GRANULAR_Y, g, s.granularRow,
                          s.granularColumn, s.granularTop, t);

    if (s.showConfirmClose) draw_confirm_dialog(c, x, y, t);
}

void MoonsetModule::draw_waveform(Canvas& c, int x, int y, const MoonsetState& s,
                                       const Theme& t,
                                       const songcore::Instrument::GranularParams* g) const {
    const int wfLeft  = x + 10;
    const int wfRight = wfLeft + WAVEFORM_W;
    const int wfLast  = wfRight - 1;
    const int midY    = y + WAVEFORM_H / 2;

    c.fill_rect(wfLeft, y, WAVEFORM_W, WAVEFORM_H, t.vizBackground);
    c.fill_rect(wfLeft, midY, WAVEFORM_W, 1, t.vizCenterLine);

    const int64_t viewStart = s.view_start();
    const int64_t viewLen   = std::max<int64_t>(s.view_end() - viewStart, 1);
    const float   viewLenF  = static_cast<float>(viewLen);

    auto frame_x = [&](float frame) {
        return static_cast<float>(wfLeft) +
               ((frame - static_cast<float>(viewStart)) / viewLenF) * static_cast<float>(WAVEFORM_W);
    };

    if (s.recordActive || rec_viz().menuOpen()) {
        draw_rec_viz(c, wfLeft, y, s, t);

        // Tête de lecture : un simple filet sur le bord droit (= présent), qui ne masque plus les courbes.
        if (s.recordActive)
            c.fill_rect(wfRight - 2, y, 2, WAVEFORM_H, t.textPlayhead);

        return;   // viz (seul ou avec son menu de réglages en overlay) occupe tout le panneau
    } else if (s.waveformData.size() >= 2) {
        const int   bins  = static_cast<int>(s.waveformData.size()) / 2;
        const float halfH = WAVEFORM_H / 2.0f;
        for (int i = 0; i < bins; ++i) {
            const float minVal = s.waveformData[static_cast<size_t>(i) * 2];
            const float maxVal = s.waveformData[static_cast<size_t>(i) * 2 + 1];

            const int cx  = wfLeft + static_cast<int>(static_cast<float>(i) *
                                                      static_cast<float>(WAVEFORM_W) /
                                                      static_cast<float>(bins));
            const int top = midY - static_cast<int>(maxVal * halfH);
            const int bot = midY - static_cast<int>(minVal * halfH);

            // Bins inside the selection are lit: that is the selection; S/E only mark its edges.
            const int64_t binFrame = viewStart + (static_cast<int64_t>(i) * viewLen) / bins;
            const bool    inSel    = (binFrame >= s.selectionStart && binFrame < s.selectionEnd);
            v_line(c, cx, top, bot - top, inSel ? t.textValue : t.textEmpty);
        }
    } else {
        c.draw_text("WAVEFORM", x + 265, midY - 6, t.textEmpty, CHAR_SPACING, FONT_SCALE);
    }

    if (s.totalFrames <= 0) return;

    // ── The selection's two edges ────────────────────────────────────────────────────────────────
    {
        const float sXf = frame_x(static_cast<float>(s.selectionStart));
        const float eXf = frame_x(static_cast<float>(s.selectionEnd));
        // Test the unclamped position, draw the clamped one: an edge scrolled out must vanish.
        if (sXf >= static_cast<float>(wfLeft) && sXf <= static_cast<float>(wfRight)) {
            const int sX = std::clamp(static_cast<int>(sXf), wfLeft, wfLast);
            v_line(c, sX, y, WAVEFORM_H, t.textTitle);
            c.draw_text("S", sX + 2, y + 3, t.textTitle, CHAR_SPACING, FONT_SCALE);
        }
        if (eXf >= static_cast<float>(wfLeft) && eXf <= static_cast<float>(wfRight)) {
            const int eX = std::clamp(static_cast<int>(eXf), wfLeft, wfLast);
            v_line(c, eX, y, WAVEFORM_H, t.textTitle);
            c.draw_text("E", eX - 17, y + 3, t.textTitle, CHAR_SPACING, FONT_SCALE);
        }
    }

    const bool onSliceRow = (s.cursorRow == 11);

    // The band behind the slice under the cursor, faint so the wave shows through.
    auto highlight_slice = [&](int64_t start, int64_t end) {
        const int sX = std::clamp(static_cast<int>(frame_x(static_cast<float>(start))), wfLeft, wfRight);
        const int eX = std::clamp(static_cast<int>(frame_x(static_cast<float>(end))),   wfLeft, wfRight);
        if (eX > sX) c.fill_rect(sX, y, eX - sX, WAVEFORM_H, with_alpha(t.rowCursor, 0.1f));
    };

    // A slice boundary. The two bounding the current slice draw dimmer — the established look.
    auto boundary = [&](int64_t frame, bool active) {
        const float mXf = frame_x(static_cast<float>(frame));
        if (mXf < static_cast<float>(wfLeft) || mXf > static_cast<float>(wfRight)) return;
        v_line(c, std::min(static_cast<int>(mXf), wfLast), y, WAVEFORM_H,
               active ? t.textEmpty : t.textParam);
    };

    // ── The slice boundaries: the detector's (TRANSIENT) or the file's (OFF) ─────────────────────
    const int markerCount = s.marker_count();
    if (markerCount > 0) {
        if (onSliceRow) {
            int64_t start = 0, end = 0;
            s.slice_bounds(s.sliceIndex, start, end);
            highlight_slice(start, end);
        }
        for (int idx = 0; idx < markerCount; ++idx) {
            // Marker `idx` is the right edge of slice `idx` and the left of `idx + 1`.
            const bool active = onSliceRow && (idx == s.sliceIndex - 1 || idx == s.sliceIndex);
            boundary(s.marker_position(idx), active);
        }
    }

    // ⚠️ Only while DIVIDE is arithmetic: once a marker is nudged the list above is the boundaries.
    if (s.sliceMethod == SLICE_DIVIDE && s.sliceDivisions > 0 && markerCount == 0) {
        const int64_t div   = s.sliceDivisions;
        const int64_t total = static_cast<int64_t>(s.totalFrames);
        if (onSliceRow) {
            highlight_slice((static_cast<int64_t>(s.sliceIndex) * total) / div,
                            std::min((static_cast<int64_t>(s.sliceIndex + 1) * total) / div, total));
        }
        for (int64_t i = 1; i < div; ++i) {
            const bool active = onSliceRow && (i == s.sliceIndex || i == s.sliceIndex + 1);
            boundary((i * total) / div, active);
        }
    }

    // ── The playhead ────────────────────────────────────────────────────────────────────────────
    if (s.playbackPosition >= 0.0f) {
        const float playFrame = s.playbackPosition * static_cast<float>(s.totalFrames);
        const float mXf       = frame_x(playFrame);
        if (mXf >= static_cast<float>(wfLeft) && mXf <= static_cast<float>(wfRight))
            v_line(c, std::min(static_cast<int>(mXf), wfLast - 1), y, WAVEFORM_H, t.vizWave,
                   /*thickness=*/2);
    }

    // ── Granular preview markers on the waveform (red, like the MOONSET page) ───────────────────
    // Shows how GRAIN / ROAM / MOTION will read the current slice. The grain head is anchored at the
    // start of the selected slice; `grain.position` then offsets the read head inside it:
    // 0 = slice start, 128 = slice middle, 255 = slice end.
    if (g && s.totalFrames > 0) {
        const int   totalF = static_cast<int>(s.totalFrames);
        auto        grain_x = [&](int pos01) {
            return frame_x(static_cast<float>(pos01) / 255.0f * static_cast<float>(totalF));
        };

        // Slice bounds in 0..255 space (absolute sample position when no slice is active).
        int64_t sliceStartF = 0, sliceEndF = totalF;
        bool    hasSlice    = false;
        if (s.sliceMethod != SLICE_OFF) {
            if (s.sliceMethod == SLICE_DIVIDE && s.sliceDivisions > 0) {
                sliceStartF = (static_cast<int64_t>(s.sliceIndex) * totalF) / s.sliceDivisions;
                sliceEndF   = std::min((static_cast<int64_t>(s.sliceIndex + 1) * totalF) /
                                       s.sliceDivisions, static_cast<int64_t>(totalF));
            } else {
                s.slice_bounds(s.sliceIndex, sliceStartF, sliceEndF);
            }
            hasSlice = true;
        }
        const int sliceStart01 = static_cast<int>(
            static_cast<float>(sliceStartF) / totalF * 255.0f);
        const int sliceEnd01   = static_cast<int>(
            static_cast<float>(sliceEndF)   / totalF * 255.0f);
        const int sliceSize01  = std::max(0, sliceEnd01 - sliceStart01);

        // Effective grain position: anchored to slice start, moved by `grain.position`.
        int effectiveGrain01 = g->grain.position;          // absolute when no slice
        if (hasSlice && sliceSize01 > 0) {
            effectiveGrain01 = sliceStart01 +
                (static_cast<int>(static_cast<float>(g->grain.position) / 255.0f *
                                   static_cast<float>(sliceSize01)));
        }

        // Slice bounds drawn in red (start + end lines + fill between).
        if (hasSlice) {
            const float sx = grain_x(sliceStart01);
            const float ex = grain_x(sliceEnd01);
            if (sx >= static_cast<float>(wfLeft) && sx <= static_cast<float>(wfRight))
                v_line(c, static_cast<int>(sx), y, WAVEFORM_H, t.textGranular);
            if (ex >= static_cast<float>(wfLeft) && ex <= static_cast<float>(wfRight))
                v_line(c, static_cast<int>(ex), y, WAVEFORM_H, t.textGranular);
            c.fill_rect(static_cast<int>(sx), y + WAVEFORM_H - 4,
                        std::max(1, static_cast<int>(ex - sx)), 4, t.textGranular);
        }

        // GRAIN head: solid red vertical line at the effective position.
        if (g->grain.state) {
            const float gx = grain_x(effectiveGrain01);
            if (gx >= static_cast<float>(wfLeft) && gx <= static_cast<float>(wfRight))
                v_line(c, static_cast<int>(gx), y, WAVEFORM_H, t.textGranular);
        }

        // ROAM range: red horizontal bar at the bottom, from head toward slice end.
        if (g->roam.state) {
            const int roamEnd01 = std::min(effectiveGrain01 + g->roam.range,
                                           hasSlice ? sliceEnd01 : 255);
            const float lx      = grain_x(effectiveGrain01);
            const float rx      = grain_x(roamEnd01);
            const int   left    = static_cast<int>(lx);
            const int   right   = static_cast<int>(rx);
            if (right > left && right >= wfLeft && left <= wfRight) {
                const int cl = std::max(left, wfLeft);
                const int cr = std::min(right, wfLast);
                c.fill_rect(cl, y + WAVEFORM_H - 3, cr - cl, 3, t.textGranular);
            }
        }

        // GRAIN spray: red vertical lines left/right of the effective head.
        if (g->grain.spray > 0) {
            const int sprayL = std::max(effectiveGrain01 - g->grain.spray, hasSlice ? sliceStart01 : 0);
            const int sprayR = std::min(effectiveGrain01 + g->grain.spray, hasSlice ? sliceEnd01 : 255);
            const float lx   = grain_x(sprayL);
            const float rx   = grain_x(sprayR);
            if (lx >= static_cast<float>(wfLeft) && lx <= static_cast<float>(wfRight))
                v_line(c, static_cast<int>(lx), y, WAVEFORM_H, t.textGranular);
            if (rx >= static_cast<float>(wfLeft) && rx <= static_cast<float>(wfRight))
                v_line(c, static_cast<int>(rx), y, WAVEFORM_H, t.textGranular);
        }

        // MOTION active: red block at the waveform's top-right corner.
        if (g->motion.state) {
            c.fill_rect(wfRight - 4, y, 4, 8, t.textGranular);
        }
    }
}

// ─── Record visualizer (courbes de niveau L/R) ─────────────────────────────────────────────────
// Tout le calcul (FFT, contours, dessin) vit dans rec_viz.h. L'audio l'alimente avec
// `pt::ui::rec_viz().push(left, right, frames)` ; ici on ne fait que le dessiner.

void MoonsetModule::draw_rec_viz(Canvas& c, int x, int y, const MoonsetState&,
                                      const Theme&) const {
    RecViz& v = rec_viz();
    v.update();   // consomme l'audio reçu : 1 pas par 50 ms
    // Texte du visualiseur un cran plus petit que le reste de l'écran (plus d'air autour des courbes).
    v.draw(c, x, y, WAVEFORM_W, WAVEFORM_H, CHAR_SPACING, std::max(1, FONT_SCALE - 1));
}

void MoonsetModule::draw_confirm_dialog(Canvas& c, int x, int y, const Theme& t) const {
    // Covers the editor. ⚠️ So it is not in `modal_backdrop_active`: the bars must not dim either.
    c.fill_rect(x, y, WIDTH, HEIGHT, t.background);

    draw_confirm_box(c, "ARE YOU SURE?", t);
}

// ─── The cursor ──────────────────────────────────────────────────────────────────────────────────

CursorContext MoonsetModule::cursor_context(const MoonsetState& s) const {
    // The confirm dialog owns every button while it is up.
    if (s.showConfirmClose) return cc::none();

    switch (s.cursorRow) {
        case 0: return cc::read_only();   // the title bar

        case 1:
            switch (s.cursorCol) {
                case 0: return cc::hex_byte(s.zoomLevel, 0, 4);
                // A mono sample: the cell reads MONO and cannot change (read_only, not none).
                case 1: return s.hasStereoData
                                   ? cc::toggle_ternary(at(source_values(), s.sourceMode), source_values())
                                   : cc::read_only();
                case 2: return cc::toggle_ternary(at(rate_values(), s.rateMode), rate_values());
                default: return cc::none();
            }

        case 2:
            switch (s.cursorCol) {
                // PITCH ±24 carried as 0..48; handle_input un-biases. A+B = +0.
                case 0: return cc::hex_byte(s.pitchSemitones + 24, 0, 48, -1, false, false, false,
                                            /*def=*/24);
                case 1: return cc::toggle_ternary(at(duration_values(), s.durationIndex), duration_values());
                case 2: {
                    // An 8-bit sample has nowhere lower to go.
                    const std::vector<std::string> choices = bit_depth_choices(s.sourceBitDepth);
                    if (choices.size() < 2) return cc::read_only();
                    return cc::toggle_ternary(std::to_string(s.bitDepth), choices);
                }
                default: return cc::none();
            }

        case 5:
            switch (s.cursorCol) {
                case 0: return cc::toggle_binary(s.recordActive);   // REC toggle
                case 1: {
                    // ⚠️ DEFAULT is the empty choice and stays IN the list, so the cycle can come back
                    // to it — otherwise a device, once picked, could never be cleared.
                    std::vector<std::string> devices{"DEFAULT"};
                    devices.insert(devices.end(), s.audioInputDevices.begin(), s.audioInputDevices.end());
                    return cc::toggle_ternary(s.recordInputDevice.empty() ? "DEFAULT" : s.recordInputDevice,
                                              devices);
                }
                case 2: return cc::toggle_binary(rec_viz().menuOpen());
                case 3: return cc::toggle_binary(s.loopPreview);            // LOOP
                default: return cc::none();
            }

        // Rows 3,4,6,7: A+DPAD drags a selection edge — the dispatcher's nudge_selection_edge.
        case 3: case 4: case 6: case 7:
            return cc::none();

        case 8:
            if (s.cursorCol == 2) return cc::toggle_binary(s.loopPreview);   // LOOP checkbox
            return cc::none();   // START / END are dragged, not stepped

        case 10:
            switch (s.cursorCol) {
                case 0: return cc::toggle_ternary(at(slice_methods(), s.sliceMethod), slice_methods());
                case 1:
                    if (s.sliceMethod == SLICE_TRANSIENT) return cc::hex_byte(s.sliceSensitivity, 0, 255);
                    if (s.sliceMethod == SLICE_DIVIDE)    return cc::hex_byte(s.sliceDivisions, 0, 255);
                    return cc::none();   // OFF has no parameter
                default: return cc::none();
            }

        case 11:
            // Col 1, the position, has no cell: A+DPAD drags the marker (nudge_slice_marker).
            if (s.cursorCol == 0 && s.sliceMethod != SLICE_OFF)
                return cc::hex_byte(s.sliceIndex, 0, s.slice_index_ceiling());
            return cc::none();

        case 13: case 14:
            return cc::none();   // the op buttons: A fires them, there is nothing to step

        case 16:
            switch (s.cursorCol) {
                case 0: return cc::toggle_ternary(at(fx_types(), s.fxType), fx_types());
                case 1:
                    // An EQ SLOT, as every EQ cell: "--" is empty, A+RIGHT takes slot 00, A+B clears.
                    if (s.fxType == FX_EQ)
                        return cc::eq_slot(s.fxEqSlot);
                    if (s.fxType == FX_SYNC) return cc::toggle_ternary(at(sync_types(), s.syncType), sync_types());
                    return cc::hex_byte(s.fxValue, 0, 255);                            // an AMOUNT
                default: return cc::none();   // col 2 = APPLY, an action
            }

        case 18: case 19:
            return cc::none();   // NAME opens the keyboard; the save buttons are actions

        default:
            return cc::none();
    }
}

// ─── The edit ────────────────────────────────────────────────────────────────────────────────────

MoonsetInputResult MoonsetModule::handle_input(MoonsetState& s,
                                                         const InputAction& action) const {
    MoonsetInputResult r;

    // The EQ slot is the one cell here that deletes and inserts.
    if (s.cursorRow == 16 && s.cursorCol == 1 && s.fxType == FX_EQ) {
        r.modified = apply_optional_byte(s.fxEqSlot, 127, action);
        return r;
    }

    if (action.type != ActionType::SET_VALUE) return r;   // every other cell is a SET_VALUE cell
    const int v = action.value;

    switch (s.cursorRow) {
        case 1:
            switch (s.cursorCol) {
                case 0: s.zoomLevel  = std::clamp(v, 0, 4); r.modified = true; break;
                case 1: s.sourceMode = v;                   r.modified = true; break;
                case 2:
                    // Destructive: the module reports it, the dispatcher makes the engine calls.
                    if (v != s.rateMode) r.rateModeChanged = true;
                    s.rateMode = v;
                    r.modified = true;
                    break;
                default: break;
            }
            break;

        case 2:
            switch (s.cursorCol) {
                case 0: s.pitchSemitones = v - 24;  r.modified = true; break;   // un-bias (see cursor_context)
                case 1: s.durationIndex  = v;       r.modified = true; break;
                case 2: {
                    // Destructive like RATE.
                    const int bits = std::atoi(at(bit_depth_choices(s.sourceBitDepth), v).c_str());
                    if (bits != s.bitDepth) r.bitDepthChanged = true;
                    s.bitDepth = bits;
                    r.modified = true;
                    break;
                }
                default: break;
            }
            break;

        case 5:
            switch (s.cursorCol) {
                case 0: s.recordActive = (v == 1);  r.modified = true; break;   // REC toggle via arrow keys
                case 1: {
                    // Cycle through input device names; "DEFAULT"/index 0 clears.
                    if (v <= 0 || v > static_cast<int>(s.audioInputDevices.size()))
                        s.recordInputDevice.clear();
                    else
                        s.recordInputDevice = s.audioInputDevices[static_cast<size_t>(v - 1)];
                    r.modified = true;
                    break;
                }
                case 3: s.loopPreview = (v == 1); r.modified = true; break;
                default: break;
            }
            break;

        case 8:
            if (s.cursorCol == 2) { s.loopPreview = (v == 1); r.modified = true; }
            break;

        case 10:
            switch (s.cursorCol) {
                case 0:
                    s.sliceMethod = v;
                    // Clearing the detector's list makes the feed re-run it.
                    if (v == SLICE_TRANSIENT) s.transientMarkers.clear();
                    r.modified = true;
                    break;
                case 1:
                    if (s.sliceMethod == SLICE_TRANSIENT) {
                        s.sliceSensitivity = v;
                        s.transientMarkers.clear();   // re-detect at the new threshold
                    } else {
                        s.sliceDivisions = v;
                    }
                    r.modified = true;
                    break;
                default: break;
            }
            break;

        case 11:
            // Stepping the index selects that slice, so CROP/CHOP/preview act on the one in view.
            if (s.cursorCol == 0 && s.sliceMethod != SLICE_OFF) {
                int64_t start = 0, end = 0;
                s.slice_bounds(v, start, end);
                s.sliceIndex      = v;
                s.selectionStart  = start;
                s.selectionEnd    = end;
                r.modified        = true;
            }
            break;

        case 16:
            switch (s.cursorCol) {
                case 0: s.fxType = v; r.modified = true; break;
                case 1:
                    if (s.fxType == FX_SYNC) s.syncType = v;
                    else                     s.fxValue  = v;
                    r.modified = true;
                    break;
                default: break;
            }
            break;

        default:
            break;
    }

    return r;
}

// ─── The TSTRETCH box ────────────────────────────────────────────────────────────────────────────

namespace {

// Under the waveform, so the playhead of a preview runs in view above the box.
constexpr int STRETCH_BOX_W   = 320;
constexpr int STRETCH_BOX_X   = (DESIGN_W - STRETCH_BOX_W) / 2;
constexpr int STRETCH_BOX_Y   = MoonsetModule::WAVEFORM_Y + MoonsetModule::WAVEFORM_H + 8;
constexpr int STRETCH_PAD     = 8;
constexpr int STRETCH_GAP     = 8;    // between the title and the lines
constexpr int STRETCH_LABEL_X = STRETCH_BOX_X + 14;
constexpr int STRETCH_VALUE_X = STRETCH_LABEL_X + 10 * CHAR_W;

/** One line of the box: a label and the cell beside it. No cell = a reading, no label = a blank line. */
struct StretchLine {
    const char* label;
    int         cell;   // a MoonsetStretchCell, or −1
};
constexpr StretchLine STRETCH_LINES[] = {
    {"ORIG BPM", -1},
    {"DURATION", static_cast<int>(MoonsetStretchCell::DURATION)},
    {"NEW BPM",  static_cast<int>(MoonsetStretchCell::BPM)},
    {"PITCH",    static_cast<int>(MoonsetStretchCell::PITCH)},
    {nullptr,    -1},
    {"GRAIN",    static_cast<int>(MoonsetStretchCell::GRAIN)},
    {nullptr,    -1},
};
constexpr int STRETCH_LINE_COUNT = static_cast<int>(sizeof(STRETCH_LINES) / sizeof(STRETCH_LINES[0]));

// The title, air, the lines, then the PREVIEW / APPLY line.
constexpr int STRETCH_BOX_H =
    ROW_HEIGHT + STRETCH_GAP + (STRETCH_LINE_COUNT + 1) * ROW_HEIGHT + 2 * STRETCH_PAD;
static_assert(STRETCH_BOX_Y + STRETCH_BOX_H + MODAL_BORDER - 1 <= DESIGN_H,
              "the TSTRETCH box runs off the bottom of the screen");

std::string pad3(int v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", std::clamp(v, 0, 999));
    return std::string(buf);
}

/** PITCH as row 2 shows it: "+0", "+7", "-12". */
std::string signed_semitones(int v) { return (v >= 0 ? "+" : "") + std::to_string(v); }

std::string moonset_stretch_value(const MoonsetState& s, MoonsetStretchCell cell, int song_tempo) {
    switch (cell) {
        case MoonsetStretchCell::DURATION: return at(MoonsetModule::duration_values(), s.durationIndex);
        case MoonsetStretchCell::BPM:      return pad3(s.stretch_bpm(song_tempo));
        case MoonsetStretchCell::PITCH:    return signed_semitones(s.pitchSemitones);
        case MoonsetStretchCell::GRAIN:    return hex2(s.stretchGrainMs);
        default:                    return "";
    }
}

}  // namespace

MoonsetStretchCell moonset_stretch_step(MoonsetStretchCell from, int d_row, int d_col) {
    const bool onButtons = (from == MoonsetStretchCell::PREVIEW || from == MoonsetStretchCell::APPLY);
    if (d_col != 0 && onButtons) return (d_col > 0) ? MoonsetStretchCell::APPLY : MoonsetStretchCell::PREVIEW;
    if (d_row == 0) return from;
    if (onButtons) return (d_row < 0) ? MoonsetStretchCell::GRAIN : from;
    return static_cast<MoonsetStretchCell>(std::clamp(static_cast<int>(from) + d_row,
                                               static_cast<int>(MoonsetStretchCell::DURATION),
                                               static_cast<int>(MoonsetStretchCell::PREVIEW)));
}

CursorContext moonset_stretch_cursor_context(const MoonsetState& s, int song_tempo) {
    using M = MoonsetModule;
    switch (s.stretchCursor) {
        case MoonsetStretchCell::DURATION:
            return cc::toggle_ternary(at(M::duration_values(), s.durationIndex), M::duration_values());
        case MoonsetStretchCell::BPM: {
            // The song TEMPO's own range and steps; A+B goes back to following it.
            CursorContext c = cc::hex_byte(s.stretch_bpm(song_tempo), 20, 999, -1, false, false, false,
                                           /*def=*/song_tempo);
            c.largeStep = 10;
            return c;
        }
        case MoonsetStretchCell::PITCH:
            // Row 2's own cell and bias; here A+B takes it back to +0.
            return cc::hex_byte(s.pitchSemitones + 24, 0, 48, -1, false, false, false, /*def=*/24);
        case MoonsetStretchCell::GRAIN:
            return cc::hex_byte(s.stretchGrainMs, 5, 255, -1, false, false, false, /*def=*/40);
        default:
            return cc::none();   // PREVIEW and APPLY are actions
    }
}

void moonset_stretch_handle_input(MoonsetState& s, const InputAction& action, int song_tempo) {
    if (action.type != ActionType::SET_VALUE) return;
    const int v = action.value;
    switch (s.stretchCursor) {
        case MoonsetStretchCell::DURATION: s.durationIndex = v; break;
        // ⚠️ The song's own tempo is stored as 0, so the cell follows the song rather than remembering it.
        case MoonsetStretchCell::BPM:      s.stretchBpm = (v == song_tempo) ? 0 : v; break;
        case MoonsetStretchCell::PITCH:    s.pitchSemitones = v - 24; break;
        case MoonsetStretchCell::GRAIN:    s.stretchGrainMs = v; break;
        default: break;
    }
}

void draw_moonset_stretch_editor(Canvas& c, const MoonsetState& s, int song_tempo, const Theme& t) {
    if (!s.stretchOpen) return;

    c.reset_clip();
    draw_modal_backdrop(c);
    draw_modal_box(c, STRETCH_BOX_X, STRETCH_BOX_Y, STRETCH_BOX_W, STRETCH_BOX_H, t);

    const std::string title = "TIMESTRETCH";
    int ty = STRETCH_BOX_Y + STRETCH_PAD + TEXT_PADDING;
    c.draw_text(title, STRETCH_BOX_X + (STRETCH_BOX_W - static_cast<int>(title.size()) * CHAR_W) / 2,
                ty, t.textTitle, CHAR_SPACING, FONT_SCALE);
    ty += ROW_HEIGHT + STRETCH_GAP;

    auto on = [&](MoonsetStretchCell cell) { return s.stretchCursor == cell; };

    for (const StretchLine& line : STRETCH_LINES) {
        if (line.label != nullptr) {
            const bool        isCell = (line.cell >= 0);
            const MoonsetStretchCell cell   = static_cast<MoonsetStretchCell>(std::max(line.cell, 0));
            const bool        cur    = isCell && on(cell);
            c.draw_text(line.label, STRETCH_LABEL_X, ty, cur ? cursor_mark_ink(t) : t.textParam,
                        CHAR_SPACING, FONT_SCALE);
            if (isCell) {
                draw_cursor_cell(c, moonset_stretch_value(s, cell, song_tempo), STRETCH_VALUE_X, ty, cur,
                                 t.textValue, t);
            } else {
                // The header's reading: the sample's tempo at this DURATION, the PITCH below counted in.
                const int bpm = s.source_bpm();
                c.draw_text(bpm > 0 ? pad3(bpm) : "---", STRETCH_VALUE_X, ty, t.textParam, CHAR_SPACING,
                            FONT_SCALE);
            }
        }
        ty += ROW_HEIGHT;
    }

    draw_cursor_cell(c, "PREVIEW", STRETCH_LABEL_X, ty, on(MoonsetStretchCell::PREVIEW), t.textValue, t);
    draw_cursor_cell(c, "APPLY",   STRETCH_VALUE_X, ty, on(MoonsetStretchCell::APPLY),   t.textValue, t);
}

// ── The granular block (GRAIN / ROAM / MOTION) ──────────────────────────────────────────────────
// The model keeps raw 0..255 bytes; every cell here shows the physical value the engine derives.

namespace {

const char* const GRAIN_LABELS[] = {
    "STATE", "POSITION", "SIZE", "DENSITY", "SPRAY", "PITCH", "TIME",
    "WINDOW", "SPREAD", "G ATTACK", "G DECAY", "G PAN", "P JITTER", "MIX",
};
const char* const ROAM_LABELS[] = {
    "STATE", "MODE", "RANGE", "WANDER", "SPEED", "SMOOTH", "BPM", "FREEZE",
    "D CURVE", "DIR", "CURVE", "DRIFT", "STEPRATE", "INERTIA", "TARGET", "T LOCK",
};
const char* const MOTION_LABELS[] = {
    "STATE", "JUMP", "HOLD", "REV", "ELASTIC", "CHAOS", "STUTTER", "TAPESTOP",
    "TUNNEL", "PROB", "LFO DPTH", "STEPGLD", "TRIGRATE", "SPIN", "FACTOR",
};
constexpr int GRAIN_ROWS  = static_cast<int>(sizeof(GRAIN_LABELS) / sizeof(GRAIN_LABELS[0]));
constexpr int ROAM_ROWS   = static_cast<int>(sizeof(ROAM_LABELS) / sizeof(ROAM_LABELS[0]));
constexpr int MOTION_ROWS = static_cast<int>(sizeof(MOTION_LABELS) / sizeof(MOTION_LABELS[0]));

enum class GK { BOOL, ENUM, BYTE };

struct GRowRef {
    GK   kind      = GK::BYTE;
    int* value     = nullptr;
    int  enumCount = 0;
};

const char* const TIME_OPTS[]   = {"HZ", "BPM"};
const char* const WINDOW_OPTS[] = {"HANN", "TRI", "RECT", "GAUSS", "EXPO"};
const char* const MODE_OPTS[]   = {"BOUNCE", "LOOP", "DRIFT", "RANDOM"};
const char* const FREEZE_OPTS[] = {"OFF", "ON"};
const char* const DCURVE_OPTS[] = {"LIN", "EASE", "EXP", "SINE"};
const char* const DIR_OPTS[]    = {"FWD", "REV", "RND"};
const char* const CURVE_OPTS[]  = {"LIN", "SINE", "STEP"};
const char* const DRIFT_OPTS[]  = {"BOUNCE", "LOOP", "DRIFT"};

const char* const* enum_options(int col, int line) {
    switch (col) {
        case 0:
            if (line == 6) return TIME_OPTS;
            if (line == 7) return WINDOW_OPTS;
            break;
        case 1:
            switch (line) {
                case 1:  return MODE_OPTS;
                case 7:  return FREEZE_OPTS;
                case 8:  return DCURVE_OPTS;
                case 9:  return DIR_OPTS;
                case 10: return CURVE_OPTS;
                case 11: return DRIFT_OPTS;
                case 15: return FREEZE_OPTS;
                default: break;
            }
            break;
        default: break;
    }
    return nullptr;
}

const char* label_for(int col, int line) {
    switch (col) {
        case 0: return (line >= 0 && line < GRAIN_ROWS) ? GRAIN_LABELS[line] : "";
        case 1: return (line >= 0 && line < ROAM_ROWS) ? ROAM_LABELS[line] : "";
        default: return (line >= 0 && line < MOTION_ROWS) ? MOTION_LABELS[line] : "";
    }
}

bool* state_for(songcore::Instrument::GranularParams& g, int col) {
    switch (col) {
        case 0: return &g.grain.state;
        case 1: return &g.roam.state;
        default: return &g.motion.state;
    }
}

GRowRef row_ref(songcore::Instrument::GranularParams& g, int col, int line) {
    GRowRef r;
    if (line == 0) {
        r.kind = GK::BOOL;
        return r;
    }
    switch (col) {
        case 0: {
            auto& b = g.grain;
            switch (line) {
                case 1:  r.value = &b.position; break;
                case 2:  r.value = &b.size; break;
                case 3:
                    if (g.grain.time == 1) {
                        r.kind = GK::ENUM;
                        r.value = &b.density_div;
                        r.enumCount = GRAN_DIV_COUNT;
                    } else {
                        r.value = &b.density;
                    }
                    break;
                case 4:  r.value = &b.spray; break;
                case 5:  r.value = &b.pitch; break;
                case 6:  r.kind = GK::ENUM; r.value = &b.time; r.enumCount = 2; break;
                case 7:  r.kind = GK::ENUM; r.value = &b.window; r.enumCount = 5; break;
                case 8:  r.value = &b.spread; break;
                case 9:  r.value = &b.attack; break;
                case 10: r.value = &b.decay; break;
                case 11: r.value = &b.pan; break;
                case 12: r.value = &b.jitter; break;
                case 13: r.value = &b.mix; break;
                default: break;
            }
            break;
        }
        case 1: {
            auto& b = g.roam;
            switch (line) {
                case 1:  r.kind = GK::ENUM; r.value = &b.mode; r.enumCount = 4; break;
                case 2:  r.value = &b.range; break;
                case 3:  r.value = &b.wander; break;
                case 4:
                    if (g.grain.time == 1) {
                        r.kind = GK::ENUM;
                        r.value = &g.roam.speed_div;
                        r.enumCount = GRAN_DIV_COUNT;
                    } else {
                        r.value = &b.speed;
                    }
                    break;
                case 5:  r.value = &b.smooth; break;
                case 6:  r.value = &b.bpm; break;
                case 7:  r.kind = GK::ENUM; r.value = &b.freeze; r.enumCount = 2; break;
                case 8:  r.kind = GK::ENUM; r.value = &b.dcurve; r.enumCount = 4; break;
                case 9:  r.kind = GK::ENUM; r.value = &b.dir; r.enumCount = 3; break;
                case 10: r.kind = GK::ENUM; r.value = &b.curve; r.enumCount = 3; break;
                case 11: r.kind = GK::ENUM; r.value = &b.drift; r.enumCount = 3; break;
                case 12: r.value = &b.step_rate; break;
                case 13: r.value = &b.inertia; break;
                case 14: r.value = &b.target; break;
                case 15: r.kind = GK::ENUM; r.value = &b.tlock; r.enumCount = 2; break;
                default: break;
            }
            break;
        }
        default: {
            auto& b = g.motion;
            switch (line) {
                case 1:  r.value = &b.jump; break;
                case 2:  r.value = &b.hold; break;
                case 3:  r.value = &b.rev; break;
                case 4:  r.value = &b.elastic; break;
                case 5:  r.value = &b.chaos; break;
                case 6:  r.value = &b.stutter; break;
                case 7:  r.value = &b.tapestop; break;
                case 8:  r.value = &b.tunnel; break;
                case 9:  r.value = &b.probability; break;
                case 10: r.value = &b.lfo_depth; break;
                case 11: r.value = &b.step_glide; break;
                case 12: r.value = &b.trigger; break;
                case 13: r.value = &b.spin; break;
                case 14: r.value = &b.factor; break;
                default: break;
            }
            break;
        }
    }
    return r;
}

int default_value(int col, int line) {
    if (col == 0) {
        if (line == 2) return 0x40;   // SIZE
        if (line == 3) return 0x80;   // DENSITY
        if (line == 5) return 0x80;   // PITCH
    } else if (col == 1) {
        if (line == 4) return 0x80;   // SPEED
        if (line == 5) return 0x40;   // SMOOTH
        if (line == 6) return 0x80;   // BPM
    } else if (col == 2) {
        if (line == 6) return 1;      // STUTTER
    }
    return 0;
}

std::string fmt_pct(int v) {
    return std::to_string((v * 100 + 127) / 255) + "%";
}
std::string fmt_signed_pct(int v) {
    const int p = static_cast<int>(std::lround((v - 128) * 100.0 / 127.0));
    return (p > 0 ? "+" : "") + std::to_string(p) + "%";
}
std::string fmt_ms(int v) {
    return std::to_string(1 + (v * 999) / 255) + "MS";
}
std::string fmt_hz(int v) {
    return std::to_string(1 + (v * 199) / 255) + "HZ";
}
std::string fmt_semis(int v) {
    const int s = static_cast<int>(std::lround((v - 128) * 24.0 / 128.0));
    return (s > 0 ? "+" : "") + std::to_string(s) + "ST";
}
std::string fmt_bpm(int v) {
    return std::to_string(20 + (v * 280) / 255) + "BPM";
}
std::string fmt_x(int v) {
    return std::to_string(1 + (v * 15) / 255) + "X";
}
std::string fmt_ct(int v) {
    return std::to_string((v * 1200) / 255) + "CT";
}
std::string div_name(int index) {
    const int i = std::clamp(index, 0, GRAN_DIV_COUNT - 1);
    return GRAN_DIVS[i].name;
}

}  // namespace

int moonset_granular_row_count(int col) {
    switch (col) {
        case 0:  return GRAIN_ROWS;
        case 1:  return ROAM_ROWS;
        case 2:  return MOTION_ROWS;
        default: return 0;
    }
}

std::string moonset_granular_value(const songcore::Instrument::GranularParams& g, int col, int line) {
    if (line == 0) {
        const bool on = (col == 0) ? g.grain.state : (col == 1) ? g.roam.state : g.motion.state;
        return on ? "ON" : "OFF";
    }

    const GRowRef r = row_ref(const_cast<songcore::Instrument::GranularParams&>(g), col, line);
    if (!r.value) return "";

    if (r.kind == GK::ENUM) {
        if (r.enumCount == GRAN_DIV_COUNT) return div_name(*r.value);
        const char* const* opts = enum_options(col, line);
        const int i = std::clamp(*r.value, 0, std::max(0, r.enumCount - 1));
        return (opts != nullptr) ? std::string(opts[i]) : std::to_string(*r.value);
    }

    const int v = std::clamp(*r.value, 0, 255);
    switch (col) {
        case 0:
            switch (line) {
                case 1:  return fmt_pct(v);
                case 2:  return fmt_ms(v);
                // DENSITY: a division while the cloud is locked to the song's tempo.
                case 3:  return (g.grain.time == 1) ? div_name(g.grain.density_div) : fmt_hz(v);
                case 5:  return fmt_semis(v);
                case 11: return fmt_signed_pct(v);
                case 12: return fmt_ct(v);
                default: return fmt_pct(v);
            }
        case 1:
            switch (line) {
                case 4:  return (g.grain.time == 1) ? div_name(g.roam.speed_div) : fmt_pct(v);
                case 6:  return fmt_bpm(v);
                case 12: return fmt_x(v);
                default: return fmt_pct(v);
            }
        default:
            return (line == 6) ? fmt_x(v) : fmt_pct(v);
    }
}

CursorContext moonset_granular_context(const songcore::Instrument::GranularParams& g, int col, int line) {
    if (line == 0) {
        const bool on = (col == 0) ? g.grain.state : (col == 1) ? g.roam.state : g.motion.state;
        return cc::toggle_binary(on);
    }

    const GRowRef r = row_ref(const_cast<songcore::Instrument::GranularParams&>(g), col, line);
    if (!r.value) return cc::none();
    if (r.kind == GK::ENUM) return cc::index_cycle(*r.value, r.enumCount);
    return cc::plain_byte(*r.value, default_value(col, line));
}

bool moonset_granular_apply(songcore::Instrument::GranularParams& g, int col, int line, int value) {
    if (line == 0) {
        bool* st = state_for(g, col);
        const bool nv = (value != 0);
        if (*st == nv) return false;
        *st = nv;
        return true;
    }

    GRowRef r = row_ref(g, col, line);
    if (!r.value) return false;
    const int nv = (r.kind == GK::ENUM)
                       ? std::clamp(value, 0, std::max(0, r.enumCount - 1))
                       : std::clamp(value, 0, 255);
    if (*r.value == nv) return false;
    *r.value = nv;
    return true;
}

void draw_moonset_granular(Canvas& c, int x, int gy,
                           const songcore::Instrument::GranularParams& g,
                           int cursorLine, int cursorCol, int top, const Theme& t) {
    constexpr int FS  = 2;    // the block is denser than the sample rows
    constexpr int SP  = 1;
    constexpr int GCW = 5 * FS + SP;
    constexpr int GCOL = 214;
    constexpr int RH   = 18;
    constexpr int GAP  = 22;  // title → first row

    static const char* const COLS[3] = {"GRAIN", "ROAM", "MOTION"};
    const int counts[3] = {GRAIN_ROWS, ROAM_ROWS, MOTION_ROWS};

    for (int ci = 0; ci < 3; ++ci) {
        const int  cx        = x + 7 + ci * GCOL;
        const bool activeCol = (ci == cursorCol);
        c.draw_text(COLS[ci], cx + 8, gy, activeCol ? t.textTitle : t.textParam, SP, FS);

        const int count = counts[ci];
        int first = std::clamp(top, 0, std::max(0, count - MoonsetState::GRANULAR_VISIBLE));
        // Keep the cursor's row on screen even if the caller's scroll is stale.
        if (ci == cursorCol) {
            if (cursorLine < first) first = cursorLine;
            if (cursorLine >= first + MoonsetState::GRANULAR_VISIBLE)
                first = cursorLine - MoonsetState::GRANULAR_VISIBLE + 1;
            first = std::clamp(first, 0, std::max(0, count - MoonsetState::GRANULAR_VISIBLE));
        }

        for (int i = 0; i < MoonsetState::GRANULAR_VISIBLE && first + i < count; ++i) {
            const int  line = first + i;
            const int  ty   = gy + GAP + i * RH;
            const bool on   = activeCol && (line == cursorLine);

            c.draw_text(label_for(ci, line), cx + 8, ty,
                        on ? cursor_mark_ink(t) : t.textParam, SP, FS);

            const std::string val = moonset_granular_value(g, ci, line);
            const int vx = cx + 8 + 9 * GCW;
            if (on)
                c.fill_rect(vx - SP, ty - 2,
                            Canvas::text_width(val, SP, FS) + 2 * SP, 5 * FS + 4, t.rowCursor);
            c.draw_text(val, vx, ty, on ? cursor_cell_ink(t) : t.textValue, SP, FS);
        }
    }

    // The focused column's own scroll: a triangle above while rows are hidden above, below while
    // more remain under the last visible one.
    const int count = counts[std::clamp(cursorCol, 0, 2)];
    const int first = std::clamp(top, 0, std::max(0, count - MoonsetState::GRANULAR_VISIBLE));
    if (first + MoonsetState::GRANULAR_VISIBLE < count)
        c.draw_text("V", x + 618, gy + GAP + (MoonsetState::GRANULAR_VISIBLE - 1) * RH, t.textValue, SP, FS);
    if (first > 0)
        c.draw_text("^", x + 618, gy + GAP, t.textValue, SP, FS);
}

}  // namespace pt::ui
