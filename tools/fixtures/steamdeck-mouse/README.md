# SteamDeck desktop sensitivity evidence

User-provided saved Steam layouts, collected 2026-10-05 at displayed 100% and 137%.
SteamID64 and the absolute autosave URL are redacted identically in both layouts;
other bytes, including CRLF, repeated group/preset keys and metadata are retained.
The selection index has the same desktop autosave entry in both archives.

Original archives contain 89 files each. Only `config/413080/controller_neptune.vdf`
changed: top-level `revision` 41 -> 42 and group 25 `settings/sensitivity` 100 -> 137.
Default preset binds group 25 to `right_joystick active`; mode is `joystick_mouse`.

These fixtures prove the file/path/field and direct integer encoding at both values.
They do not prove that external file writes have been loaded by a running Steam,
or that physical ROG stick behavior has been tested with the new YMCC build.
