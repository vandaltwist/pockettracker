// The sample editor: its buttons, the selection, the slice markers, the ops, SAVE and CHOP.

#include "ui/dispatch/dispatch_common.h"
#include "ui/modules/rec_viz.h"
#include "audio-engine.h"

#include "ui/std_filesystem.h"
#include "ui/modules/rec_viz.h"

#include <algorithm>
#include "ui/modules/rec_viz.h"
#include <array>
#include "ui/modules/rec_viz.h"
#include <cmath>
#include "ui/modules/rec_viz.h"
#include <cstring>
#include "ui/modules/rec_viz.h"
#include <string>
#include "ui/modules/rec_viz.h"
#include <vector>
#include "ui/modules/rec_viz.h"

#include "kissfft/kiss_fftr.h"
#include "ui/modules/rec_viz.h"

namespace pt::ui {

namespace {

// Runs `f` when the scope ends, whichever `return` leaves it.
template <class F> struct MoonsetScopeExit {
    F f;
    ~MoonsetScopeExit() { f(); }
};
template <class F> MoonsetScopeExit(F) -> MoonsetScopeExit<F>;

// TRANSIENT with an empty list = "run the detector". The sample editor's per-frame feed did this;
// MOONSET had no such feed, so the markers never came back after a method / sensitivity change.
template <class Host, class State>
void moonset_ensure_transients(Host& host, State& se) {
    if (se.sliceMethod != MoonsetModule::SLICE_TRANSIENT || !se.transientMarkers.empty()) return;
    if (se.totalFrames < 4096) return;
    const int64_t key = static_cast<int64_t>(se.sliceSensitivity) * 1000000007LL + static_cast<int64_t>(se.totalFrames);
    if (key == se.transientKey) return;   // already ran for this and found nothing
    se.transientMarkers = host.detect_transients(se.instrumentId, se.sliceSensitivity);
    se.transientKey     = se.transientMarkers.empty() ? key : -1;
    const int ceiling = se.slice_index_ceiling();
    if (se.sliceIndex > ceiling) se.sliceIndex = ceiling;
}


/** The waveform panel's width: one (min, max) pair per pixel. */
constexpr int WAVEFORM_BINS = MoonsetModule::WAVEFORM_W;

/** SOURCE mode ? the channel the waveform is drawn from: 0 = left, 1 = right, 2 = averaged. */
int waveform_channel(int source_mode) {
    return (source_mode == 0) ? 0 : (source_mode == 1) ? 1 : 2;
}

/** A+LEFT/RIGHT nudge by about one visible pixel, A+UP/DOWN by sixteen - both scaled by the zoom. */
int64_t fine_step(const MoonsetState& se) {
    return std::max<int64_t>(1, static_cast<int64_t>(se.totalFrames) / (256LL << se.zoomLevel));
}
int64_t coarse_step(const MoonsetState& se) {
    return std::max<int64_t>(1, static_cast<int64_t>(se.totalFrames) / (16LL << se.zoomLevel));
}

/** The selection and the slice position, moved with audio that went from `oldLen` frames to `newLen`. */
void rescale_positions(MoonsetState& se, int oldLen, int newLen) {
    auto scale = [&](int64_t f) -> int64_t {
        return oldLen > 0 ? std::clamp<int64_t>((f * newLen) / oldLen, 0, newLen) : 0;
    };
    se.selectionStart = scale(se.selectionStart);
    se.selectionEnd   = scale(se.selectionEnd);
    se.slicePosition  = scale(se.slicePosition);
}

/** DIVIDE's boundaries: `divisions` equal slices. */
std::vector<int> divide_points(int totalFrames, int divisions) {
    const int        div = std::max(divisions, 1);
    std::vector<int> out;
    for (int i = 1; i < div; ++i)
        out.push_back(static_cast<int>((static_cast<int64_t>(i) * totalFrames) / div));
    return out;
}

/** Hann-windowed real FFT ? log-spaced band magnitudes in dB (floor -100).
 *  `out` receives `bands` values, band b covers [f0, f1] log-spaced between fmin/fmax. */
void compute_log_bands(const float* buf, int len, int sampleRate, int fftSize,
                       float fmin, float fmax, int bands, std::vector<float>& out) {
    out.assign(static_cast<size_t>(bands), -100.0f);
    if (len <= 0 || buf == nullptr || fftSize < 8) return;

    // Window length = min(len, fftSize); pad with zeros if len < fftSize.
    const int n = std::min(len, fftSize);
    std::vector<float> window(fftSize, 0.0f);
    for (int i = 0; i < n; ++i) {
        float w = 0.5f * (1.0f - cosf(2.0f * static_cast<float>(M_PI) * i /
                                           static_cast<float>(std::max(n - 1, 1))));
        window[i] = buf[i] * w;
    }

    kiss_fftr_cfg cfg = kiss_fftr_alloc(fftSize, 0, nullptr, nullptr);
    if (!cfg) return;
    std::vector<kiss_fft_cpx> spec(fftSize / 2 + 1);
    kiss_fftr(cfg, window.data(), spec.data());
    kiss_fftr_free(cfg);

    const float nyq = static_cast<float>(sampleRate) * 0.5f;
    const float norm = 1.0f / static_cast<float>(fftSize);
    const int   nBins = fftSize / 2;

    // bin ? frequency
    auto bin_freq = [&](int k) { return static_cast<float>(k) * nyq / static_cast<float>(nBins); };

    for (int b = 0; b < bands; ++b) {
        float f0 = fmin * std::pow(fmax / fmin, static_cast<float>(b) / static_cast<float>(bands));
        float f1 = fmin * std::pow(fmax / fmin, static_cast<float>(b + 1) / static_cast<float>(bands));
        int   i0 = std::max(0, static_cast<int>(std::floor(f0 / nyq * static_cast<float>(nBins))));
        int   i1 = std::min(nBins - 1, static_cast<int>(std::floor(f1 / nyq * static_cast<float>(nBins))));
        if (i1 <= i0) i1 = i0 + 1;
        float best = 0.0f;
        for (int k = i0; k <= i1; ++k) {
            float re = spec[static_cast<size_t>(k)].r * norm;
            float im = spec[static_cast<size_t>(k)].i * norm;
            float m = sqrtf(re * re + im * im);
            if (m > best) best = m;
        }
        float db = (best > 0.0f) ? 20.0f * log10f(best) : -100.0f;
        out[static_cast<size_t>(b)] = std::max(-100.0f, std::min(0.0f, db));
    }
}

}  // namespace

// ??? The buttons ?????????????????????????????????????????????????????????????????????????????????

GestureResult InputDispatcher::moonset_screen(Gesture g) {
    MoonsetState& se = s_.moonset;

    // What the waveform shows depends on the zoom, the cursor, the selection and the slice: whatever this
    // button changes, the min/max pairs are re-read for the new window (the sample editor's feed did it;
    // MOONSET had none, so ZOOM changed the window but not the picture). TRANSIENT markers likewise.
    const int64_t viewStart0 = se.view_start(), viewEnd0 = se.view_end();
    const int     source0    = se.sourceMode;
    MoonsetScopeExit syncView{[&] {
        if (s_.currentScreen != ScreenType::MOONSET) return;   // B closed the editor
        MoonsetState& m = s_.moonset;
        moonset_ensure_transients(host_, m);
        if (m.totalFrames > 0 &&
            (m.view_start() != viewStart0 || m.view_end() != viewEnd0 || m.sourceMode != source0))
            refresh_moonset_view(/*reset_selection=*/false);
    }};

    // The granular block owns A and the D-pad while it has the cursor: the sample rows below it
    // (row 19 in particular) must not also react. B, X and Y keep their meaning.
    if (se.granularFocus) {
        switch (g) {
            case Gesture::B:
                if (pt::ui::rec_viz().menuOpen()) { pt::ui::rec_viz().closeMenu(); return GestureResult::TAKEN; }
                if (se.isModified) se.showConfirmClose = true;
                else               close_moonset();
                return GestureResult::TAKEN;
            case Gesture::X:
                if (!se.recordActive) { moonset_audition(); return GestureResult::TAKEN; }
                return GestureResult::PASS;
            case Gesture::Y:
                se.loopPreview = !se.loopPreview;
                set_status(s_, se.loopPreview ? "LOOP: ON" : "LOOP: OFF", true);
                if (se.playbackPosition >= 0.0f) {
                    host_.stop_preview();
                    se.playbackPosition = -1.0f;
                }
                return GestureResult::TAKEN;
            default:
                return GestureResult::PASS;   // A / A+DPAD / DPAD edit the granular cell
        }
    }

    // The INPUT cell (row 5, cols 0..1) must show what is plugged in NOW: a USB mic connected after the
    // editor opened is missing from the list read at open, and picking it would silently record the mic.
    if (se.cursorRow == 5 && (se.cursorCol == 0 || se.cursorCol == 1)) refresh_moonset_input_devices();

    // A+DPAD drags the selection's active edge on rows 3..8, the slice boundary on row 11's POSITION
    // cell; elsewhere it edits the cell like any screen.
    auto nudge = [&](int64_t delta) {
        if (on_moonset_selection_row())        nudge_moonset_selection_edge(delta);
        else if (on_moonset_slice_marker_row()) nudge_moonset_slice_marker(delta);
        else return GestureResult::PASS;
        return GestureResult::TAKEN;
    };

    switch (g) {
        // ?? A cell that opens something (EQ) goes first, or its FX APPLY would run.
        case Gesture::A:
            // Row 5 is RECORD: A on REC toggles recording, A on INPUT cycles devices,
            // A on SET opens/closes the viz settings menu.
            if (se.cursorRow == 5) {
                if (se.cursorCol == 0) handle_moonset_record_toggle();
                else if (se.cursorCol == 1) generic_input(pt::ui::increment);
                else if (se.cursorCol == 2) { pt::ui::rec_viz().toggleMenu(); }
                else if (se.cursorCol == 3) {
                    se.loopPreview = !se.loopPreview;
                    set_status(s_, se.loopPreview ? "LOOP: ON" : "LOOP: OFF", true);
                    if (se.playbackPosition >= 0.0f) {
                        host_.stop_preview();
                        se.playbackPosition = -1.0f;
                    }
                }
                return GestureResult::TAKEN;
            }
            // Close the viz menu if it is open and A is pressed elsewhere.
            if (pt::ui::rec_viz().menuOpen()) { pt::ui::rec_viz().closeMenu(); return GestureResult::TAKEN; }
            if (!open_sub_screen_at_cursor(/*peek=*/false)) moonset_confirm();
            return GestureResult::TAKEN;

        case Gesture::B:
            if (pt::ui::rec_viz().menuOpen()) { pt::ui::rec_viz().closeMenu(); return GestureResult::TAKEN; }
            if (se.isModified) se.showConfirmClose = true;
            else               close_moonset();
            return GestureResult::TAKEN;

        // X is the transport (PLAY): on the sample editor it auditions, exactly as START did.
        case Gesture::X:
            if (!se.recordActive) {
                moonset_audition();
                return GestureResult::TAKEN;
            }
            return GestureResult::PASS;

        case Gesture::Y:
            // Boucle de pr�-�coute : rejoue le sample en boucle jusqu'� ce qu'on rappuie sur X.
            se.loopPreview = !se.loopPreview;
            set_status(s_, se.loopPreview ? "LOOP: ON" : "LOOP: OFF", true);
            if (se.playbackPosition >= 0.0f) {
                host_.stop_preview();
                se.playbackPosition = -1.0f;
            }
            return GestureResult::TAKEN;


        case Gesture::A_UP:
            if (se.cursorRow == 5 && se.cursorCol == 0) return GestureResult::TAKEN;
            return nudge(+coarse_step(se));
        case Gesture::A_DOWN:
            if (se.cursorRow == 5 && se.cursorCol == 0) return GestureResult::TAKEN;
            return nudge(-coarse_step(se));
        case Gesture::A_LEFT:
            if (se.cursorRow == 5 && se.cursorCol != 0) {
                generic_input(pt::ui::decrement);
                return GestureResult::TAKEN;
            }
            if (se.cursorRow == 5 && se.cursorCol == 0) return GestureResult::TAKEN;
            return nudge(-fine_step(se));
        case Gesture::A_RIGHT:
            if (se.cursorRow == 5 && se.cursorCol != 0) {
                generic_input(pt::ui::increment);
                return GestureResult::TAKEN;
            }
            if (se.cursorRow == 5 && se.cursorCol == 0) return GestureResult::TAKEN;
            return nudge(+fine_step(se));

        case Gesture::A_B:
            // The SELECTION row: back to the sample's own bound - START to 0, END to the last frame.
            if (se.cursorRow == 8) {
                if (se.cursorCol == 0)      se.selectionStart = 0;
                else if (se.cursorCol == 1) se.selectionEnd   = se.totalFrames;
                return GestureResult::TAKEN;
            }
            // The SLICE DETAIL row: the boundary back where its method would put it; under MANUAL, gone.
            if (se.cursorRow == 11) {
                reset_moonset_slice_marker();
                return GestureResult::TAKEN;
            }
            // The TSTRETCH cell: everything its box dials, back as a session opens.
            if (se.cursorRow == 16 && se.cursorCol == 1 && se.fxType == MoonsetModule::FX_SYNC &&
                se.syncType == MoonsetModule::SYNC_TSTRETCH) {
                se.reset_stretch();
                return GestureResult::TAKEN;
            }
            return GestureResult::PASS;

        // When the viz settings menu is open, the D-pad drives it instead of the editor cursor.
        case Gesture::DPAD_UP:
            if (pt::ui::rec_viz().menuOpen()) { pt::ui::rec_viz().menuMove(-1); return GestureResult::TAKEN; }
            return GestureResult::PASS;
        case Gesture::DPAD_DOWN:
            if (pt::ui::rec_viz().menuOpen()) { pt::ui::rec_viz().menuMove(+1); return GestureResult::TAKEN; }
            return GestureResult::PASS;
        case Gesture::DPAD_LEFT:
            if (pt::ui::rec_viz().menuOpen()) { pt::ui::rec_viz().menuAdjust(-1); return GestureResult::TAKEN; }
            return GestureResult::PASS;
        case Gesture::DPAD_RIGHT:
            if (pt::ui::rec_viz().menuOpen()) { pt::ui::rec_viz().menuAdjust(+1); return GestureResult::TAKEN; }
            return GestureResult::PASS;

        default:
            return GestureResult::PASS;
    }
}

GestureResult InputDispatcher::moonset_close_layer(Gesture g) {
    MoonsetState& se = s_.moonset;
    switch (g) {
        case Gesture::A:   // YES - discard and leave
            se.showConfirmClose = false;
            close_moonset();
            break;
        case Gesture::B:   // NO - stay
            se.showConfirmClose = false;
            break;
        default:
            break;
    }
    return GestureResult::TAKEN;
}

void InputDispatcher::moonset_audition() {
    MoonsetState& se = s_.moonset;

    // Only while the TRANSPORT IS STOPPED: `playbackPosition` also tracks song voices on this sample.
    if (se.playbackPosition >= 0.0f && !host_.is_playing()) {
        host_.stop_preview();
        se.playbackPosition = -1.0f;
        return;
    }

    // ?? A pending restore from the previous preview lands first, or it strips this one's EQ and sends.
    run_due_moonset_preview_restore(/*force=*/true);

    moonsetPending_.previewRestore     = true;
    moonsetPending_.previewRestoreAtMs = now_ms_ + 100;
    moonsetPending_.previewRestoreInst = se.instrumentId;

    // The FX row is auditioned by applying it, and the clean audio restored after.
    host_.restore_fx_preview_backup();
    if (se.fx_armed()) {
        before_long_operation();
        host_.save_fx_preview_backup(se.instrumentId);
        host_.apply_sample_fx(se.instrumentId, se.fxType, se.fx_param());
    }

    // SYNC is auditioned from a copy (it changes the length), taking the pending PITCH with it.
    const songcore::SampleSync sync = planned_moonset_sync();
    const bool synced = (sync.kind != songcore::SampleSync::Kind::NONE);
    if (synced) before_long_operation();
    const int pitch = synced ? 0 : se.pitchSemitones;

    host_.set_preview_track(-1);   // a waveform being edited is not in the arrangement
    host_.preview_sample_editor(se.instrumentId, se.sourceMode, se.selectionStart, se.selectionEnd,
                                se.totalFrames, pitch, sync);
}

// ??? Opening and closing ?????????????????????????????????????????????????????????????????????????

void InputDispatcher::open_moonset() {
    const Instrument& ins = s_.project->instruments[static_cast<size_t>(s_.currentInstrument)];
    if (ins.instrumentType == songcore::InstrumentType::SOUNDFONT) return;

    s_.moonset              = MoonsetState{};   // a fresh session, every time
    pt::ui::rec_viz().closeMenu();   // shared with the sample editor: an open menu hides the waveform and eats the D-pad
    s_.moonset.sampleId     = s_.currentInstrument;
    s_.moonset.instrumentId = s_.currentInstrument;
    s_.moonset.cursorRow    = 1;
    s_.moonset.cursorCol    = 0;

    s_.previousScreen = s_.currentScreen;
    s_.currentScreen  = ScreenType::MOONSET;
    init_moonset_state();
}

void InputDispatcher::init_moonset_state() {
    MoonsetState& se  = s_.moonset;
    const Instrument&  ins = s_.project->instruments[static_cast<size_t>(se.instrumentId)];

    // The NAME is the FILE's - it is what you will save back over.
    se.sampleFilePath = ins.sampleFilePath.value_or("");
    se.sampleName     = se.sampleFilePath.empty() ? "" : path_stem(se.sampleFilePath);

    se.totalFrames   = host_.sample_length(se.instrumentId);
    se.sampleRate    = host_.sample_rate_of(se.instrumentId);
    se.hasStereoData = host_.has_stereo_data(se.instrumentId);
    // BIT opens at the sample's own depth, which is also the highest it can offer.
    se.sourceBitDepth = host_.sample_bit_depth(se.instrumentId);
    se.bitDepth       = se.sourceBitDepth;

    // ?? SOURCE opens on STEREO for a stereo sample: any other mode saves one channel.
    se.sourceMode = se.hasStereoData ? 2 /*STEREO*/ : 0;

    se.waveformData  = host_.sample_waveform(se.instrumentId, WAVEFORM_BINS, 0, 0,
                                             waveform_channel(se.sourceMode));

    // The selection opens on the instrument's own window, stored as 0..255 and scaled to frames here.
    if (se.totalFrames > 0) {
        se.selectionStart = (static_cast<int64_t>(ins.sampleStart) * se.totalFrames) / 255;
        se.selectionEnd   = (static_cast<int64_t>(ins.sampleEnd) * se.totalFrames) / 255;
        // ?? An inverted window runs to the end, as the engine plays it.
        if (se.selectionStart >= se.selectionEnd) {
            // A START of 0xFF scales to the last frame; the tail must be at least one frame wide.
            se.selectionStart = std::min<int64_t>(se.selectionStart, se.totalFrames - 1);
            se.selectionEnd   = se.totalFrames;
        }
    } else {
        se.selectionStart = 0;
        se.selectionEnd   = 0;
    }

    // The file's markers, from the project. ?? Made sorted, unique and strictly inside the sample - the
    // only door they come in through, and slice_bounds reads neighbours as one slice's edges.
    se.fileMarkers.clear();
    se.fileMarkers.reserve(ins.sliceMarkers.size());
    for (const int64_t m : ins.sliceMarkers)
        if (m >= 1 && m < static_cast<int64_t>(se.totalFrames)) se.fileMarkers.push_back(static_cast<int>(m));
    std::sort(se.fileMarkers.begin(), se.fileMarkers.end());
    se.fileMarkers.erase(std::unique(se.fileMarkers.begin(), se.fileMarkers.end()), se.fileMarkers.end());

    // ?? Both other lists open empty: an empty detector list re-detects, and hand-placed frames index
    // the old audio.
    se.transientMarkers.clear();
    se.transientKey = -1;
    se.manualMarkers.clear();
    se.manualKeyMethod = -1;
    se.manualKeyParam  = -1;
    se.sliceIndex = 0;
    moonset_ensure_transients(host_, se);
    // ?? sliceMethod survives, so a second sample keeps TRANSIENT.

    se.recordActive = false;
    se.recordFailed = false;
    se.recordSkipFrames = 0;
    se.playbackPosition = -1.0f;

    // Populate the audio input devices list from the host
    host_.listInputDevices(se.audioInputDevices);
    set_status(s_, "INPUT DEVICES: " + std::to_string(se.audioInputDevices.size()) + " devices", true);
}

void InputDispatcher::close_moonset() {
    pt::ui::rec_viz().closeMenu();
    // Stop recording if active before closing the editor
    MoonsetState& se = s_.moonset;
    if (se.recordActive) {
        host_.stop_record();
        se.recordActive = false;
        se.recordSkipFrames = 0;
    }
    
    // Pay any pending audition restore now, or it lands on a slot no longer on screen.
    run_due_moonset_preview_restore(/*force=*/true);

    host_.restore_fx_preview_backup();          // drop an un-applied FX preview
    host_.free_sample_undo(se.instrumentId);     // unreachable once the editor is gone
    host_.clear_previews();                     // the 254/255 scratch slots

    s_.currentScreen = s_.previousScreen;
}

// ??? The selection: A+DPAD on rows 3..8 ??????????????????????????????????????????????????????????

void InputDispatcher::nudge_moonset_selection_edge(int64_t delta) {
    MoonsetState& se = s_.moonset;

    // An empty slot reaches this (EDIT, DOWN, DOWN, A+RIGHT), and the clamps would have lo > hi.
    if (se.totalFrames <= 0) return;

    const int64_t maxFrame = se.totalFrames;
    const int     dir      = (delta >= 0) ? 1 : -1;

    // SNAP: the nearest zero crossing in the direction of travel, ?? in the signal SAVE writes.
    auto snap = [&](int64_t f) -> int64_t {
        if (!se.snapEnabled) return f;
        return host_.find_zero_crossing(se.instrumentId, static_cast<int>(f), dir, se.sourceMode);
    };

    if (se.cursorCol == 0) {
        // START can never reach END - an inverted selection is not a selection.
        const int64_t raw = std::clamp<int64_t>(se.selectionStart + delta, 0, se.selectionEnd - 1);
        se.selectionStart = std::min<int64_t>(snap(raw), se.selectionEnd - 1);
    } else {
        const int64_t raw = std::clamp<int64_t>(se.selectionEnd + delta, se.selectionStart + 1, maxFrame);
        se.selectionEnd   = std::clamp<int64_t>(snap(raw), se.selectionStart + 1, maxFrame);
    }

    // LOOP : la pr�-�coute suit les changements de d�but/fin.
    if (se.loopPreview && se.playbackPosition >= 0.0f) {
        host_.stop_preview();
        se.playbackPosition = -1.0f;
        moonset_audition();
    }
}

// ??? The slice markers: A+DPAD and A+B on row 11 ?????????????????????????????????????????????????

namespace {

/**
 * Put boundary `k` at `want` (or make one when `k` is past the list) and return its new index, ?1 when
 * there is no room. Its number follows its position. ?? Two never share a frame: a taken landing is
 * stepped past. Frame 0 and the last frame are the bounds, never boundaries.
 */
int place_slice_marker(std::vector<MoonsetSliceMarker>& m, int k, int64_t want, int dir, int totalFrames) {
    const int64_t last = static_cast<int64_t>(totalFrames) - 1;
    if (last < 1) return -1;   // no room for an interior boundary at all

    // ?? By index: the dragged boundary must not collide with itself.
    const auto taken = [&](int64_t f) {
        for (int i = 0; i < static_cast<int>(m.size()); ++i)
            if (i != k && static_cast<int64_t>(m[static_cast<size_t>(i)].frame) == f) return true;
        return false;
    };

    int64_t at = std::clamp<int64_t>(want, 1, last);
    while (at >= 1 && at <= last && taken(at)) at += dir;
    if (at < 1 || at > last) return -1;   // walked off the end through a wall of boundaries

    // ?? Move, never rebuild: originFrame is the identity. A new one has none (?1).
    if (k >= 0 && k < static_cast<int>(m.size())) m[static_cast<size_t>(k)].frame = static_cast<int>(at);
    else                                          m.push_back(MoonsetSliceMarker{static_cast<int>(at), -1});
    std::sort(m.begin(), m.end(),
              [](const MoonsetSliceMarker& a, const MoonsetSliceMarker& b) { return a.frame < b.frame; });

    for (int i = 0; i < static_cast<int>(m.size()); ++i)
        if (static_cast<int64_t>(m[static_cast<size_t>(i)].frame) == at) return i;
    return -1;   // unreachable: the frame was just written and the frames are unique
}

}  // namespace

/** Copy what the method shows into the editable list, stamped with its (method, parameter). */
void InputDispatcher::materialise_moonset_markers() {
    MoonsetState& se = s_.moonset;
    if (se.manual_markers_live()) return;

    // ?? Each copy keeps its origin: after a drag the index no longer says which cut it was.
    se.manualMarkers.clear();
    if (se.sliceMethod == MoonsetModule::SLICE_TRANSIENT) {
        for (const int f : se.transientMarkers) se.manualMarkers.push_back(MoonsetSliceMarker{f, f});
    } else if (se.sliceMethod == MoonsetModule::SLICE_DIVIDE) {
        for (const int f : divide_points(se.totalFrames, se.sliceDivisions))
            se.manualMarkers.push_back(MoonsetSliceMarker{f, f});
    } else if (se.sliceMethod == MoonsetModule::SLICE_MANUAL) {
        // ?? Origin ?1: a file cue point has no computed home, so A+B removes it.
        for (const int f : se.fileMarkers) se.manualMarkers.push_back(MoonsetSliceMarker{f, -1});
    }
    se.manualKeyMethod = se.sliceMethod;
    se.manualKeyParam  = se.manual_key_param();
}

/** The selection follows row 11's slice. ?? Every boundary or cursor move ends here, so START plays
 *  the slice as it now is. */
void InputDispatcher::select_moonset_slice() {
    MoonsetState& se = s_.moonset;
    int64_t start = 0, end = 0;
    se.slice_bounds(se.sliceIndex, start, end);
    se.selectionStart = start;
    se.selectionEnd   = end;
}

void InputDispatcher::nudge_moonset_slice_marker(int64_t delta) {
    MoonsetState& se = s_.moonset;

    // An empty slot reaches this screen; `std::clamp` with lo > hi is UB.
    if (se.totalFrames <= 0 || delta == 0) return;

    const bool manual = se.sliceMethod == MoonsetModule::SLICE_MANUAL;
    const int  k      = se.slice_marker_index();

    // Slice 00's left edge is the sample's start. Under MANUAL a rightward drag makes a boundary there.
    if (k < 0 && (!manual || delta < 0)) return;

    materialise_moonset_markers();
    std::vector<MoonsetSliceMarker>& m = se.manualMarkers;

    // A boundary past the end of the list is MANUAL's next free slot, born on the boundary to its left.
    if (k >= static_cast<int>(m.size()) && !manual) return;
    const int64_t from = (k >= 0 && k < static_cast<int>(m.size()))
                             ? static_cast<int64_t>(m[static_cast<size_t>(k)].frame)
                             : (k < 0 || m.empty() ? 0 : static_cast<int64_t>(m.back().frame));

    const int dir  = (delta > 0) ? 1 : -1;
    int64_t   want = from + delta;
    // SNAP (row 2) applies to boundaries as to selection edges, searching in the direction of travel.
    if (se.snapEnabled)
        want = host_.find_zero_crossing(
            se.instrumentId,
            static_cast<int>(std::clamp<int64_t>(want, 0, static_cast<int64_t>(se.totalFrames) - 1)),
            dir, se.sourceMode);

    const int landed = place_slice_marker(m, k, want, dir, se.totalFrames);
    if (landed < 0) return;

    se.sliceIndex = landed + 1;   // the boundary's own number - marker `j` is the left edge of slice j+1
    select_moonset_slice();
}

void InputDispatcher::reset_moonset_slice_marker() {
    MoonsetState& se = s_.moonset;

    const int k = se.slice_marker_index();
    if (k < 0) return;   // slice 00's left edge is the sample's own start: no boundary, nothing to undo

    // ?? Materialise first: under MANUAL it may be a cue point not yet copied.
    materialise_moonset_markers();
    std::vector<MoonsetSliceMarker>& m = se.manualMarkers;
    if (k >= static_cast<int>(m.size())) return;      // MANUAL's free slot holds no boundary yet

    // Its own origin, never its index (after a crossing, another cut's).
    const int origin = m[static_cast<size_t>(k)].originFrame;

    if (origin < 0) {
        // Placed by hand or from the file: A+B removes it. ?? An empty list keeps its stamp, or the read
        // falls back to the file's cue points and undoes every delete.
        m.erase(m.begin() + k);
    } else {
        // ?? Through `place_slice_marker`: a neighbour may have been dragged across that position since.
        const int landed = place_slice_marker(m, k, origin, +1, se.totalFrames);
        if (landed < 0) return;
        se.sliceIndex = landed + 1;
    }

    select_moonset_slice();
}

/**
 * Cut a boundary at the playhead - MANUAL only, while the sample sounds. The frame is the playhead as A
 * went down (the line the user tapped to); SNAP searches backward, since a tap is always late.
 */
void InputDispatcher::tap_moonset_slice_marker() {
    MoonsetState& se = s_.moonset;

    // Consumed: a press that ended in an A+DPAD must not leave a snapshot for the next tap.
    const float at                  = moonsetPending_.sliceTapPlayhead;
    moonsetPending_.sliceTapPlayhead = -1.0f;

    if (se.sliceMethod != MoonsetModule::SLICE_MANUAL) return;
    if (se.totalFrames <= 0 || at < 0.0f) return;

    const int64_t last = static_cast<int64_t>(se.totalFrames) - 1;
    int64_t       want = std::clamp<int64_t>(
        static_cast<int64_t>(at * static_cast<float>(se.totalFrames)), 0, last);
    if (se.snapEnabled)
        want = host_.find_zero_crossing(se.instrumentId, static_cast<int>(want), -1, se.sourceMode);

    materialise_moonset_markers();
    std::vector<MoonsetSliceMarker>& m = se.manualMarkers;

    // ?? Past the end of the list on purpose: a tap always MAKES a boundary (origin ?1, so A+B removes it).
    const int landed = place_slice_marker(m, static_cast<int>(m.size()), want, +1, se.totalFrames);
    if (landed < 0) return;

    se.sliceIndex = landed + 1;   // the cursor follows the cut onto the slice it opens
    select_moonset_slice();
}

// ??? RATE and BIT: the two cells that rebuild the audio ??????????????????????????????????????????

void InputDispatcher::apply_moonset_rate_and_bits() {
    before_long_operation();
    MoonsetState& se     = s_.moonset;
    const int          factor = (se.rateMode == 1) ? 2 : (se.rateMode == 2) ? 4 : 1;
    const int          oldLen = se.totalFrames;

    // ?? Notes already scheduled (up to LONG_OPERATION_MS) keep the old base frequency.
    host_.apply_rate_and_bits(se.instrumentId, factor, se.bitDepth);

    const int newLen = host_.sample_length(se.instrumentId);
    rescale_positions(se, oldLen, newLen);
    se.totalFrames = newLen;
    se.sampleRate  = host_.sample_rate_of(se.instrumentId);
    se.waveformData = host_.sample_waveform(se.instrumentId, WAVEFORM_BINS, 0, 0,
                                            waveform_channel(se.sourceMode));
    se.isModified = true;
}

// ??? The view, after an op ???????????????????????????????????????????????????????????????????????

void InputDispatcher::refresh_moonset_view(bool reset_selection) {
    MoonsetState& se     = s_.moonset;
    const int          newLen = host_.sample_length(se.instrumentId);
    se.totalFrames = newLen;

    // ?? Reset, not clamp: a clamped selection after a shortening op is one the user never made.
    if (reset_selection) {
        se.selectionStart = 0;
        se.selectionEnd   = newLen;

        // ?? And the instrument's START/END and LOOP: 0-255 fractions of the buffer, re-aimed by a
        // length change. The ops reset the engine's copy; this is the project's.
        Instrument& ins = host_.edit_project().instruments[static_cast<size_t>(se.instrumentId)];
        if (ins.sampleStart != 0x00 || ins.sampleEnd != 0xFF ||
            ins.loopStart   != 0x00 || ins.loopEnd   != 0xFF) {
            ins.sampleStart = 0x00;
            ins.sampleEnd   = 0xFF;
            ins.loopStart   = 0x00;
            ins.loopEnd     = 0xFF;
            host_.push_instrument(se.instrumentId);
            mark_modified();
        }

        // ?? And every marker list: they index audio just replaced, and a stale line looks right. This
        // also keeps a save after a resize from writing back half the old cue points.
        se.fileMarkers.clear();
        se.transientMarkers.clear();
        se.transientKey = -1;
        se.manualMarkers.clear();
        se.manualKeyMethod = -1;
        se.manualKeyParam  = -1;
        se.sliceIndex      = 0;   // the ceiling has just collapsed to a single slice, the whole sample
    }

    se.waveformData = host_.sample_waveform(se.instrumentId, WAVEFORM_BINS,
                                            static_cast<int>(se.view_start()),
                                            static_cast<int>(se.view_end()),
                                            waveform_channel(se.sourceMode));
}

// ??? A on the op rows, the FX row, the name and the save buttons ?????????????????????????????????

void InputDispatcher::moonset_confirm() {
    before_long_operation();
    MoonsetState& se     = s_.moonset;
    // ?? First: an auditioned effect has processed the sample itself, and SAVE or COPY would take it.
    // Not on row 11, whose tap must not stop the audition it is tapped against.
    if (se.cursorRow != 11) host_.restore_fx_preview_backup();
    const int          instId = se.instrumentId;
    const int          startF = static_cast<int>(se.selectionStart);
    const int          endF   = static_cast<int>(se.selectionEnd);

    auto begin_destructive = [&] { host_.backup_sample(instId); };
    auto in_place = [&](auto&& op) {   // an op that does NOT change the length
        begin_destructive();
        op();
        refresh_moonset_view(/*reset_selection=*/false);
        se.isModified = true;
    };
    auto resizing = [&](auto&& op) {   // an op that DOES
        begin_destructive();
        op();
        refresh_moonset_view(/*reset_selection=*/true);
        se.isModified = true;
    };

    switch (se.cursorRow) {
        // ?? Row 11: cut a boundary at the playhead, mid-audition ?????????????????????????????????
        // On A's release (defer_a_to_release), at the playhead of the press. Editor state: no backup.
        case 11:
            tap_moonset_slice_marker();
            break;

        // ?? Row 13: CROP  COPY  CUT  DUPL  PASTE  DEL ????????????????????????????????????????????
        case 13:
            switch (se.cursorCol) {
                case 0:   // CROP - keep the selection, discard the rest
                    if (startF < endF) resizing([&] { host_.crop_sample(instId, startF, endF); });
                    break;
                case 1:   // COPY - the ONE op on this row that changes nothing, so it takes no backup
                    host_.copy_region(instId, startF, endF);
                    break;
                case 2:   // CUT = copy, then delete
                    if (startF < endF) resizing([&] {
                        host_.copy_region(instId, startF, endF);
                        host_.delete_sample_region(instId, startF, endF);
                    });
                    break;
                case 3:   // DUPL = copy the selection and paste it at the END
                    if (startF < endF) resizing([&] {
                        host_.copy_region(instId, startF, endF);
                        host_.paste_region(instId, se.totalFrames);
                    });
                    break;
                case 4:   // PASTE - inserts at the selection's START
                    if (host_.clipboard_length() > 0)
                        resizing([&] { host_.paste_region(instId, startF); });
                    break;
                case 5:   // DEL
                    if (startF < endF)
                        resizing([&] { host_.delete_sample_region(instId, startF, endF); });
                    break;
                default: break;
            }
            break;

        // ?? Row 14: NORM  FADE+  FADE-  SLNC  REV  UNDO ??????????????????????????????????????????
        case 14:
            switch (se.cursorCol) {
                case 0: in_place([&] { host_.normalize_sample(instId, startF, endF); }); break;
                case 1: in_place([&] { host_.fade_in_sample(instId, startF, endF); }); break;
                case 2: in_place([&] { host_.fade_out_sample(instId, startF, endF); }); break;
                case 3: in_place([&] { host_.silence_region(instId, startF, endF); }); break;
                case 4: in_place([&] { host_.reverse_sample(instId, startF, endF); }); break;

                case 5: {   // UNDO - one level, and it may restore a DIFFERENT length
                    host_.undo_sample(instId);
                    refresh_moonset_view(/*reset_selection=*/true);
                    // ?? `isModified` is NOT cleared: one undo is one step back, not back to the file.
                    se.slicePosition = std::clamp<int64_t>(se.slicePosition, 0, se.totalFrames);
                    break;
                }
                default: break;
            }
            break;

        // ?? Row 16: the FX row. Col 2 is APPLY; the other two are dialled with A+DPAD. ???????????
        case 16: {
            if (se.cursorCol != 2) break;

            if (se.fxType <= MoonsetModule::FX_EQ) {
                if (!se.fx_armed()) break;

                begin_destructive();
                host_.apply_sample_fx(instId, se.fxType, se.fx_param());
                refresh_moonset_view(/*reset_selection=*/false);
                se.isModified = true;
                // Spent - left in the cell, START would audition it over its own result.
                if (se.fxType == MoonsetModule::FX_EQ) se.fxEqSlot = -1;
                else                                        se.fxValue  = 0;
                break;
            }

            apply_moonset_sync();
            break;
        }

        // ?? Row 18: NAME ????????????????????????????????????????????????????????????????????????
        case 18:
            open_qwerty(QwertyContext::SAMPLE_NAME, se.sampleName, "SAMPLE NAME:",
                        fs_.samples_directory());
            break;

        // ?? Row 19: LOAD  SAVE  OVERWRITE  CHOP ?????????????????????????????????????????????????
        case 19:
            switch (se.cursorCol) {
                case 0: {   // LOAD - a different sample, into the slot the editor is already open on
                    // ?? Keep previousScreen as the editor's return target.
                    const ScreenType keep = s_.previousScreen;
                    open_file_browser(AppState::BrowserPurpose::LOAD_MOONSET,
                                      browser_dir(BrowserDir::SAMPLES), {"wav"});
                    s_.previousScreen = keep;
                    break;
                }

                case 1: {   // SAVE - to <name>.wav, or ask for a name if that one is taken
                    const std::string base = se.sampleName.empty() ? "SAMPLE" : se.sampleName;
                    const std::string dir  = fs_.samples_directory();
                    bake_moonset_pending_pitch();

                    // Taken: suggest the next free name - SAVE is not OVERWRITE.
                    const std::string stem = free_wav_stem(fs_, dir, base);
                    if (stem == base)
                        save_moonset_sample_to(dir + "/" + base + ".wav", /*adopt_name=*/true);
                    else
                        open_qwerty(QwertyContext::SAMPLE_SAVE, stem, "SAVE AS:", dir,
                                    /*max_length=*/24, /*clear_on_first_b=*/true);
                    break;
                }

                case 2:   // OVERWRITE - back over the file it came from. Nothing to do if it has none.
                    if (!se.sampleFilePath.empty()) {
                        bake_moonset_pending_pitch();
                        save_moonset_sample_to(se.sampleFilePath, /*adopt_name=*/false);
                    }
                    break;

                case 3:   // CHOP
                    moonset_chop();
                    break;

                default: break;
            }
            break;

        default:
            break;
    }
}

// ??? SYNC: fit the sample to the grid ????????????????????????????????????????????????????????????

songcore::SampleSync InputDispatcher::planned_moonset_sync() const {
    const MoonsetState& se = s_.moonset;
    if (se.fxType != MoonsetModule::FX_SYNC) return {};

    // RPITCH resamples to the song's tempo (faster is higher); TSTRETCH holds the pitch and fits its
    // box's BPM.
    const bool   stretch = (se.syncType == MoonsetModule::SYNC_TSTRETCH);
    const int    bpm     = stretch ? se.stretch_bpm(s_.project->tempo) : s_.project->tempo;
    const double rawSecs = (se.sampleRate > 0)
                               ? static_cast<double>(se.totalFrames) / se.sampleRate
                               : 0.0;
    if (rawSecs <= 0.0 || bpm <= 0) return {};

    const double targetSecs = MoonsetModule::duration_beats(se.durationIndex) * 60.0 / bpm;

    // ?? "Already on the grid" is asked in frames: �0.1 % is most of a tick on a 4-bar loop.
    const double  ratio      = targetSecs / rawSecs;
    const int64_t wantFrames = std::llround(static_cast<double>(se.totalFrames) * ratio);
    if (ratio <= 0.001 || wantFrames < 1) return {};
    const bool fits = (wantFrames == se.totalFrames);

    songcore::SampleSync sync;
    if (stretch) {
        // The pending PITCH is part of a stretch, so a sample that fits can still have work to do.
        if (fits && se.pitchSemitones == 0) return {};
        sync.kind      = songcore::SampleSync::Kind::STRETCH;
        sync.frames    = static_cast<int>(wantFrames);
        sync.semitones = static_cast<float>(se.pitchSemitones);
        sync.grainMs   = static_cast<float>(se.stretchGrainMs);
    } else {
        if (fits) return {};
        // ?? Fractional: whole semitones can miss a 4-bar loop by ~230 ms.
        const double exact = 12.0 * std::log(rawSecs / targetSecs) / std::log(2.0);
        sync.kind      = songcore::SampleSync::Kind::REPITCH;
        sync.semitones = static_cast<float>(std::clamp(exact, -24.0, 24.0));
    }
    return sync;
}

void InputDispatcher::apply_moonset_sync() {
    MoonsetState&         se   = s_.moonset;
    const songcore::SampleSync sync = planned_moonset_sync();
    if (sync.kind == songcore::SampleSync::Kind::NONE) return;

    const int instId = se.instrumentId;
    const int oldLen = se.totalFrames;

    host_.restore_fx_preview_backup();   // act on the clean audio
    host_.backup_sample(instId);
    host_.sync_sample(instId, sync);
    // Spent either way: TSTRETCH has baked the pending shift in, RPITCH has replaced it.
    se.pitchSemitones = 0;

    rescale_positions(se, oldLen, host_.sample_length(instId));
    // ?? RATE resets (the result is the new original; it would decimate twice). BIT stays: SAVE writes it.
    se.rateMode = 0;
    refresh_moonset_view(/*reset_selection=*/true);
    se.isModified = true;
}

// ??? The pending pitch shift ?????????????????????????????????????????????????????????????????????

void InputDispatcher::bake_moonset_pending_pitch() {
    MoonsetState& se = s_.moonset;
    if (se.pitchSemitones == 0) return;

    const int oldLen = se.totalFrames;
    host_.pitch_shift_sample(se.instrumentId, static_cast<float>(se.pitchSemitones));
    const int newLen = host_.sample_length(se.instrumentId);
    // Everything frame-measured moves with the audio; the selection is scaled, not reset.
    rescale_positions(se, oldLen, newLen);
    se.totalFrames    = newLen;
    se.pitchSemitones = 0;   // spent
    se.rateMode       = 0;   // the shifted buffer IS the new original - see sample_edit.h
    // ?? BIT is NOT reset. This runs on the way INTO a save, and BIT is the depth that save writes.
    se.waveformData   = host_.sample_waveform(se.instrumentId, WAVEFORM_BINS, 0, 0,
                                              waveform_channel(se.sourceMode));

    // The instrument's playback params were derived from the old buffer's length.
    host_.push_instrument(se.instrumentId);
    mark_modified();
}

// ??? The slices ??????????????????????????????????????????????????????????????????????????????????

std::vector<int> InputDispatcher::compute_moonset_slice_cue_points() const {
    const MoonsetState& se = s_.moonset;

    // DIVIDE only while it is still arithmetic - once a boundary is dragged, it is a list like any other.
    if (se.sliceMethod == MoonsetModule::SLICE_DIVIDE && se.marker_count() == 0)
        return divide_points(se.totalFrames, se.sliceDivisions);

    // Whichever list the method answers with (MANUAL is seeded from the file's). ?? Under OFF the file's
    // own: a save with slicing OFF can neither add nor drop a slice. Frames 0 and the end are dropped.
    std::vector<int> cues;
    for (int i = 0; i < se.marker_count(); ++i) {
        const int m = static_cast<int>(se.marker_position(i));
        if (m > 0 && m < se.totalFrames) cues.push_back(m);
    }
    return cues;
}

std::vector<std::pair<int64_t, int64_t>> InputDispatcher::current_moonset_slices() const {
    const MoonsetState& se = s_.moonset;

    // N markers ? N+1 slices. OFF, TRANSIENT before detection, and MANUAL with no boundaries are one
    // slice - the whole sample.
    const int markers = se.marker_count();
    const int count   = (se.sliceMethod == MoonsetModule::SLICE_OFF)
                            ? 0
                            : (markers > 0)
                                  ? markers + 1
                                  : (se.sliceMethod == MoonsetModule::SLICE_DIVIDE)
                                        ? std::max(se.sliceDivisions, 1)
                                        : 1;

    std::vector<std::pair<int64_t, int64_t>> out;
    out.reserve(static_cast<size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        int64_t start = 0, end = 0;
        se.slice_bounds(i, start, end);
        out.emplace_back(start, end);
    }
    return out;
}

// ??? SAVE ????????????????????????????????????????????????????????????????????????????????????????

void InputDispatcher::save_moonset_sample_to(const std::string& path, bool adopt_name) {
    before_long_operation();
    MoonsetState& se   = s_.moonset;
    const std::vector<int> cues = compute_moonset_slice_cue_points();

    if (!host_.save_sample_wav(se.instrumentId, path, cues, se.sourceMode, se.hasStereoData,
                               se.bitDepth)) {
        set_status(s_, "SAVE FAILED", false);
        return;
    }
    host_.adopt_saved_sample(se.instrumentId, se.bitDepth);

    // ?? A mono save reloads the file: the buffer may still be stereo, and the slot must match the file.
    const bool wrote_mono = !(se.hasStereoData && se.sourceMode == 2);
    if (wrote_mono) host_.load_sample(se.instrumentId, path);

    Instrument& ins = host_.edit_project().instruments[static_cast<size_t>(se.instrumentId)];
    ins.sampleFilePath = path;
    // The markers go into the PROJECT too - the .ptp is what a reload reads first.
    ins.sliceMarkers.clear();
    ins.sliceMarkers.reserve(cues.size());
    for (const int c : cues) ins.sliceMarkers.push_back(static_cast<int64_t>(c));

    se.sampleFilePath = path;
    if (adopt_name) se.sampleName = path_stem(path);
    se.isModified    = false;
    se.hasStereoData = host_.has_stereo_data(se.instrumentId);

    mark_modified();
    s_.currentScreen = s_.previousScreen;   // a save LEAVES the editor, as it does on Android
}

// ??? CHOP ????????????????????????????????????????????????????????????????????????????????????????

void InputDispatcher::moonset_chop() {
    MoonsetState& se = s_.moonset;
    if (se.sliceMethod == MoonsetModule::SLICE_OFF) return;

    const std::vector<std::pair<int64_t, int64_t>> slices = current_moonset_slices();
    if (slices.empty()) return;

    // A slice becomes a file name, sanitized like every other the app writes.
    const std::string base = safe_file_name(se.sampleName.empty() ? "SAMPLE" : se.sampleName);

    // Samples/Chops/<base>/ - its own folder, so a 32-slice break does not bury the sample directory.
    const std::string samples = fs_.samples_directory();
    fs_.create_folder(samples, "Chops");                     // "" if it already exists - either is fine
    const std::string chops = samples + "/Chops";
    fs_.create_folder(chops, base);
    const std::string dir = chops + "/" + base;

    const int written = host_.chop_sample(se.instrumentId, dir, base, slices, se.bitDepth);
    set_status(s_, written > 0 ? ("CHOPPED " + std::to_string(written)) : "CHOP FAILED", written > 0);
}

// ??? The audition's deferred restore ?????????????????????????????????????????????????????????????

void InputDispatcher::run_due_moonset_preview_restore(bool force) {
    if (!moonsetPending_.previewRestore) return;
    if (!force && now_ms_ < moonsetPending_.previewRestoreAtMs) return;

    moonsetPending_.previewRestore = false;
    host_.finish_sample_preview(moonsetPending_.previewRestoreInst);
}

// ??? The cursor and the edit ?????????????????????????????????????????????????????????????????????

CursorContext InputDispatcher::moonset_context() const {
    const MoonsetState& m = s_.moonset;
    if (m.granularFocus) {
        const songcore::Instrument& ins =
            s_.project->instruments[static_cast<size_t>(m.instrumentId)];
        return moonset_granular_context(ins.granular, m.granularColumn, m.granularRow);
    }
    MoonsetState s = s_.moonset;
    return moonset_.cursor_context(s);
}

void InputDispatcher::refresh_moonset_record() {
    if (!on_moonset()) return;
    MoonsetState& se = s_.moonset;
    if (se.recordActive) {
        se.recordLevel = host_.record_peak();
        const int len = host_.record_length();
        se.recordFrames = len;
        const float* L = host_.record_buffer_l();
        const float* R = host_.record_buffer_r();
        if (L && len > 0) {
            // pan / bass / high
            int start = std::max(0, len - 512);
            float sumAbsL = 0, sumAbsR = 0;
            float highL = 0, highR = 0;
            float prevL = 0, prevR = 0;
            bool first = true;
            for (int i = start; i < len; ++i) {
                float l = L[i]; if (l < 0) l = -l;
                float r = R ? R[i] : L[i]; if (r < 0) r = -r;
                sumAbsL += l; sumAbsR += r;
                if (!first) {
                    float dl = L[i] - prevL; if (dl < 0) dl = -dl; highL += dl;
                    if (R) { float dr = R[i] - prevR; if (dr < 0) dr = -dr; highR += dr; }
                }
                prevL = L[i]; prevR = R ? R[i] : L[i];
                first = false;
            }
            int n = len - start;
            float meanL = sumAbsL / n;
            float meanR = sumAbsR / n;
            float total = meanL + meanR;
            se.recordPan = (total > 0.0001f) ? (meanR - meanL) / total : 0.0f;
            float bass = (meanL + meanR) * 0.5f;
            float high = ((highL + highR) * 0.5f) / (float)n;
            se.recordBass = std::min(1.0f, bass * 2.0f);
            se.recordHigh = std::min(1.0f, high * 8.0f);

            pt::ui::rec_viz().feedBuffer(L, R, len, se.sampleRate);
            // full-buffer waveform (scaled to fit) for L and R
            if (len > se.lastRecordLength + 1024 || se.recWaveL.empty()) {
                const int bins = 256;
                se.recWaveL.assign(bins * 2, 0.0f);
                se.recWaveR.assign(bins * 2, 0.0f);
                se.recThick.assign(bins, 0.1f);
                for (int i = 0; i < bins; ++i) {
                    int s0 = (int)((long long)i * len / bins);
                    int e0 = (int)((long long)(i + 1) * len / bins);
                    if (e0 <= s0) e0 = s0 + 1;
                    if (e0 > len) e0 = len;
                    float minL = 0, maxL = 0, minR = 0, maxR = 0;
                    int zc = 0; float prev = 0; bool first2 = true;
                    for (int j = s0; j < e0; ++j) {
                        if (L[j] < minL) minL = L[j];
                        if (L[j] > maxL) maxL = L[j];
                        float rv = R ? R[j] : L[j];
                        if (rv < minR) minR = rv;
                        if (rv > maxR) maxR = rv;
                        if (!first2 && ((L[j] >= 0) != (prev >= 0))) ++zc;
                        prev = L[j];
                        first2 = false;
                    }
                    se.recWaveL[i*2]   = minL;
                    se.recWaveL[i*2+1] = maxL;
                    se.recWaveR[i*2]   = minR;
                    se.recWaveR[i*2+1] = maxR;
                    int nn = e0 - s0;
                    float zcr = (nn > 1) ? (float)zc / (float)(nn - 1) : 0.0f;
                    float thick = 1.0f - std::min(1.0f, zcr * 2.0f);
                    if (thick < 0.1f) thick = 0.1f;
                    se.recThick[i] = thick;
                }
                se.lastRecordLength = len;
            }

            // ?? Topographic spectrogram: grille dB bandes�temps, fondu + nouvelle colonne ????????
            const int   bands  = std::max(se.recVizBands, 2);
            const int   cols   = std::max(se.recVizCols, 2);
            const size_t gridSize = static_cast<size_t>(bands) * static_cast<size_t>(cols);
            if (se.recSpecL.size() != gridSize) se.recSpecL.assign(gridSize, -100.0f);
            if (se.recSpecR.size() != gridSize) se.recSpecR.assign(gridSize, -100.0f);

            // Estompage progressif � chaque frame (les formes s'estompent doucement).
            const float fade = se.recVizFade;
            if (fade > 0.0f) {
                for (size_t i = 0; i < gridSize; ++i) {
                    float v = se.recSpecL[i] - fade;  if (v < -100.0f) v = -100.0f; se.recSpecL[i] = v;
                    v = se.recSpecR[i] - fade;        if (v < -100.0f) v = -100.0f; se.recSpecR[i] = v;
                }
            }

            // Nouvelle colonne (plus r�cente � droite) � l'intervalle d'analyse.
            if (++se.recVizFrameCounter >= std::max(se.recVizAnalyzeEvery, 1)) {
                se.recVizFrameCounter = 0;
                const int fftSize = std::max(se.recVizFftSize, 128);
                const int take    = std::min(len, fftSize);
                const float* srcL = L + len - take;
                const float* srcR = (R != nullptr) ? (R + len - take) : srcL;

                std::vector<float> bandsL, bandsR;
                compute_log_bands(srcL, take, se.sampleRate, fftSize, se.recVizFmin, se.recVizFmax,
                                  bands, bandsL);
                compute_log_bands(srcR, take, se.sampleRate, fftSize, se.recVizFmin, se.recVizFmax,
                                  bands, bandsR);

                // Glisser l'historique vers la gauche ; le nouveau son appara�t � droite.
                for (int b = 0; b < bands; ++b) {
                    const size_t row = static_cast<size_t>(b) * static_cast<size_t>(cols);
                    std::memmove(&se.recSpecL[row], &se.recSpecL[row + 1],
                                 (cols - 1) * sizeof(float));
                    std::memmove(&se.recSpecR[row], &se.recSpecR[row + 1],
                                 (cols - 1) * sizeof(float));
                    se.recSpecL[row + static_cast<size_t>(cols) - 1] = bandsL[static_cast<size_t>(b)];
                    se.recSpecR[row + static_cast<size_t>(cols) - 1] = bandsR[static_cast<size_t>(b)];
                }
                se.recVizDirty = true;
            }
        }
    } else {
        se.recordLevel = 0.0f;
        se.recWaveL.clear();
        se.recWaveR.clear();
        se.recThick.clear();
        se.recSpecL.clear();
        se.recSpecR.clear();
        se.lastRecordLength = 0;
    }
}
void InputDispatcher::refresh_moonset_input_devices() {
    MoonsetState& se = s_.moonset;
    std::vector<std::string> fresh;
    host_.listInputDevices(fresh);
    if (fresh == se.audioInputDevices) return;
    se.audioInputDevices = std::move(fresh);
    // The device that was picked is gone (unplugged): fall back to DEFAULT rather than keep a name that
    // maps to no card, which would make REC quietly use the built-in mic.
    if (!se.recordInputDevice.empty() &&
        std::find(se.audioInputDevices.begin(), se.audioInputDevices.end(), se.recordInputDevice) ==
            se.audioInputDevices.end())
        se.recordInputDevice.clear();
}

void InputDispatcher::handle_moonset_record_toggle() {
    MoonsetState& se = s_.moonset;

    if (!se.recordActive) {
        // The list is rebuilt here (cheap, reads /proc/asound): a USB mic plugged after boot, or the
        // saved INPUT from last session, must map to a real code - an empty list would fall back to
        // the built-in mic without saying so.
        refresh_moonset_input_devices();
        // Make sure no capture is left open from a previous attempt.
        host_.stop_record();
        // Applique la dur�e max choisie dans VIZ SETTINGS > DURATION avant d'allouer le buffer d'enregistrement.
        AudioEngine::MAX_RECORD_SECONDS = pt::ui::rec_viz().p.recordMaxSeconds;
        // Start recording on the chosen input device. -1 = the platform default.
        // The list ends with the engine's own buses (P1..P8, M1 MASTER): the backend's cards keep their
        // 1-based index, a bus gets INTERNAL_INPUT_BASE + n and never opens a capture device.
        int device = -1;
        const int phys = std::max(0, static_cast<int>(se.audioInputDevices.size()) -
                                         AudioEngine::INTERNAL_INPUT_COUNT);
        for (size_t i = 0; i < se.audioInputDevices.size(); ++i) {
            if (se.audioInputDevices[i] != se.recordInputDevice) continue;
            device = (static_cast<int>(i) < phys)
                         ? static_cast<int>(i) + 1   // backend uses 1-based indices (0 = default)
                         : AudioEngine::INTERNAL_INPUT_BASE + (static_cast<int>(i) - phys);
            break;
        }
        if (host_.start_record(device)) {
            se.recordActive = true;
            se.recordFailed = false;
            // START (SET menu): DIRECT, 1/4, 2/4, 1 BAR, 2 BAR of count-in at the project's tempo, with the
            // click if CLICK is on. The capture runs from now on (the visualiser is live); the count-in's
            // frames are cut when the take is installed. There is no calibration pause any more.
            static const int kBeats[5] = {0, 1, 2, 4, 8};
            const auto& vp = pt::ui::rec_viz().p;
            se.recordSkipFrames = host_.begin_count_in(kBeats[std::max(0, std::min(4, vp.startMode))], vp.clickOn);
            se.recordFrames = 0;
            set_status(s_, se.recordSkipFrames > 0 ? "COUNT-IN" : "RECORDING", true);
        } else {
            se.recordActive = false;
            se.recordFailed = true;
            set_status(s_, "RECORD FAILED", false);
        }
    } else {
        // Stop and install into the current instrument. Only the user stops a take.
        const int skip = se.recordSkipFrames;
        se.recordSkipFrames = 0;
        if (skip > 0 && host_.record_length() <= skip) {   // stopped during the count-in: nothing to keep
            host_.stop_record();
            se.recordActive = false;
            se.recordFailed = false;
            set_status(s_, "CANCELLED", false);
            return;
        }
        host_.backup_sample(se.instrumentId);
        if (host_.record_capture(se.instrumentId, skip, pt::ui::rec_viz().p.micGainDb)) {
            se.recordActive = false;
            se.recordFailed = false;
            se.isModified = true;
            set_status(s_, "RECORDED", true);
            // Refresh the editor state to see the new sample.
            refresh_moonset_view(true);
        } else {
            se.recordActive = false;
            se.recordFailed = true;
            set_status(s_, "RECORD FAILED", false);
        }
    }
}

bool InputDispatcher::moonset_edit(const InputAction& action) {
    MoonsetState& se = s_.moonset;

    // The granular block: the bytes live in the instrument, not in the session.
    if (se.granularFocus) {
        songcore::Instrument& ins =
            host_.edit_project().instruments[static_cast<size_t>(se.instrumentId)];
        if (!moonset_granular_apply(ins.granular, se.granularColumn, se.granularRow, action.value))
            return false;
        host_.push_instrument(se.instrumentId);   // a sounding voice reads them live
        return true;
    }

    const bool    loopBefore = se.loopPreview;
    const int64_t viewStart0 = se.view_start(), viewEnd0 = se.view_end();
    const int     source0    = se.sourceMode;
    const MoonsetInputResult r = moonset_.handle_input(se, action);
    moonset_ensure_transients(host_, se);   // METHOD / SENS clear the list: detect again at once
    if (se.totalFrames > 0 &&
        (se.view_start() != viewStart0 || se.view_end() != viewEnd0 || se.sourceMode != source0))
        refresh_moonset_view(/*reset_selection=*/false);   // ZOOM, SOURCE: the picture follows the window
    if (r.rateModeChanged || r.bitDepthChanged) apply_moonset_rate_and_bits();

    // LOOP : la pr�-�coute suit l'activation/d�sactivation et les changements de d�but/fin.
    if (se.loopPreview != loopBefore) {
        if (se.loopPreview) {
            if (se.playbackPosition < 0.0f) moonset_audition();
        } else if (se.playbackPosition >= 0.0f) {
            host_.stop_preview();
            se.playbackPosition = -1.0f;
        }
    }

    // ?? false: the editor's session is not the document (a held A+UP on ZOOM would arm an autosave).
    // RATE and BIT push for themselves.
    return false;
}

// ─── The TSTRETCH box ───────────────────────────────────────────────────────────────────────────
// Raised by A on the SYNC row's TSTRETCH cell. It owns the buttons while it is up, but lets Y
// through — its cells have help.

GestureResult InputDispatcher::moonset_stretch_layer(Gesture g) {
    MoonsetState& se = s_.moonset;
    switch (g) {
        case Gesture::DPAD_UP:    se.stretchCursor = moonset_stretch_step(se.stretchCursor, -1, 0); break;
        case Gesture::DPAD_DOWN:  se.stretchCursor = moonset_stretch_step(se.stretchCursor, +1, 0); break;
        case Gesture::DPAD_LEFT:  se.stretchCursor = moonset_stretch_step(se.stretchCursor, 0, -1); break;
        case Gesture::DPAD_RIGHT: se.stretchCursor = moonset_stretch_step(se.stretchCursor, 0, +1); break;

        case Gesture::A_UP:    moonset_stretch_edit(pt::ui::increment_fast); break;
        case Gesture::A_DOWN:  moonset_stretch_edit(pt::ui::decrement_fast); break;
        case Gesture::A_LEFT:  moonset_stretch_edit(pt::ui::decrement); break;
        case Gesture::A_RIGHT: moonset_stretch_edit(pt::ui::increment); break;
        case Gesture::A_B:     moonset_stretch_edit(pt::ui::on_a_b); break;

        case Gesture::A:
            if (se.stretchCursor == MoonsetStretchCell::PREVIEW) {
                moonset_audition();
            } else if (se.stretchCursor == MoonsetStretchCell::APPLY) {
                se.stretchOpen = false;
                before_long_operation();
                apply_moonset_sync();
            }
            break;

        // The editor's own X: it plays the sample as these settings would leave it.
        case Gesture::X: moonset_audition(); break;

        // The settings stay for APPLY on the FX row.
        case Gesture::B: se.stretchOpen = false; break;

        case Gesture::Y:
            return GestureResult::PASS;

        default: break;
    }
    return GestureResult::TAKEN;
}

void InputDispatcher::open_moonset_stretch() {
    s_.moonset.stretchOpen   = true;
    s_.moonset.stretchCursor = MoonsetStretchCell::DURATION;
}

void InputDispatcher::moonset_stretch_edit(InputAction (*fn)(const CursorContext&)) {
    const int         tempo  = s_.project->tempo;
    const InputAction action = fn(moonset_stretch_cursor_context(s_.moonset, tempo));
    // The editor's session, not the document: nothing to mark modified.
    moonset_stretch_handle_input(s_.moonset, action, tempo);
}

}  // namespace pt::ui
