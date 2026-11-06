#pragma once

// The sample editor: trim, slice, normalise, pitch to the grid, chop into one-shots. Full-screen
// (640×480): a waveform needs the width. All DSP is in the engine (`native/sample-editor.cpp`); this
// draws and walks the cursor.
//
// The row map is sparse: the cursor walks 1, 2, 8, 10, 11, 13, 14, 16, 18, 19 — rows 3–7 are the
// waveform, 9/12/15/17 spacers. Row 11 is unreachable with slicing OFF.
// ⚠️ On rows 3..8 the D-pad drags a selection edge (the dispatcher does it), so `cursor_context()`
// answers `none()` there.

#include <cstdint>
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "songcore/model.h"
#include "ui/canvas.h"
#include "ui/cursor.h"
#include "ui/theme.h"
#include "ui/modules/grain_preview.h"

namespace pt::ui {

// A slice boundary the user has moved or made.
// ⚠️ `originFrame` is its identity (the list is sorted by `frame`, so an index moves): A+B puts it
// back there; −1 = made by hand, so A+B removes it.
struct MoonsetSliceMarker {
    int frame       = 0;
    int originFrame = -1;
};

// The TSTRETCH box's cells in cursor order. PREVIEW and APPLY share the last line.
enum class MoonsetStretchCell { DURATION, BPM, PITCH, GRAIN, PREVIEW, APPLY };

// The editor's session state. The audio is in the engine (only the waveform's min/max pairs are
// here); name, path and slice markers reach the document only when saved.
struct MoonsetState {
    int sampleId     = 0;
    int instrumentId = 0;

    int cursorRow = 1;
    int cursorCol = 0;

    // ── The sample, as the engine reports it ─────────────────────────────────────────────────────
    std::string sampleName;
    std::string sampleFilePath;      // empty = none (the `sampleFilePath == null` convention)
    int         sampleRate  = 44100; // the FILE's rate, not the device's
    int         totalFrames = 0;

    // 620 (min, max) pairs, one per pixel column. Refilled by the feed on zoom/scroll.
    std::vector<float> waveformData;

    // ── Row 1: the view ──────────────────────────────────────────────────────────────────────────
    int  zoomLevel     = 0;      // 0 = 1×, 1 = 2×, … 4 = 16×
    // 0 = LEFT, 1 = RIGHT, 2 = STEREO, 3 = MONO. Opens on STEREO when there is a right channel —
    // only that mode saves both.
    int  sourceMode    = 0;
    int  rateMode      = 0;      // 0 = HIGH, 1 = NORM, 2 = LOFI  (a DESTRUCTIVE decimation)
    bool hasStereoData = false;  // the loaded sample has a right channel

    // ── Row 2: the edit controls ─────────────────────────────────────────────────────────────────
    // A pending shift in semitones; SAVE bakes it.
    int  pitchSemitones = 0;
    int  durationIndex  = 2;     // "1 BAR" — the target SYNC stretches/pitches to
    // BIT: the depth the sample came in at, and the depth now (never above it). A destructive
    // requantise, and the depth SAVE writes. ⚠️ Not the instrument's CRUSH.
    int  sourceBitDepth = 16;
    int  bitDepth       = 16;
    // Snap a dragged edge to a zero crossing. ⚠️ Has no cell (BIT took its place), so always on.
    bool snapEnabled    = true;

    // ── The selection, in FRAMES (not the 0..255 the instrument stores) ──────────────────────────
    int64_t selectionStart = 0;
    int64_t selectionEnd   = 0;

    // ── Slicing ──────────────────────────────────────────────────────────────────────────────────
    int sliceMethod      = 2;   // 0 = TRANSIENT, 1 = DIVIDE, 2 = OFF, 3 = MANUAL
    int sliceSensitivity = 64;  // TRANSIENT: the detector's threshold
    int sliceDivisions   = 8;   // DIVIDE: cut into N equal parts
    // Row 11: which slice, under every method. ⚠️ Slice 00's left edge is the sample start, not a
    // marker (`slice_marker_index()`). Under MANUAL it reaches one past the last slice: the next one.
    int sliceIndex       = 0;
    bool sliceFollow     = false;   // GRAIN/SLICE: follow the slice played by the tracker
    // The instrument's `slicingMode` as the dispatcher last pushed it: the engine reads slices only
    // then, so the grain columns follow the slice only when it is true. Hash of the pushed state.
    bool     sliceReading = false;
    uint32_t sliceSyncHash = 0;

    // ⚠️ Kept for UNDO and the two SYNC ops; the slice rows read `effective_slice_position()`.
    int64_t slicePosition = 0;

