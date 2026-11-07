# Pixel editor source fixture

`pixel_editor_tests.cpp` tests the copy-owned model without showing a window:
clipped continuous strokes, grouped undo/redo, right-erase-equivalent alpha,
center resize/crop, bounded history, malformed canvas normalization and source
copy isolation. Link it with `src/pixel_editor.cpp`, User32 and Gdi32.

`pixel_editor_snapshot.cpp` includes the editor implementation in its own
hidden test process. It renders example 20×20 and 100×100 masks at 100% and 150%
UI scale. The fixture does not invoke the modal API, activate a user app,
send input, install hooks, register hotkeys or save configuration. Its sample
art is test data, not a capture of the user's current design.

The fixture directly uses the editor's painting routine and native child-control
WM_PRINT output. It verifies layout and drawing, not desktop focus behavior,
modal interaction, real input capture or live DPI-change events.
The v1.5 editor uses genuine native push buttons, radio buttons, checkboxes,
group boxes, edit controls and static labels/bitmap preview. Fixture assertions
verify those styles, absence of owner-drawn buttons, radio selection and bitmap
attachment. Only the pixel canvas/checkerboard/mask image is custom rendered;
font and surrounding colors come from system message-font/color settings.

Build with the same Unicode/Windows10 flags as the application, using
`tests/pixel_editor_snapshot.cpp` alone and User32/Gdi32. Do not also compile
`src/pixel_editor.cpp` for this source-inclusion fixture. Run from the project
directory; generated BMPs are placed under `build`.
