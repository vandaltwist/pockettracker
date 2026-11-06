#include "ui/modules/moonset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/helpers.h"
#include "ui/modules/confirm_dialog.h"
#include "ui/modules/granular_ui.h"
#include "granular/granular-voice.h"   // GranTarget / GranSource
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
        case 18: return 19;
        case 19: return 1;    // wrap to the first row
        default: return row + 1;
    }
}

int MoonsetModule::max_col_for_row(int row, int slice_method) {
    if (row == 1 || row == 2) return 2;
    if (row == 5) return 3;          // RECORD: REC / REC WAV / INPUT / SET (left to right)
    if (row >= 3 && row <= 7) return 1;          // the selection's two edges (START / END)
    if (row == 8) return 2;                      // START / END / LOOP checkbox
    if (row == 10) return slice_has_parameter(slice_method) ? 1 : 0;
    if (row == 11) return 1;    if (row == 13 || row == 14) return 5;        // six ops per row
    if (row == 16) return 2;                     // TYPE / VALUE / APPLY
    if (row == 18) return 0;                     // NAME — the whole row is one cell
    if (row == 19) return (slice_method == SLICE_OFF) ? 2 : 3;   // LOAD / SAVE / OVERWRITE / CHOP
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
        case MoonsetModule::SLICE_OFF:       return (marker_count() > 0) ? marker_count() : 0;
        default:                                  return 0;
    }
}

