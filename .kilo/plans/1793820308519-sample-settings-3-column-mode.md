# Moonset Sample Settings 3-Column Mode

## Goal
Implement a 3-column layout for sample settings in Moonset module, toggled by keyboard shortcut: **A + right arrow** (when cursor is on row 1, column 0 - ZOOM).

## Changes Required

### 1. Update MoonsetState (moonset.h)
Add 3-column mode state variables:
- `bool sampleSettings3ColumnMode` - toggles 3-column vs scrolling layout
- `int sampleSettingsColumnIndex` - tracks current column in 3-column mode (0-2)

### 2. Add 3-Column Drawing Logic (moonset.cpp)

**Add new function:**
```cpp
void MoonsetModule::draw_sample_settings_3_column(Canvas& c, int x, int y, const MoonsetState& s, const Theme& t) const;
```

**Features:**
- Divide sample settings area into 3 equal columns
- Column 0 (left): View controls (ZOOM/SOURCE/RATE, PITCH/DURATION/BIT, RECORD)
- Column 1 (middle): Editing tools (SELECTION, SLICE, OPERATIONS rows)
- Column 2 (right): Effects and name/save (EFFECT row, NAME, LOAD/SAVE/CHOPS)
- Draw vertical dividers between columns when active
- Handle cursor highlighting per column

**Modify existing draw function:**
- Add conditional call: when `s.sampleSettings3ColumnMode` is true, call `draw_sample_settings_3_column()`
- Place 3-column settings below visualizer (y position offset)
- Keep existing header (rows 1-2) and visualizer intact

### 3. Add Keyboard Shortcut Handler (moonset.cpp)

**In MoonsetModule::handle_input:**
```cpp
// Toggle 3-column mode with A + RIGHT
if (action.type == ActionType::NAVIGATE_RIGHT && s.cursorRow == 1 && s.cursorCol == 0) {
    s.sampleSettings3ColumnMode = !s.sampleSettings3ColumnMode;
    if (!s.sampleSettings3ColumnMode) s.sampleSettingsColumnIndex = 0;  // Reset column when disabled
    r.modified = true;
}
```

### 4. Update Cursor Context for 3-Column Mode (moonset.cpp)

**In MoonsetModule::cursor_context:**
- Add column navigation support in 3-column mode
- Allow D-PAD up/down to change columns when in 3-column mode
- Highlight active column indicator

## Implementation Details

### Column Layout Mapping
- **Column 0**: Rows 1-2 (ZOOM/SOURCE/RATE, PITCH/DURATION/BIT), Row 5 (RECORD)
- **Column 1**: Rows 8-11 (SELECTION, SLICE), Rows 13-14 (OPS1, OPS2)
- **Column 2**: Row 16 (EFFECT), Rows 18-19 (NAME, LOAD/SAVE/CHOPS)

### Visual Design
- Screen: 640×480
- Each column width: ~200px
- Column spacing: 10px
- Header height: 20px
- Vertical dividers: 2px between columns
- Column headers: "VIEW", "EDITING", "EFFECTS"

### Keyboard Shortcut Logic
- Trigger: A + RIGHT (cursor on ZOOM cell)
- Visual feedback: column dividers appear when enabled
- State management: reset column index when disabled

## Validation Checklist
1. **Layout Test**: Verify 3 columns display correctly with all settings
2. **Keyboard Test**: Confirm A + RIGHT toggles mode properly
3. **Cursor Test**: Validate cursor navigation in both modes
4. **Scroll Test**: Ensure single-column mode still scrolls correctly
5. **Visual Test**: Check column alignment and dividers
6. **Integration Test**: Verify with granular block and UI elements

## Risk Mitigation
1. **Layout overflow**: Ensure columns fit within screen boundaries
2. **Cursor confusion**: Add clear visual indicators for column mode
3. **Input conflicts**: Verify no conflicts with existing actions
4. **Memory impact**: Minimal additional state variables

## Code Organization
- All 3-column specific code is contained in one new function
- Backward compatible: single-column mode preserved
- No existing functions modified (except draw function for conditional display)
- State management is localized to MoonsetState

## Testing Strategy
1. Compile and build the project
2. Launch application
3. Verify existing single-column scrolling works
4. Move cursor to ZOOM cell (row 1, col 0)
5. Press A + RIGHT to enable 3-column mode
6. Verify columns display with headers and content
7. Test D-PAD navigation between columns
8. Toggle mode off with same shortcut
9. Verify scroll mode returns
10. Test with actual sample loaded