    // The file's own markers (its `cue ` chunk), cleared by a length-changing op. What SLICE = OFF
    // shows and saves back, and where MANUAL starts. Sorted, unique, strictly inside the sample.
    std::vector<int> fileMarkers;

    // The detector's output, TRANSIENT only: N markers → N+1 slices.
    // ⚠️ Empty is the trigger: the feed re-runs the detector when it finds TRANSIENT with none.
    std::vector<int> transientMarkers;
    // (sensitivity, length) the detector last ran for AND found nothing: an empty list is the trigger to
    // detect, so a sample with no transient must not re-detect on every button press. -1 = unknown.
    int64_t transientKey = -1;

    // The boundaries the user placed or moved, sorted, strictly increasing; they override the
    // method's. ⚠️ Live only while the stamp below matches (`manual_markers_live()`); empty is live.
    std::vector<MoonsetSliceMarker> manualMarkers;

    // The (method, parameter) `manualMarkers` was made under; a change makes them stale.
    int manualKeyMethod = -1;
    int manualKeyParam  = -1;

    // ── Row 16: the FX row ───────────────────────────────────────────────────────────────────────
    int fxType   = 0;   // 0 = OTT, 1 = DUST, 2 = DRIVE, 3 = EQ, 4 = SYNC
    int fxValue  = 0;   // OTT / DUST / DRIVE: the amount. 0 = nothing to audition or apply
    int fxEqSlot = -1;  // EQ: the slot in the 128-preset bank. −1 = none, drawn "--"
    int syncType = 0;   // when fxType == SYNC: 0 = RPITCH (resample), 1 = TSTRETCH (SOLA)

    // ── The TSTRETCH box (A on the TSTRETCH cell) ────────────────────────────────────────────────
    // Session settings: APPLY on the FX row uses them whether or not the box is up.
    bool        stretchOpen    = false;
    MoonsetStretchCell stretchCursor  = MoonsetStretchCell::DURATION;
    int         stretchBpm     = 0;    // the tempo to fit to. ⚠️ 0 = the song's, read when it is needed
    int         stretchGrainMs = 40;   // the length of each piece, in milliseconds (sola-stretch.h)

    // ── The STRETCH page (A on the GRAIN column's STRETCH cell) ─────────────────────────────────
    // A live page: ORIGIN / DURATION / NEW BPM / PITCH / GRAIN / MASTER plus the LFO-RANDOM row that
    // modulates MASTER. Nothing is applied: every edit pushes the instrument and a sounding cloud
    // follows at once.
    bool stretchPageOpen = false;
    int  stretchPageLine = 0;
    int  stretchModSlot  = -1;   // the granular.mods row it edits; -1 = every row is taken
    int  stretchOriginBpm = 0;   // 0 = automatic (the sample's own tempo, source_bpm())

    // ── Flags ────────────────────────────────────────────────────────────────────────────────────
    // An unsaved destructive edit is in the engine's buffer. B asks before dropping it.
    bool isModified       = false;
    bool showConfirmClose = false;

    // ── Record ───────────────────────────────────────────────────────────────────────────────────
    // The editor is recording input audio into a new sample. REC draws lit; INPUT picks the device.
    bool recordActive = false;
    bool recordFailed  = false;   // last record attempt failed (shown in the REC cell)
    float recordLevel  = 0.0f;    // live input peak (0..1) while recording
    float recordPan   = 0.0f;    // stereo pan of input (-1..1)
    float recordBass  = 0.0f;    // bass energy (0..1)
    float recordHigh  = 0.0f;    // high-freq energy (0..1)
    int   recordFrames = 0;       // frames captured so far
    int   recordSkipFrames = 0;   // frames of the count-in, dropped from the take (0 = DIRECT)
    int   lastRecordLength = 0;   // for accumulating viz
    std::vector<float> recWaveL;   // live waveform (min/max pairs) L
    std::vector<float> recWaveR;   // live waveform (min/max pairs) R
    std::vector<float> recThick;   // per-bin thickness (0..1)
    std::string recordInputDevice;   // selected input device name ("" = none/OFF)
    std::vector<std::string> audioInputDevices;   // input device names for the INPUT cell

