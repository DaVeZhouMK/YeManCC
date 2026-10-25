# Keyboard preview asset research · 2026-10-17

The complete existing SVG is reused from W3C UI Events KeyboardEvent code values:
- Specification: https://www.w3.org/TR/uievents-code/
- SVG source: https://github.com/w3c/uievents-code/blob/gh-pages/images/keyboard-101-us.svg
- Raw source: https://raw.githubusercontent.com/w3c/uievents-code/gh-pages/images/keyboard-101-us.svg
- W3C Document License: https://www.w3.org/copyright/document-license/

Copyright © W3C. Reused under the W3C Document License. The original SVG is retained alongside this note. The embedded preview only removes editor metadata/style and adds a viewBox; every original path and transform is retained. Product CSS changes colors to match the controller diagrams. No keyboard geometry was drawn.

Steam's local sp.js contains keyboard glyphs/soft-keyboard components, but a complete reusable standalone keyboard SVG was not established. Steam API documentation exposes action-origin glyphs, not a reusable complete hardware-keyboard diagram. This is not evidence that Steam has no keyboard asset anywhere, and Steam assets are not claimed to be open source.

Also found: Kenney Input Prompts (CC0), https://kenney.nl/assets/input-prompts . This is suitable for individual input glyphs; the downloaded pack was verified: 125 original outline glyphs cover individual keyboard/mouse prompts (including F1–F12). These are now reused directly in the output key picker. No glyph geometry was drawn. The Wikimedia ANSI form-factor SVG is an alternative whole-layout diagram but includes size annotations and was not used.
