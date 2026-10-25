# Controller diagram references

Steam Deck and PS5 drawings were supplied by the user on 2026-10-17. Only the
title rows were removed: Steam Deck 2111x805 -> 2111x718 (top 87 rows),
PS5 747x504 -> 747x462 (top 42 rows). Full widths and bottom edges are retained.
The WebP assets are lossless. Xbox is a neutral capture of the existing renderer;
the application continues to use its original Xbox SVG rather than this capture.

`src/bridge/controllerDiagramData.json` contains per-control masks extracted from
the original strokes, plus native-index and thumb geometry. A highlighted control
colors its own original pixels; labels are not drawn over Steam or rear keys.
Stick caps use the reference's pixels and move using the same native +Y-up
conversion as the original Xbox renderer.

`GamepadVisualizer.vue` retains ownership of the existing `gamepad.state`
subscription, visibility behavior and test-mode lifecycle. The diagram is a
renderer receiving that state. OEM key IDs, capture availability, input admission,
shortcuts and virtual output are not rewritten by a drawing or test interaction.
