# Same-frame Windows source owner -> actual Host codec

Run `node tools/windows_owner_host_wire_selftest.mjs`. The fixture composes retained current native source-owner/processing bodies, the actual existing pinned GamepadMotion DLL, actual calibrated/gravity readback and Madgwick pose, exact final source guard, and the production Native frame builder/serializer. Each model alias exercises default/private A/private B/calibration restore/default return, old-source rejection and source-independent neutral release. The resulting same-frame values are parsed and encoded using the complete current InputHost sources through the existing inert HMProfile boundary.

The SDK, message scheduling, physical pad availability, initialization-valid gate, fixed 8 ms delta and Host target identity are controlled. No whole BUS, normal application/Host session, pipe, virtual target, device changes or game is executed. Do not describe offline Host codec results as real submission receipts. Historical MSI SensorAPI numbers replayed into declared private WinRT recipes do not establish API-coordinate equivalence.

Semantic controls: `--drop-readback`, `--wire-calibrated-as-raw`, `--omit-final-source-guard`. Each uses isolated generated code. Production and previous evidence are preserved.