    // ── Topographic record visualizer (courbes de niveau L/R) ────────────────────────────────────
    // Grille dB : [bande][colonne], colonne la plus récente à droite. -100 = silence.
    // Colonnes glissent vers la gauche à chaque tick d'analyse, nouveau son apparaît à droite.
    int   recVizBands     = 40;   // bandes fréquences log (200 Hz → 5 kHz)
    int   recVizCols      = 100;  // colonnes temps (≈ 30 s d'historique)
    float recVizHistory   = 30.0f;// s d'historique total
    float recVizTimePow   = 2.5f; // compression du temps (plus grand = passé plus compressé)
    float recVizFade      = 3.0f; // dB d'estompage par frame
    float recVizFmin      = 200.0f;
    float recVizFmax      = 5000.0f;
    int   recVizFftSize   = 1024; // taille FFT (puissance de 2)
    int   recVizAnalyzeEvery = 3; // frames entre deux analyses
    std::array<float, 4> recVizThresh = {{-20.0f, -40.0f, -60.0f, -80.0f}}; // seuils dB
    int   recVizFrameCounter = 0; // compteur pour le throttling d'analyse
    bool  recVizDirty     = true; // grille modifiée, redraw requis
    std::vector<float> recSpecL;  // bands*cols dB, canal L
    std::vector<float> recSpecR;  // bands*cols dB, canal R

    // 0..1 while the sample sounds, −1 when not: the playhead line.
    float playbackPosition = -1.0f;

    // Boucle de pré-écoute (cellule LOOP, rangée SELECTION) : le sample boucle jusqu'à ce qu'on rappuie sur X.
    bool loopPreview = false;

    // ── The granular block, under the waveform (MOONGRANULAR only) ────────────────────────────────
    // ⚠️ SHIFT swaps the LOWER HALF only, between the sample edit rows (SELECTION … LOAD) and
    // GRAIN / ROAM / MOTION. The four header rows and the waveform (red playhead included) stay.
    // `granularFocus` says which one the cursor is in; SHIFT toggles it.
    bool granularFocus  = false;
    int  granularColumn = 0;   // 0 = GRAIN, 1 = ROAM, 2 = MOTION
    int  granularRow    = 0;
    int  granularTop    = 0;   // kept for the shared helper: every row fits in the panel
    // The MOD window (A on a cell that can be modulated): which of the 8 mod rows it edits, and its cursor.
    // Live grains: where each one reads, 0..1 of the sample. Refreshed every frame by the dispatcher.
    static constexpr int GRAIN_HEADS_MAX = 64;
    float grainHeads[GRAIN_HEADS_MAX] = {};
    int   grainHeadCount = 0;
    // Animation of the red lines: the dispatcher stamps the frame clock, the draw advances the simulation.
    long long nowMs = 0;
    mutable GrainPreview preview;
    bool modOpen = false;
    int  modSlot = 0;    // 0..7, a row of Instrument::granular.mods
    int  modLine = 0;    // 0 SOURCE, 1 AMOUNT, 2 RATE or BUS, 3 SHAPE (LFO only)
    // Live sidechain level for the BUS visualiser (0..1).
    float modBusLevel = 0.0f;

    static constexpr int GRANULAR_VISIBLE = 17;   // ROAM's depth: all three columns fit full-page


    // ── Derived ──────────────────────────────────────────────────────────────────────────────────

    // The parameter `manualMarkers` is keyed to: SENS under TRANSIENT, BY under DIVIDE.
    int manual_key_param() const;

    // The hand-placed markers still belong to what is on screen. ⚠️ The stamp alone: empty can be live.
    bool manual_markers_live() const;

    // The method's own markers: the file's under OFF/MANUAL, the detector's under TRANSIENT, none
    // under DIVIDE. ⚠️ Read through `marker_count()`/`marker_position()`, where hand-placed ones win.
    const std::vector<int>& method_markers() const;

    // How many boundaries are in force. ⚠️ Every reader counts through here.
    int marker_count() const;

    // Boundary `k`'s frame; an unplaced one sits on its predecessor (or frame 0). Not clamped.
    int64_t marker_position(int k) const;

    // The marker at the left edge of slice `sliceIndex`. −1 on slice 00; one past the list on
    // MANUAL's free slot.
    int slice_marker_index() const;

    // Row 11's top index: N markers → N, MANUAL one further.
    int slice_index_ceiling() const;

    // (start, end) of slice `idx`. OFF = the whole sample.
    void slice_bounds(int idx, int64_t& start, int64_t& end) const;

    // Row 11's position: the start of the slice under the cursor.
    int64_t effective_slice_position() const;

    // The visible frame window: the whole sample at zoom 0, else `totalFrames >> zoom` centred on
    // what the cursor points at. A playhead takes the centre only while the selection does not fit.
    int64_t view_start() const;
    int64_t view_end() const;

    // "MM:SS.CC", scaled by the pending pitch — what SAVE will bake.
    std::string duration_display() const;

