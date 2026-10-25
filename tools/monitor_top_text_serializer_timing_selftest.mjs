// Default-allocator cycle timing of the matching verified serializer fixture.
import path from 'node:path';
process.env.YMCC_TOP_STRING_OUT = process.env.YMCC_TOP_SERIALIZER_OUT || process.env.YMCC_TOP_STRING_OUT || path.resolve('../../../Build/Tasks/YMCC-Steam-HUD-Probe/top-text-serializer-20261017');
await import('./monitor_top_string_timing_selftest.mjs');
