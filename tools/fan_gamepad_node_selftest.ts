import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const root = process.cwd();
const engine = readFileSync(join(root, 'src/gamepad/engine.ts'), 'utf8');
const fanView = readFileSync(join(root, 'src/views/FanView.vue'), 'utf8');
const native = readFileSync(join(root, 'native/main.cpp'), 'utf8');

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

assert(engine.includes('[data-gp-inline-edit-scope="fan-curve-node"].edit-armed'),
  'engine does not recognize the synchronously armed fan node');
assert(engine.includes("new CustomEvent('gp:spatial-edit'"),
  'engine does not dispatch fan node spatial edits');
assert(fanView.includes('syncGraphNodeEditMarker(index, true'),
  'FanView does not synchronously arm the selected node');
assert(fanView.includes('@gp:spatial-edit="onGraphSpatialEdit(index, $event)"'),
  'FanView has no spatial edit handler');
assert(native.includes('g_uiFanNodeEditActive ? GP_UI_FAN_NODE_REPEAT_MS'),
  'native fan-node repeat cadence is not scoped to fan editing');

console.log('fan gamepad node selftest: 5/5 passed');