    // The tempo the sample is if it holds DURATION's bar count; 0 outside 1..999. Scaled by the
    // pending pitch, as `duration_display` is.
    int source_bpm() const;

    // "NNNBPM", "---BPM" when none.
    std::string bpm_display() const;

    // TSTRETCH's tempo: the box's BPM, or the song's while unmoved.
    int stretch_bpm(int song_tempo) const { return stretchBpm > 0 ? stretchBpm : song_tempo; }

    // DURATION, BPM, PITCH, GRAIN back to their opening values.
    void reset_stretch();

    // OTT / DUST / DRIVE has an amount, or EQ has a slot.
    bool fx_armed() const;

    // What the engine takes beside `fxType`: the amount, or the EQ slot.
    int fx_param() const;
};

// ── The TSTRETCH box: a modal over the lower half ───────────────────────────────────────────────

// `d_row` walks the column, `d_col` crosses between PREVIEW and APPLY.
MoonsetStretchCell moonset_stretch_step(MoonsetStretchCell from, int d_row, int d_col);

CursorContext moonset_stretch_cursor_context(const MoonsetState& s, int song_tempo);

void moonset_stretch_handle_input(MoonsetState& s, const InputAction& action, int song_tempo);

// No-op when the box is not open.
void draw_moonset_stretch_editor(Canvas& c, const MoonsetState& s, int song_tempo, const Theme& t);

// ── The STRETCH page ───────────────────────────────────────────────────────────────────────────
// A on the GRAIN column's STRETCH cell. `begin` finds (or makes) the granular.mods row that targets
// GT_STRETCH and computes the ratio from DURATION + NEW BPM; `edit` changes the line under the
// cursor; both are live (the dispatcher pushes the instrument). `move` walks the lines that exist.
bool moonset_stretch_page_begin(MoonsetState& s, songcore::Instrument::GranularParams& g, int song_tempo);
void moonset_stretch_page_move(const songcore::Instrument::GranularParams& g, MoonsetState& s, int dir);
bool moonset_stretch_page_edit(songcore::Instrument::GranularParams& g, MoonsetState& s, int delta,
                               bool reset, int song_tempo);
void draw_moonset_stretch_page(Canvas& c, const MoonsetState& s,
                               const songcore::Instrument::GranularParams& g, int song_tempo,
                               const Theme& t);

// ── The MOD window of the granular cells ────────────────────────────────────────────────────────
// GranTarget (granular-voice.h) a cell can be modulated through; 0 = this cell has none.
int  moonset_granular_target(int col, int line);
// Opens the window on the cell's mod row (its own row, else a free one). False: no target, or all 8 rows used.
bool moonset_mod_open(songcore::Instrument::GranularParams& g, MoonsetState& s);
// Cursor up/down over the lines that exist for the row's source.
void moonset_mod_move(const songcore::Instrument::GranularParams& g, MoonsetState& s, int dir);
// Edits the line under the cursor by `delta` (A+LEFT/RIGHT = 1, A+UP/DOWN = 16). `reset`: A+B. True if a byte changed.
bool moonset_mod_edit(songcore::Instrument::GranularParams& g, MoonsetState& s, int delta, bool reset);
void draw_moonset_mod_window(Canvas& c, const MoonsetState& s, const songcore::Instrument::GranularParams& g, const Theme& t);

// What `handle_input` changed, so the dispatcher knows which engine calls to make.
struct MoonsetInputResult {
    bool modified = false;
    // RATE and BIT are destructive: the dispatcher rebuilds the audio from the cached original.
    bool rateModeChanged = false;
    bool bitDepthChanged = false;
};

class MoonsetModule {
public:
    static constexpr int WIDTH  = 640;
    static constexpr int HEIGHT = 480;   // full-screen: no top strip, no right bar

    // The waveform panel, under the three header rows.
    static constexpr int WAVEFORM_Y = 85;
    static constexpr int WAVEFORM_H = 155;
    static constexpr int WAVEFORM_W = 620;

    // Rows 8..19 sit under the waveform.
    static int content_y(int row);

    // The granular panel's title line, in the lower half (under the waveform, y=240).
    static constexpr int GRANULAR_Y = 243;

    static const std::vector<std::string>& source_values();    // LEFT / RIGHT / STEREO / MONO
    static const std::vector<std::string>& rate_values();      // HIGH / NORM / LOFI
    // That depth and every standard one below it, highest first. No 12: WAV has none.
    static std::vector<std::string> bit_depth_choices(int source_bits);
    static const std::vector<std::string>& duration_values();  // 4 BAR … 1/32