void MoonsetState::slice_bounds(int idx, int64_t& start, int64_t& end) const {
    const int64_t total = static_cast<int64_t>(totalFrames);
    if (sliceMethod == MoonsetModule::SLICE_OFF && (idx == 0 || marker_count() == 0)) {
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

int MoonsetState::reading_slice_index() const {
    // -1 = no tracker slice is sounding: the cloud reads the WHOLE sample at GRAIN POSITION. The
    // slicing tool's cursor is deliberately NOT a fallback — TRANSIENT/DIVIDE/MANUAL exist to cut
    // the sample, and must never drive the grain or its red animation.
    if (granularSliceIndex < 0) return -1;
    return std::clamp(granularSliceIndex, 0, slice_index_ceiling());
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
    const int oy = y;

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
        const bool     onWavCell = (cur && s.cursorCol == 1);
        const uint32_t recInk = s.recordActive && !s.recordToWav
            ? (s.recordLevel > 0.01f ? 0xFFFF0000u : 0xFFFFAA00u)   // red if sound, amber if silent
            : t.textValue;
        c.draw_text("RECORD", x + 10, ty,
                    (onRecCell || onWavCell) ? cursor_mark_ink(t) : t.textParam,
                    CHAR_SPACING, FONT_SCALE);
        if (s.recordActive && !s.recordToWav && !s.recordFailed &&
            s.recordFrames >= s.recordSkipFrames) {
            // « REC » puis le temps COLLÉ et plus petit : la cellule reste dans sa place devant INPUT.
            char tb[24];
            std::snprintf(tb, sizeof(tb), "%.1fs", (s.recordFrames - s.recordSkipFrames) / 48000.0f);
            const std::string timeStr = tb;
            const int recX = x + 120, fs2 = FONT_SCALE - 1, cs2 = 1, gap = 3;
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
                                      : (s.recordActive && !s.recordToWav) ? std::string("COUNT-IN")
                                                                           : std::string("REC");
            draw_cursor_cell(c, recText, x + 120, ty, onRecCell, recInk, t);
        }

        // REC WAV: the same capture, written to a new file instead of into the instrument.
        const uint32_t wavInk = s.recordActive && s.recordToWav
            ? (s.recordLevel > 0.01f ? 0xFFFF0000u : 0xFFFFAA00u)
            : t.textValue;
        if (s.recordActive && s.recordToWav && !s.recordFailed &&
            s.recordFrames >= s.recordSkipFrames) {
            char tb[24];
            std::snprintf(tb, sizeof(tb), "%.1fs", (s.recordFrames - s.recordSkipFrames) / 48000.0f);
            const std::string timeStr = tb;
            const int recX = x + 230, fs2 = FONT_SCALE - 1, cs2 = 1, gap = 3;
            const int wRec = Canvas::text_width("REC", CHAR_SPACING, FONT_SCALE);
            const int wAll = wRec + gap + Canvas::text_width(timeStr, cs2, fs2);
            if (onWavCell)
                c.fill_rect(recX - CHAR_SPACING, ty - TEXT_PADDING, wAll + 2 * CHAR_SPACING, ROW_HEIGHT,
                            t.rowCursor);
            const uint32_t ink = onWavCell ? cursor_cell_ink(t) : wavInk;
            c.draw_text("REC", recX, ty, ink, CHAR_SPACING, FONT_SCALE);
            c.draw_text(timeStr, recX + wRec + gap, ty + (FONT_SCALE - fs2) * 5 / 2, ink, cs2, fs2);
        } else {
            const std::string wavText = (s.recordActive && s.recordToWav) ? std::string("COUNT-IN")
                                                                          : std::string("REC WAV");
            draw_cursor_cell(c, wavText, x + 230, ty, onWavCell, wavInk, t);
        }

        std::string inputName = s.recordInputDevice.empty() ? "DEF" : s.recordInputDevice.substr(0, 3);
        while (!inputName.empty() && inputName.back() == ' ') inputName.pop_back();   // "M1 MASTER" -> "M1"
        c.draw_text("INPUT", x + 370, ty, cur && s.cursorCol == 2 ? cursor_mark_ink(t) : t.textParam,
                    CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, inputName, x + 460, ty, cur && s.cursorCol == 2, t.textValue, t);
        c.draw_text("SET", x + 520, ty, cur && s.cursorCol == 3 ? cursor_mark_ink(t) : t.textParam,
                    CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, rec_viz().menuOpen() ? "ON" : "OFF", x + 575, ty,
                         cur && s.cursorCol == 3, t.textValue, t);
    }

    draw_waveform(c, x, oy + WAVEFORM_Y, s, t, &g);

    // SHIFT swaps ONLY this section (SELECTION ... LOAD). The four header rows and the waveform -
    // its red playhead included - stay on screen above it.
    if (s.granularFocus) {
        draw_moonset_granular(c, x, oy + GRANULAR_Y, g, s.granularRow,
                              s.granularColumn, s.granularTop, t, s.sliceReading, &s);
    } else {
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

    }   // granularFocus

    draw_moonset_mod_window(c, s, g, t);
    if (s.presetMenuOpen) draw_moonset_preset_menu(c, s, t);
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

    // ── The granular visualizer: where and how the cloud reads the sample ───────────────────────
    // Red = the cloud. Every line moves with GRAIN / ROAM / MOTION and the MOD rows, even at rest:
    //   tall thick line ... the head: POS + ROAM + position LFO (the point the grains start from)
    //   thin lines ........ one per grain, moving at its own speed (PITCH, JITTER, TAPESTOP, CHAOS);
    //                       height = the window now (SHAPE of the grain), thin trace = its course left
    //   "<" on a grain .... it plays backwards (REV)       hollow square ... a grain skipped (PROB)
    //   top bracket ....... SPRAY (and a second one for CHAOS)   bottom bar ... ROAM range + its marker
    //   white line ........ the dry sample (MIX under FF); text = what the cloud does right now
    // Playing: the engine's real grain positions are drawn; at rest, the same logic is simulated.
    if (g && g->master && s.totalFrames > 0) {
        const float totalF = static_cast<float>(s.totalFrames);
        auto nx = [&](float n) { return frame_x(std::clamp(n, 0.0f, 1.0f) * totalF); };
        const Argb red = t.textGranular;

        int64_t sliceStartF = 0, sliceEndF = s.totalFrames;
        // Only when the tracker is really sounding a slice (SLICE FOLLOW). The slicing tool is a
        // cutter, not a transport: TRANSIENT/DIVIDE/MANUAL never turn this on.
        const int  readSlice = s.reading_slice_index();
        const bool hasSlice  = readSlice >= 0;
        if (hasSlice) {
            // SLICE FOLLOW: while the tracker plays a slice, the cloud reads THAT slice. Paused, it
            // reads the whole sample at GRAIN POSITION. The engine already resolves the note's slice
            // in program.h; this keeps the drawing on the same one.
            s.slice_bounds(readSlice, sliceStartF, sliceEndF);
        }
        const float lo = static_cast<float>(sliceStartF) / totalF;
        const float hi = static_cast<float>(sliceEndF) / totalF;

        s.preview.advance(*g, s.nowMs, lo, hi, hasSlice,
                          totalF / static_cast<float>(std::max(s.sampleRate, 1)), 120.0f);
        const GrainPreview& pv = s.preview;

        auto inside = [&](float px) { return px >= static_cast<float>(wfLeft) && px <= static_cast<float>(wfRight); };
        auto hline  = [&](float xa, float xb, int yy, int h) {   // clipped horizontal bar
            const int l = std::max(static_cast<int>(std::min(xa, xb)), wfLeft);
            const int r = std::min(static_cast<int>(std::max(xa, xb)), wfLast);
            if (r > l) c.fill_rect(l, yy, r - l, h, red);
        };

        // Slice bounds: the zone the grains read in (and loop inside).
        if (hasSlice) {
            const float sx = nx(lo), ex = nx(hi);
            if (inside(sx)) v_line(c, static_cast<int>(sx), y, WAVEFORM_H, red);
            if (inside(ex)) v_line(c, std::min(static_cast<int>(ex), wfLast), y, WAVEFORM_H, red);
            hline(sx, ex, y + WAVEFORM_H - 3, 3);
        }

        // ROAM: the range the head travels, and where it is on it right now.
        if (g->roam.state) {
            const float ax = nx(pv.anchor), rx = nx(pv.rangeEnd);
            hline(ax, rx, y + WAVEFORM_H - 10, 2);
            if (inside(ax)) v_line(c, static_cast<int>(ax), y + WAVEFORM_H - 13, 8, red);
            if (g->roam.tlock > 0) {
                const float tx = nx(static_cast<float>(g->roam.target) / 255.0f);
                if (inside(tx)) v_line(c, static_cast<int>(tx), y + WAVEFORM_H - 20, 6, red);
            }
        }

        // SPRAY (and CHAOS): the brackets the grains start inside.
        if (g->grain.state) {
            const float hl = nx(pv.head - pv.sprayHalf), hr = nx(pv.head + pv.sprayHalf);
            if (pv.sprayHalf > 0.0f) {
                hline(hl, hr, y + 3, 1);
                if (inside(hl)) v_line(c, static_cast<int>(hl), y + 3, 6, red);
                if (inside(hr)) v_line(c, std::min(static_cast<int>(hr), wfLast), y + 3, 6, red);
            }
            if (pv.chaosHalf > 0.0f)
                hline(nx(pv.head - pv.chaosHalf), nx(pv.head + pv.chaosHalf), y + 11, 1);
        }

        // The grains.
        if (g->grain.state) {
            // Engine grain positions (accurate during playback) on top of the animated simulation:
            // the simulation runs in every case, so the three columns stay animated while playing.
            if (s.grainHeadCount > 0) {
                const int n = std::min(s.grainHeadCount, MoonsetState::GRAIN_HEADS_MAX);
                for (int i = 0; i < n; ++i) {
                    const float px = nx(s.grainHeads[i]);
                    if (inside(px)) v_line(c, std::min(static_cast<int>(px), wfLast), midY - 24, 48, red);
                }
            }
            // Animated simulation (runs at rest AND during playback):
            for (const auto& gr : pv.g) {
                if (!gr.on) continue;
                hline(nx(gr.pos), nx(GrainPreview::end_pos(gr)), midY, 1);   // its course left
            }
            for (const auto& gr : pv.g) {
                if (!gr.on) continue;
                const float px = nx(gr.pos);
                if (!inside(px)) continue;
                const int   x  = std::min(static_cast<int>(px), wfLast);
                const int   lh = 8 + static_cast<int>(static_cast<float>(WAVEFORM_H - 24) * GrainPreview::window_gain(gr));
                const int   ty = midY - lh / 2;
                v_line(c, x, ty, lh, red);
                if (gr.rev) {          // "<" : reads backwards
                    c.fill_rect(x + 2, ty - 5, 1, 1, red); c.fill_rect(x + 1, ty - 4, 1, 1, red);
                    c.fill_rect(x,     ty - 3, 1, 1, red); c.fill_rect(x + 1, ty - 2, 1, 1, red);
                    c.fill_rect(x + 2, ty - 1, 1, 1, red);
                }
            }
            // The head itself.
            const float hx = nx(pv.head);
            if (inside(hx)) v_line(c, std::min(static_cast<int>(hx), wfLast), y, WAVEFORM_H, red, 2);

            // A skipped grain (PROB): a hollow square where it would have been, for a moment.
            if (s.nowMs - pv.lastSkipMs < 180) {
                const float kx = nx(pv.lastSkipAt);
                if (inside(kx)) {
                    const int x0 = static_cast<int>(kx) - 3, y0 = midY - 3;
                    c.fill_rect(x0, y0, 7, 1, red); c.fill_rect(x0, y0 + 6, 7, 1, red);
                    c.fill_rect(x0, y0, 1, 7, red); c.fill_rect(x0 + 6, y0, 1, 7, red);
                }
            }
        }

        // The dry sample (MIX under FF): a white line at normal speed.
        const float mixN   = static_cast<float>(std::clamp(g->grain.mix, 0, 255)) / 255.0f;
        const float dryGn  = g->grain.state ? std::min(1.0f, 2.0f * (1.0f - mixN)) : 1.0f;
        if (dryGn > 0.01f) {
            const float dx = nx(pv.dryPos);
            if (inside(dx)) v_line(c, std::min(static_cast<int>(dx), wfLast), y + WAVEFORM_H - 40, 40,
                                   t.vizWave, dryGn > 0.5f ? 2 : 1);
        }

        // What the cloud is doing, in numbers.
        char info[64];
        std::snprintf(info, sizeof info, "%dHZ %dMS %+.1fST N%d", static_cast<int>(pv.densHz),
                      static_cast<int>(pv.sizeMs), pv.semis, pv.count);
        c.draw_text(info, wfLeft + 4, y + 16, red, CHAR_SPACING, FONT_SCALE);
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
                case 1: return cc::none();                          // REC WAV: an action, not a value
                case 2: {
                    // ⚠️ DEFAULT is the empty choice and stays IN the list, so the cycle can come back
                    // to it — otherwise a device, once picked, could never be cleared.
                    std::vector<std::string> devices{"DEFAULT"};
                    devices.insert(devices.end(), s.audioInputDevices.begin(), s.audioInputDevices.end());
                    return cc::toggle_ternary(s.recordInputDevice.empty() ? "DEFAULT" : s.recordInputDevice,
                                              devices);
                }
                case 3: return cc::toggle_binary(rec_viz().menuOpen());
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
                case 2: {
                    // Cycle through input device names; "DEFAULT"/index 0 clears.
                    if (v <= 0 || v > static_cast<int>(s.audioInputDevices.size()))
                        s.recordInputDevice.clear();
                    else
                        s.recordInputDevice = s.audioInputDevices[static_cast<size_t>(v - 1)];
                    r.modified = true;
                    break;
                }
                default: break;
            }
            break;

        case 8:
            // LOOP: a plain binary cell. Its value only changes when the requested one differs, so a
            // held arrow does not keep reporting an edit it did not make.
            if (s.cursorCol == 2) {
                const bool nv = (v == 1);
                if (nv != s.loopPreview) {
                    s.loopPreview = nv;
                    r.modified    = true;
                }
            }
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
    "WINDOW", "SPREAD", "G ATTACK", "G DECAY", "G PAN", "P JITTER", "STRETCH", "MIX",
};
const char* const ROAM_LABELS[] = {
    "STATE", "MODE", "RANGE", "WANDER", "SPEED", "SMOOTH", "BPM", "FREEZE",
    "D CURVE", "DIR", "CURVE", "DRIFT", "STEPRATE", "INERTIA", "TARGET", "T LOCK", "MIX",
};
const char* const MOTION_LABELS[] = {
    "STATE", "JUMP", "HOLD", "REV", "ELASTIC", "CHAOS", "STUTTER", "TAPESTOP",
    "TUNNEL", "PROB", "LFO DPTH", "STEPGLD", "TRIGRATE", "SPIN", "FACTOR", "MIX",
};
constexpr int GRAIN_ROWS  = static_cast<int>(sizeof(GRAIN_LABELS) / sizeof(GRAIN_LABELS[0]));
constexpr int ROAM_ROWS   = static_cast<int>(sizeof(ROAM_LABELS) / sizeof(ROAM_LABELS[0]));
constexpr int MOTION_ROWS = static_cast<int>(sizeof(MOTION_LABELS) / sizeof(MOTION_LABELS[0]));

// MIX is one parameter shown at the bottom of all three columns: the same cell, copied.
bool is_mix_row(int col, int line) {
    return (col == 0 && line == GRAIN_ROWS - 1) || (col == 1 && line == ROAM_ROWS - 1) ||
           (col == 2 && line == MOTION_ROWS - 1);
}

enum class GK { BOOL, ENUM, BYTE };

struct GRowRef {
    GK   kind      = GK::BYTE;
    int* value     = nullptr;
    int  enumCount = 0;
};

const char* const TIME_OPTS[]   = {"HZ", "BPM"};
const char* const WINDOW_OPTS[] = {"HANN", "TRI", "RECT", "GAUSS", "EXPO"};
const char* const MODE_OPTS[]   = {"BOUNCE", "LOOP", "DRIFT", "RANDOM", "SLC"};
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
                case 13: r.value = &b.stretch; break;
                case 14: r.value = &b.mix; break;
                default: break;
            }
            break;
        }
        case 1: {
            auto& b = g.roam;
            switch (line) {
                case 1:  r.kind = GK::ENUM; r.value = &b.mode; r.enumCount = 5; break;
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
                case 16: r.value = &g.roam.mix; break;   // MIX: own mix
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
                case 15: r.value = &g.motion.mix; break;   // MIX: own mix
                default: break;
            }
            break;
        }
    }
    return r;
}