    // Beats per DURATION index: "1 BAR" = 4, halving each step.
    static double duration_beats(int duration_index);
    static const std::vector<std::string>& fx_types();         // OTT / DUST / DRIVE / EQ / SYNC
    static const std::vector<std::string>& sync_types();       // RPITCH / TSTRETCH
    static const std::vector<std::string>& slice_methods();    // TRANSIENT / DIVIDE / OFF / MANUAL
    static const std::vector<std::string>& ops_row1();         // CROP COPY CUT DUPL PASTE DEL
    static const std::vector<std::string>& ops_row2();         // NORM FADE+ FADE- SLNC REV UNDO

    // `fxValue` means something different under each.
    static constexpr int FX_OTT   = 0;
    static constexpr int FX_DUST  = 1;
    static constexpr int FX_DRIVE = 2;
    static constexpr int FX_EQ    = 3;   // reads `fxEqSlot`, not the amount
    static constexpr int FX_SYNC  = 4;   // fxValue is unused; syncType picks RPITCH vs TSTRETCH

    static constexpr int SYNC_RPITCH   = 0;   // resample: faster is higher
    static constexpr int SYNC_TSTRETCH = 1;   // the pitch held; A on the cell opens its box

    static constexpr int SLICE_TRANSIENT = 0;
    static constexpr int SLICE_DIVIDE    = 1;
    static constexpr int SLICE_OFF       = 2;
    // ⚠️ Appended after OFF: `sliceMethod` survives a session.
    static constexpr int SLICE_MANUAL    = 3;

    // Row 10's second cell: SENS (TRANSIENT) or BY (DIVIDE).
    static constexpr bool slice_has_parameter(int slice_method) {
        return slice_method == SLICE_TRANSIENT || slice_method == SLICE_DIVIDE;
    }

    // Rows 3–7 and the spacers are skipped, and row 11 too with slicing OFF; row 1 and 19 wrap.
    static int row_above(int row, int slice_method = -1);
    static int row_below(int row, int slice_method = -1);
    static int max_col_for_row(int row, int slice_method = SLICE_OFF);


    void draw(Canvas& c, int x, int y, const MoonsetState& s,
              const songcore::Instrument::GranularParams& g, const Theme& t) const;

    CursorContext cursor_context(const MoonsetState& s) const;

    MoonsetInputResult handle_input(MoonsetState& s, const InputAction& action) const;

private:
    // The wave, the S/E edges, slice boundaries and the playhead.
    void draw_waveform(Canvas& c, int x, int y, const MoonsetState& s, const Theme& t,
                       const songcore::Instrument::GranularParams* g = nullptr) const;

    // Topographic record visualizer: L/R contour panels (replaces the record waveform while recording).
    void draw_rec_viz(Canvas& c, int x, int y, const MoonsetState& s, const Theme& t) const;

    // Marching squares: extract contour segments for one threshold from a bands×cols grid.
    // `grid` indexed [band*cols + col], value in dB. Segments emitted in logical (col, band) space.
    struct VizSeg { float x0, y0, x1, y1; };
    static void marching_squares(const std::vector<float>& grid, int bands, int cols,
                                 float threshold, std::vector<VizSeg>& out);

    // Draw a thin line between two points using 1px fills (canvas has no line primitive).
    static void draw_thin_line(Canvas& c, float x0, float y0, float x1, float y1, Argb color);

    // Time → x with logarithmic compression of the past (now at right, past compressed to left).
    static float viz_tx(int col, int cols, float historySec, float timePow, float left, float width);

    // B on a modified sample.
    void draw_confirm_dialog(Canvas& c, int x, int y, const Theme& t) const;
};

// ── The granular block (GRAIN / ROAM / MOTION) ──────────────────────────────────────────────────
// The parameters are stored as raw 0..255 bytes, like the engine's; the page shows physical units.
// DENSITY and SPEED become divisions while GRAIN TIME = BPM (the model keeps a separate div index).

int  moonset_granular_row_count(int col);
std::string moonset_granular_value(const songcore::Instrument::GranularParams& g, int col, int line, bool sliceReading = false);
CursorContext moonset_granular_context(const songcore::Instrument::GranularParams& g, int col, int line, bool sliceReading = false);
bool moonset_granular_apply(songcore::Instrument::GranularParams& g, int col, int line, int value);

// `gy` is the top of the block's title; `top` is the first visible row.
void draw_moonset_granular(Canvas& c, int x, int gy,
                           const songcore::Instrument::GranularParams& g,
                           int cursorLine, int cursorCol, int top, const Theme& t,
                           bool sliceReading = false,
                           const MoonsetState* s = nullptr);

}  // namespace pt::ui