int default_value(int col, int line) {
    // A+B: 0 everywhere, except the three values that have no musical meaning at 0.
    if (col == 0) {
        if (line == 3)  return 0x80;   // DENSITY: one grain per millisecond
        if (line == 5)  return 0x80;   // PITCH: centre (unison)
        if (line == 14) return 0xFF;   // MIX: the cloud alone
    }
    return 0;
}

// A+UP / A+DOWN: ten at a time, except the cells that need a fine touch (one at a time, with the
// held-button repeat for the long scroll).
int granular_large_step(int col, int line) {
    if (col == 0) {
        switch (line) {
            case 5:  // PITCH
            case 11: // G PAN
            case 12: // P JITTER
            case 14: // MIX
                return 1;
            default: break;
        }
    }
    return 10;
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
        case 0:  return GRAIN_ROWS + 1;   // + the PRESET cell
        case 1:  return ROAM_ROWS;
        case 2:  return MOTION_ROWS;
        default: return 0;
    }
}

int moonset_preset_row() { return GRAIN_ROWS; }

std::string moonset_granular_value(const songcore::Instrument::GranularParams& g, int col, int line, bool sliceReading) {
    if (line == 0) {
        const bool on = (col == 0) ? g.grain.state : (col == 1) ? g.roam.state : g.motion.state;
        std::string v = on ? "ON" : "OFF";
        // Master/shared indicator: STATE and MIX are the cloud's master.
        if (col == 0) v += " M";
        return v;
    }

    // POSITION stays editable while slices are read: it is relative to the slice window, and the
    // head has to move for SPRAY / ROAM / MOTION to travel with it.

    if (is_mix_row(col, line)) {   // per-column MIX cell
        const int v = (col == 0) ? std::clamp(g.grain.mix, 0, 255)
                    : (col == 1) ? std::clamp(g.roam.mix, 0, 255)
                                 : std::clamp(g.motion.mix, 0, 255);
        std::string s = v == 0 ? "DRY" : (v == 255 ? "WET" : fmt_pct(v));
        s += " M";
        return s;
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
                case 13: return fmt_pct(v);   // STRETCH
                case 14: return v == 0 ? "DRY" : (v == 255 ? "WET" : fmt_pct(v));
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

CursorContext moonset_granular_context(const songcore::Instrument::GranularParams& g, int col, int line, bool sliceReading) {
    if (line == 0) {
        const bool on = (col == 0) ? g.grain.state : (col == 1) ? g.roam.state : g.motion.state;
        return cc::toggle_binary(on);
    }

    // POSITION stays editable while slices are read (it is relative to the slice window).

    const GRowRef r = row_ref(const_cast<songcore::Instrument::GranularParams&>(g), col, line);
    if (!r.value) return cc::none();
    if (r.kind == GK::ENUM) return cc::index_cycle(*r.value, r.enumCount);

    CursorContext c = cc::plain_byte(*r.value, default_value(col, line));
    c.largeStep = granular_large_step(col, line);
    return c;
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
                           int cursorLine, int cursorCol, int top, const Theme& t,
                           bool sliceReading, const MoonsetState* sp) {
    constexpr int FS  = 2;    // the block is denser than the sample rows
    constexpr int SP  = 1;
    constexpr int GCW = 5 * FS + SP;
    constexpr int GCOL = 214;
    constexpr int RH   = 13;  // compact: the panel shares the screen with the waveform
    constexpr int GAP  = 14;  // title → first row

    static const char* const COLS[3] = {"GRAIN", "ROAM", "MOTION"};
    const int counts[3] = {GRAIN_ROWS + 1, ROAM_ROWS, MOTION_ROWS};   // GRAIN: + the PRESET cell

    for (int ci = 0; ci < 3; ++ci) {
        const int  cx        = x + 7 + ci * GCOL;
        const bool activeCol = (ci == cursorCol);
        c.draw_text(COLS[ci], cx + 8, gy, activeCol ? t.textTitle : t.textParam, SP, FS);

        // The PRESET cell rides in the column's title line — small, so the GRAIN parameters keep their
        // place. It still walks as the column's last row; A on it opens the SAVE / LOAD popup.
        if (ci == 0) {
            const std::string cell = "PRST >";
            const int         pw   = Canvas::text_width(cell, SP, FS);
            const int         px   = cx + GCOL - pw - 10;
            const bool        on   = activeCol && cursorLine == GRAIN_ROWS;
            if (on)
                c.fill_rect(px - SP, gy - 1, pw + 2 * SP, 5 * FS + 2, t.rowCursor);
            c.draw_text(cell, px, gy, on ? cursor_cell_ink(t) : t.textValue, SP, FS);
        }

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

            // The PRESET cell is drawn in the title line (above): its slot at the bottom stays empty.
            // ⚠️ Keep it out of that slot, or the panel shows it twice.
            if (ci == 0 && line == GRAIN_ROWS) continue;

            c.draw_text(label_for(ci, line), cx + 8, ty,
                        on ? cursor_mark_ink(t) : t.textParam, SP, FS);

            std::string val = moonset_granular_value(g, ci, line, sliceReading);
            if (ci == 0 && line == 1 && sp && sp->trackerSliceActive) {
                val = "SLC " + std::to_string(sp->trackerSliceDisplay + 1);
            }
            const int vx = cx + 8 + 9 * GCW;
            if (on)
                c.fill_rect(vx - SP, ty - 2,
                            Canvas::text_width(val, SP, FS) + 2 * SP, 5 * FS + 4, t.rowCursor);
            c.draw_text(val, vx, ty, on ? cursor_cell_ink(t) : t.textValue, SP, FS);

            // Blinking red square for any cell that carries a live LFO/RANDOM/BUS modulator.
            if (sp) {
                const int tgt = moonset_granular_target(ci, line);
                if (tgt != 0) {
                    for (int k = 0; k < pt::GRAN_MODS; ++k) {
                        if (g.mods[k].target != tgt || g.mods[k].src == pt::GS_NONE) continue;
                        const float rateByte = (float)std::clamp(g.mods[k].rate, 0, 255) / 255.0f;
                        const float hz = 0.05f * std::pow(400.0f, rateByte);
                        const float ph = (float)(sp->nowMs % 100000) * 0.001f * hz;
                        const bool on = (ph - std::floor(ph)) < 0.5f;
                        if (on) {
                            const int sqX = vx + (int)Canvas::text_width(val, SP, FS) + 4;
                            c.fill_rect(sqX, ty + 2, 4, 4, t.textGranular);
                        }
                        break;
                    }
                }
            }
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

// ── The GRAIN column's PRESET popup ─────────────────────────────────────────────────────────────

void draw_moonset_preset_menu(Canvas& c, const MoonsetState& s, const Theme& t) {
    constexpr int W    = 200;
    constexpr int PAD  = 14;
    constexpr int ROWS = 2;   // SAVE / LOAD
    const int H = PAD * 2 + ROW_HEIGHT * (ROWS + 1) + 8;
    const int X = (DESIGN_W - W) / 2;
    const int Y = (DESIGN_H - H) / 2;

    c.reset_clip();
    draw_modal_backdrop(c);
    draw_modal_box(c, X, Y, W, H, t);

    const std::string title = "GRAIN PRESET";
    int ty = Y + PAD + TEXT_PADDING;
    c.draw_text(title, X + (W - Canvas::text_width(title, CHAR_SPACING, FONT_SCALE)) / 2, ty,
                t.textTitle, CHAR_SPACING, FONT_SCALE);
    ty += ROW_HEIGHT * 2;

    draw_cursor_cell(c, "SAVE", X + PAD, ty, s.presetMenuLine == 0, t.textValue, t);
    ty += ROW_HEIGHT;
    draw_cursor_cell(c, "LOAD", X + PAD, ty, s.presetMenuLine == 1, t.textValue, t);
}

}  // namespace pt::ui

// ── The MOD window ──────────────────────────────────────────────────────────────────────────────
// A on a GRAIN / ROAM / MOTION cell that has a target opens it. 3 or 4 lines edit one row of
// granular.mods: SOURCE (NONE / LFO / RANDOM / BUS), AMOUNT, then RATE + SHAPE (LFO), RATE (RANDOM)
// or BUS (BUS). Every edit is live: the dispatcher pushes the instrument after each one.

namespace pt::ui {

int moonset_granular_target(int col, int line) {
    if (col == 0) {
        switch (line) {
            case 1:  return pt::GT_POSITION;
            case 2:  return pt::GT_SIZE;
            case 3:  return pt::GT_DENSITY;
            case 4:  return pt::GT_SPRAY;
            case 5:  return pt::GT_PITCH;
            case 11: return pt::GT_STEREO;   // G PAN
            case 12: return pt::GT_JITTER;   // P JITTER
            case 13: return pt::GT_STRETCH;  // STRETCH
            default: return 0;
        }
    }
    if (col == 1) return line == 2 ? pt::GT_ROAM_RANGE : (line == 4 ? pt::GT_ROAM_SPEED : 0);
    if (col == 2) {
        switch (line) {
            case 1:  return pt::GT_JUMP;
            case 3:  return pt::GT_REV;
            case 5:  return pt::GT_CHAOS;
            default: return 0;
        }
    }
    return 0;
}

namespace {

int mod_line_count(int src) {
    switch (src) {
        case pt::GS_LFO:    return 4;
        case pt::GS_RANDOM: return 3;
        case pt::GS_BUS:    return 3;
        default:            return 1;
    }
}
// Extra line appended to every MOD window: SLICE FOLLOW toggle.
int mod_total_lines(int src) { return mod_line_count(src) + 1; }
int mod_slice_line(int src) { return mod_line_count(src); }

std::string mod_bus_name(int b) {
    b = std::clamp(b, 0, pt::GRAN_BUS_COUNT - 1);
    if (b < 8) return "TRACK " + std::to_string(b + 1);
    return std::string("MSG ") + static_cast<char>('A' + (b - 8));
}

}  // namespace

bool moonset_mod_open(songcore::Instrument::GranularParams& g, MoonsetState& s) {
    const int target = moonset_granular_target(s.granularColumn, s.granularRow);
    if (target == 0) return false;
    int slot = -1;
    for (int k = 0; k < pt::GRAN_MODS && slot < 0; ++k)
        if (g.mods[k].target == target) slot = k;                // this cell already has a row
    for (int k = 0; k < pt::GRAN_MODS && slot < 0; ++k)
        if (g.mods[k].src == pt::GS_NONE) slot = k;              // else the first one doing nothing
    if (slot < 0) return false;
    g.mods[slot].target = target;                                // src NONE: it still sounds like before
    s.modSlot = slot;
    s.modLine = 0;
    s.modOpen = true;
    return true;
}

void moonset_mod_move(const songcore::Instrument::GranularParams& g, MoonsetState& s, int dir) {
    const int n = mod_total_lines(g.mods[std::clamp(s.modSlot, 0, pt::GRAN_MODS - 1)].src);
    s.modLine = std::clamp(s.modLine + dir, 0, n - 1);
}

bool moonset_mod_edit(songcore::Instrument::GranularParams& g, MoonsetState& s, int delta, bool reset) {
    auto& m = g.mods[std::clamp(s.modSlot, 0, pt::GRAN_MODS - 1)];
    const auto before = m;
    auto wrap = [](int v, int n) { return ((v % n) + n) % n; };

    const int baseLines = mod_line_count(m.src);
    if (s.modLine == baseLines) {   // SLICE FOLLOW toggle line
        bool nv = s.sliceFollow;
        if (reset) nv = false;
        else if (delta > 0) nv = true;
        else if (delta < 0) nv = false;
        else nv = !nv;
        if (nv == s.sliceFollow) {
            s.modLine = std::min(s.modLine, mod_total_lines(m.src) - 1);
            return false;
        }
        s.sliceFollow = nv;
        s.modLine = std::min(s.modLine, mod_total_lines(m.src) - 1);
        return true;
    }

    switch (s.modLine) {
        case 0:
            m.src = reset ? pt::GS_NONE : wrap(m.src + (delta > 0 ? 1 : -1), pt::GS_COUNT);
            break;
        case 1:
            m.amount = reset ? 0x80 : std::clamp(m.amount + delta, 0, 255);
            break;
        case 2:
            if (m.src == pt::GS_BUS)
                m.bus = reset ? 0 : wrap(m.bus + (delta > 0 ? 1 : -1), pt::GRAN_BUS_COUNT);
            else
                m.rate = reset ? 0x80 : std::clamp(m.rate + delta, 0, 255);
            break;
        case 3:
            m.shape = reset ? 0 : wrap(m.shape + (delta > 0 ? 1 : -1), pt::GL_COUNT);
            break;
        default: break;
    }
    s.modLine = std::min(s.modLine, mod_total_lines(m.src) - 1);   // a shorter source drops the lines below
    return !(m == before);
}

void draw_moonset_mod_window(Canvas& c, const MoonsetState& s, const songcore::Instrument::GranularParams& g,
                             const Theme& t) {
    if (!s.modOpen || !s.granularFocus) return;
    const auto& m = g.mods[std::clamp(s.modSlot, 0, pt::GRAN_MODS - 1)];

    constexpr int W = 330, PAD = 14, LX = PAD, VX = 150, VIZ_H = 54;
    const int lines = mod_total_lines(m.src);
    const int H = PAD * 2 + ROW_HEIGHT * (lines + 2) + VIZ_H + 8;
    const int X = (DESIGN_W - W) / 2, Y = (DESIGN_H - H) / 2;

    c.reset_clip();
    draw_modal_backdrop(c);
    draw_modal_box(c, X, Y, W, H, t);

    static const char* const SRC[]   = {"NONE", "LFO", "RANDOM", "BUS"};
    static const char* const SHAPE[] = {"SINE", "TRI", "SAW", "SQUARE"};
    static const char* const COLS[]  = {"GRAIN", "ROAM", "MOTION"};
    const std::string title = std::string("MOD ") + COLS[std::clamp(s.granularColumn, 0, 2)] + " " +
                              label_for(s.granularColumn, s.granularRow);
    int ty = Y + PAD + TEXT_PADDING;
    c.draw_text(title, X + (W - Canvas::text_width(title, CHAR_SPACING, FONT_SCALE)) / 2, ty,
                t.textTitle, CHAR_SPACING, FONT_SCALE);
    ty += ROW_HEIGHT * 2;

    auto row = [&](int line, const char* label, const std::string& value) {
        const bool cur = (s.modLine == line);
        c.draw_text(label, X + LX, ty, cur ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, value, X + VX, ty, cur, t.textValue, t);
        ty += ROW_HEIGHT;
    };

    char b[16];
    row(0, "SOURCE", SRC[std::clamp(m.src, 0, pt::GS_COUNT - 1)]);
    if (m.src != pt::GS_NONE) {
        const int pct = (m.amount - 0x80) * 100 / 127;
        std::snprintf(b, sizeof b, "%+d%%", std::clamp(pct, -100, 100));
        row(1, "AMOUNT", b);
        if (m.src == pt::GS_BUS) {
            row(2, "BUS", mod_bus_name(m.bus));
        } else {
            std::snprintf(b, sizeof b, "%02X", m.rate & 0xFF);
            row(2, "RATE", b);
            if (m.src == pt::GS_LFO) row(3, "SHAPE", SHAPE[std::clamp(m.shape, 0, pt::GL_COUNT - 1)]);
        }
    }

    // ── Live visualiser of the modulation source ───────────────────────────────────────────────
    // SLICE FOLLOW toggle (MOD window on POSITION): follows slices sent by tracker notes.
    {
        const bool cur = (s.modLine == mod_slice_line(m.src));
        c.draw_text("SLICE", X + LX, ty, cur ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, s.sliceFollow ? "FOLLOW ON" : "FOLLOW OFF", X + VX, ty, cur, t.textValue, t);
        ty += ROW_HEIGHT;
    }

    const int vX = X + PAD;
    const int vY = ty + 4;
    const int vW = W - PAD * 2;
    c.fill_rect(vX, vY, vW, VIZ_H, t.vizBackground);
    // border
    c.fill_rect(vX, vY, vW, 1, t.textParam);
    c.fill_rect(vX, vY + VIZ_H - 1, vW, 1, t.textParam);
    c.fill_rect(vX, vY, 1, VIZ_H, t.textParam);
    c.fill_rect(vX + vW - 1, vY, 1, VIZ_H, t.textParam);

    auto lfoWave = [](int shape, float ph) {
        switch (shape) {
            case pt::GL_TRI: return 1.0f - 4.0f * std::fabs(ph - 0.5f);
            case pt::GL_SAW: return 2.0f * ph - 1.0f;
            case pt::GL_SQR: return ph < 0.5f ? 1.0f : -1.0f;
            default:         return std::sin(6.28318530718f * ph);
        }
    };

    const float midY = vY + VIZ_H * 0.5f;
    const float amp  = VIZ_H * 0.4f;

    if (m.src == pt::GS_NONE) {
        // Flat line: no modulation.
        c.fill_rect(vX + 1, static_cast<int>(midY), vW - 2, 1, t.textValue);
    } else if (m.src == pt::GS_BUS) {
        // BUS: live sidechain level of the selected track (red bar), not an LFO.
        const float level = std::clamp(s.modBusLevel, 0.0f, 1.0f);
        const int barH = VIZ_H - 8;
        const int barX = vX + 2;
        const int barY = vY + 4;
        c.fill_rect(barX, barY, vW - 4, barH, t.vizBackground);
        // Filled portion (level).
        const int fillW = (int)(level * (float)(vW - 4));
        if (fillW > 0) c.fill_rect(barX, barY, fillW, barH, t.textGranular);
        // Tick marks (mixer channels).
        for (int i = 1; i < 8; ++i) {
            const int tx = vX + (vW * i) / 8;
            c.fill_rect(tx, vY, 1, VIZ_H, t.textParam);
        }
        // Label.
        const std::string lbl = mod_bus_name(m.bus);
        c.draw_text(lbl, vX + 4, vY + VIZ_H - 12, t.textValue, CHAR_SPACING, FONT_SCALE);
    } else {
        const float rateByte = (m.src == pt::GS_BUS) ? 0.5f : (float)std::clamp(m.rate, 0, 255) / 255.0f;
        const float hz = 0.05f * std::pow(400.0f, rateByte);
        const float phase0 = (float)(s.nowMs % 100000) * 0.001f * hz;
        // Draw one or two cycles across the width.
        const int cycles = std::max(1, (int)std::round(hz * 1.0f));
        const int n = std::min(cycles, 6);
        std::vector<float> pts;
        pts.reserve((size_t)vW);
        for (int i = 0; i < vW; ++i) {
            float ph = phase0 + (float)i / (float)vW * (float)n;
            ph -= std::floor(ph);
            float v = 0.0f;
            if (m.src == pt::GS_LFO) {
                v = lfoWave(m.shape, ph);
            } else if (m.src == pt::GS_RANDOM) {
                // Square steps: quantise the phase into a few held values.
                const int step = (int)(ph * 8.0f);
                // deterministic-ish pseudo-random per step
                uint32_t r = (uint32_t)(step * 7919 + (int)(phase0 * 1000.0f));
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                v = ((float)(r >> 8) / 16777216.0f) * 2.0f - 1.0f;
            } else { // BUS
                v = (ph < 0.5f) ? 1.0f : -1.0f;
            }
            pts.push_back(v);
        }
        for (int i = 1; i < vW; ++i) {
            int y0 = (int)(midY - pts[(size_t)i - 1] * amp);
            int y1 = (int)(midY - pts[(size_t)i] * amp);
            // vertical segment
            int lo = std::min(y0, y1), hi = std::max(y0, y1);
            for (int yy = lo; yy <= hi; ++yy)
                if (yy >= vY && yy < vY + VIZ_H)
                    c.fill_rect(vX + i, yy, 1, 1, t.textGranular);
        }
        // Centre line
        c.fill_rect(vX + 1, static_cast<int>(midY), vW - 2, 1, t.textParam);
    }
}

// ─── The STRETCH page ────────────────────────────────────────────────────────────────────────────
// Raised by A on the GRAIN column's STRETCH cell. Unlike the TSTRETCH box of the SYNC FX (which bakes
// the sample), this one is a live performance page: ORIGIN / DURATION / NEW BPM set the target length,
// PITCH and GRAIN shape the cloud, MASTER is how much of that length the read head actually travels,
// and the LFO / RANDOM row below modulates that MASTER. Nothing is applied — every edit pushes the
// instrument, so a cloud that is already sounding follows at once.

namespace {

// ORIGIN, DURATION, NEW BPM, PITCH, GRAIN, MASTER — the modulation rows follow.
constexpr int STRETCH_PAGE_BASE = 6;
constexpr int STRETCH_PAGE_MOD  = STRETCH_PAGE_BASE;   // SOURCE, then AMOUNT / RATE or BUS / SHAPE

// The ratio byte is exponential: 0.25x at 0, 1x at 0x80, 4x at 0xFF (pt::gran_stretch_ratio).
int stretch_ratio_byte(double ratio) {
    ratio = std::clamp(ratio, 0.25, 4.0);
    const double b = std::log(ratio / 0.25) / std::log(16.0) * 255.0;
    return std::clamp(static_cast<int>(std::lround(b)), 0, 255);
}

std::string stretch_ratio_text(int byte) {
    const double r = pt::gran_stretch_ratio(byte);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2fX", r);
    return std::string(buf);
}

// The granular.mods row this page edits: the one already targeting STRETCH, else a row doing nothing,
// else −1 (the page still opens, without its modulation lines).
int stretch_page_find_slot(const songcore::Instrument::GranularParams& g) {
    for (int k = 0; k < pt::GRAN_MODS; ++k)
        if (g.mods[k].target == pt::GT_STRETCH) return k;
    for (int k = 0; k < pt::GRAN_MODS; ++k)
        if (g.mods[k].src == pt::GS_NONE && g.mods[k].target == 0) return k;
    return -1;
}

int stretch_page_line_count(const songcore::Instrument::GranularParams& g, const MoonsetState& s) {
    int n = STRETCH_PAGE_BASE;
    if (s.stretchModSlot < 0) return n;
    const auto& m = g.mods[std::clamp(s.stretchModSlot, 0, pt::GRAN_MODS - 1)];
    n += 1;                                            // SOURCE
    if (m.src != pt::GS_NONE) {
        n += 2;                                        // AMOUNT + RATE / BUS
        if (m.src == pt::GS_LFO) n += 1;               // SHAPE
    }
    return n;
}

// Changing ORIGIN moves DURATION onto the musical length the sample is closest to at that tempo.
void snap_duration_to_origin(MoonsetState& s, int origin_bpm) {
    if (origin_bpm <= 0 || s.sampleRate <= 0 || s.totalFrames <= 0) return;
    const double secs  = static_cast<double>(s.totalFrames) / s.sampleRate;
    const double beats = static_cast<double>(origin_bpm) * secs / 60.0;
    int    best  = s.durationIndex;
    double bestD = 1.0e9;
    const int count = static_cast<int>(MoonsetModule::duration_values().size());
    for (int i = 0; i < count; ++i) {
        const double d = std::fabs(MoonsetModule::duration_beats(i) - beats);
        if (d < bestD) { bestD = d; best = i; }
    }
    s.durationIndex = best;
}

// DURATION + NEW BPM -> the target length, against the sample's raw length.
void update_stretch_ratio(MoonsetState& s, songcore::Instrument::GranularParams& g, int song_tempo) {
    if (s.sampleRate <= 0 || s.totalFrames <= 0) return;
    const double raw = static_cast<double>(s.totalFrames) / s.sampleRate;
    const int    bpm = s.stretch_bpm(song_tempo);
    if (raw <= 0.0 || bpm <= 0) return;
    const double target = MoonsetModule::duration_beats(s.durationIndex) * 60.0 / bpm;
    g.grain.ratio = stretch_ratio_byte(target / raw);
}

}  // namespace

bool moonset_stretch_page_begin(MoonsetState& s, songcore::Instrument::GranularParams& g, int song_tempo) {
    if (s.totalFrames <= 0) return false;
    s.stretchModSlot  = stretch_page_find_slot(g);
    if (s.stretchModSlot >= 0 && g.mods[s.stretchModSlot].target != pt::GT_STRETCH)
        g.mods[s.stretchModSlot].target = pt::GT_STRETCH;   // still silent until a SOURCE is picked
    s.stretchPageLine = 0;
    s.stretchPageOpen = true;
    update_stretch_ratio(s, g, song_tempo);
    return true;
}

void moonset_stretch_page_move(const songcore::Instrument::GranularParams& g, MoonsetState& s, int dir) {
    const int n = stretch_page_line_count(g, s);
    if (n <= 0) return;
    s.stretchPageLine = ((s.stretchPageLine + dir) % n + n) % n;   // wraps, like the granular block
}

bool moonset_stretch_page_edit(songcore::Instrument::GranularParams& g, MoonsetState& s, int delta,
                               bool reset, int song_tempo) {
    const int line  = s.stretchPageLine;
    const int sign  = delta > 0 ? 1 : -1;
    auto wrap = [](int v, int n) { return ((v % n) + n) % n; };
    bool changed = false;

    if (line < STRETCH_PAGE_BASE) {
        switch (line) {
            case 0: {   // ORIGIN: 0 = automatic (the sample's own tempo)
                if (reset) {
                    s.stretchOriginBpm = 0;
                } else {
                    const int autoBpm = s.stretchOriginBpm > 0 ? s.stretchOriginBpm
                                                               : std::max(s.source_bpm(), song_tempo);
                    s.stretchOriginBpm = std::clamp(autoBpm + delta, 20, 999);
                }
                snap_duration_to_origin(s, s.stretchOriginBpm > 0 ? s.stretchOriginBpm : s.source_bpm());
                changed = true;
                break;
            }
            case 1:     // DURATION
                s.durationIndex = reset ? 2
                                        : wrap(s.durationIndex + sign,
                                               static_cast<int>(MoonsetModule::duration_values().size()));
                changed = true;
                break;
            case 2:     // NEW BPM: 0 = the song's tempo
                if (reset) {
                    s.stretchBpm = 0;
                } else {
                    const int bpm = std::clamp(s.stretch_bpm(song_tempo) + delta, 20, 999);
                    s.stretchBpm  = (bpm == song_tempo) ? 0 : bpm;   // follows the song when it matches
                }
                changed = true;
                break;
            case 3: {   // PITCH: the cloud's own pitch, +-24 semitones around 0x80
                const int before = g.grain.pitch;
                g.grain.pitch = reset ? 0x80 : std::clamp(g.grain.pitch + delta, 0, 255);
                changed = (g.grain.pitch != before);
                break;
            }
            case 4: {   // GRAIN: the length of one piece
                const int before = g.grain.size;
                g.grain.size = reset ? 0x80 : std::clamp(g.grain.size + delta, 0, 255);
                changed = (g.grain.size != before);
                break;
            }
            case 5: {   // MASTER: how much of the target length the head travels
                const int before = g.grain.stretch;
                g.grain.stretch = reset ? 0x00 : std::clamp(g.grain.stretch + delta, 0, 255);
                changed = (g.grain.stretch != before);
                break;
            }
            default: break;
        }
        if (line == 0 || line == 1 || line == 2) update_stretch_ratio(s, g, song_tempo);
        return changed;
    }

    // ── The modulation row on MASTER ────────────────────────────────────────────────────────────
    if (s.stretchModSlot < 0) return false;
    auto& m = g.mods[std::clamp(s.stretchModSlot, 0, pt::GRAN_MODS - 1)];
    const auto before = m;
    switch (line - STRETCH_PAGE_MOD) {
        case 0:
            m.src = reset ? pt::GS_NONE : wrap(m.src + sign, pt::GS_COUNT);
            break;
        case 1:
            m.amount = reset ? 0x80 : std::clamp(m.amount + delta, 0, 255);
            break;
        case 2:
            if (m.src == pt::GS_BUS)
                m.bus = reset ? 0 : wrap(m.bus + sign, pt::GRAN_BUS_COUNT);
            else
                m.rate = reset ? 0x80 : std::clamp(m.rate + delta, 0, 255);
            break;
        case 3:
            m.shape = reset ? 0 : wrap(m.shape + sign, pt::GL_COUNT);
            break;
        default: break;
    }
    // A shorter source drops the lines below, and the cursor with them.
    s.stretchPageLine = std::min(s.stretchPageLine, stretch_page_line_count(g, s) - 1);
    return !(m == before);
}

void draw_moonset_stretch_page(Canvas& c, const MoonsetState& s,
                               const songcore::Instrument::GranularParams& g, int song_tempo,
                               const Theme& t) {
    if (!s.stretchPageOpen) return;

    constexpr int W = 340, PAD = 14, LX = PAD, VX = 150;
    const int lines = stretch_page_line_count(g, s);
    const int H     = PAD * 2 + ROW_HEIGHT * (lines + 2);
    const int X     = (DESIGN_W - W) / 2, Y = (DESIGN_H - H) / 2;

    c.reset_clip();
    draw_modal_backdrop(c);
    draw_modal_box(c, X, Y, W, H, t);

    const std::string title = "STRETCH";
    int ty = Y + PAD + TEXT_PADDING;
    c.draw_text(title, X + (W - Canvas::text_width(title, CHAR_SPACING, FONT_SCALE)) / 2, ty,
                t.textTitle, CHAR_SPACING, FONT_SCALE);
    ty += ROW_HEIGHT * 2;

    auto row = [&](int line, const char* label, const std::string& value) {
        const bool cur = (s.stretchPageLine == line);
        c.draw_text(label, X + LX, ty, cur ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, value, X + VX, ty, cur, t.textValue, t);
        ty += ROW_HEIGHT;
    };

    static const char* const SRC[]   = {"NONE", "LFO", "RANDOM", "BUS"};
    static const char* const SHAPE[] = {"SINE", "TRI", "SAW", "SQUARE"};

    row(0, "ORIGIN", s.stretchOriginBpm > 0 ? pad3(s.stretchOriginBpm) : "AUTO");
    row(1, "DURATION", at(MoonsetModule::duration_values(), s.durationIndex));
    row(2, "NEW BPM", pad3(s.stretch_bpm(song_tempo)));
    row(3, "PITCH", fmt_semis(g.grain.pitch));
    row(4, "GRAIN", fmt_ms(g.grain.size));
    row(5, "MASTER", fmt_pct(g.grain.stretch) + " " + stretch_ratio_text(g.grain.ratio));

    if (s.stretchModSlot >= 0) {
        const auto& m = g.mods[std::clamp(s.stretchModSlot, 0, pt::GRAN_MODS - 1)];
        char b[16];
        row(STRETCH_PAGE_MOD, "SOURCE", SRC[std::clamp(m.src, 0, pt::GS_COUNT - 1)]);
        if (m.src != pt::GS_NONE) {
            const int pct = (m.amount - 0x80) * 100 / 127;
            std::snprintf(b, sizeof b, "%+d%%", std::clamp(pct, -100, 100));
            row(STRETCH_PAGE_MOD + 1, "AMOUNT", b);
            if (m.src == pt::GS_BUS) {
                row(STRETCH_PAGE_MOD + 2, "BUS", mod_bus_name(m.bus));
            } else {
                row(STRETCH_PAGE_MOD + 2, "RATE", hex2(m.rate));
                if (m.src == pt::GS_LFO)
                    row(STRETCH_PAGE_MOD + 3, "SHAPE", SHAPE[std::clamp(m.shape, 0, pt::GL_COUNT - 1)]);
            }
        }
    }
}

}  // namespace pt::ui
