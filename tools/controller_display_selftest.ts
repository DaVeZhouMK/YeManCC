import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { EMPTY_GAMEPAD_PRESENTATION, reduceGamepadPresentation } from '../src/bridge/gamepadPresentation';
const root = process.cwd();
let checks = 0;
const check = (value: boolean, message: string) => { assert(value, message); checks++; };
const source = fs.readFileSync(path.join(root, 'src/components/GamepadVisualizer.vue'), 'utf8').replace(/\r\n/g, '\n');
const geometry = source.match(/const STICK_L = [\s\S]*?(?=function isMapped\()/)?.[0];
assert(geometry, 'real visualizer geometry is present');
const calculate = new Function('axes', 'computed', geometry + '\nreturn {stickL,stickR,stickLPushed,stickRPushed};');
function draw(axes: readonly number[]) {
  return calculate({ value: axes }, (fn: () => unknown) => fn()) as {
    stickL: { x: number; y: number }; stickR: { x: number; y: number };
    stickLPushed: boolean; stickRPushed: boolean;
  };
}
const neutral = draw([]);
for (const y of [-1, -0.6, -0.001, 0, 0.001, 0.6, 1]) {
  const axes = [0.25, y, -0.25, y];
  const input = { connected: true, buttons: [], axes };
  const state = reduceGamepadPresentation(EMPTY_GAMEPAD_PRESENTATION, input);
  const output = draw(state.axes);
  check(output.stickL.y === neutral.stickL.y - y * 10, 'left preview positive Y is upward');
  check(output.stickR.y === neutral.stickR.y - y * 10, 'right preview positive Y is upward');
  check(output.stickL.x === neutral.stickL.x + 2.5 && output.stickR.x === neutral.stickR.x - 2.5, 'X direction and precision are unchanged');
  check(output.stickLPushed === (Math.hypot(0.25,y) > 0.45) && output.stickRPushed === (Math.hypot(-0.25,y) > 0.45), 'live displacement threshold unchanged');
  assert.deepEqual(state.axes, axes); assert.deepEqual(input.axes, axes); checks += 2;
}
const independent = draw([0, 1, 0, -1]);
check(independent.stickL.y < neutral.stickL.y && independent.stickR.y > neutral.stickR.y, 'two sticks use their own Y axes');
const page = fs.readFileSync(path.join(root, 'src/views/GyroMotionView.vue'), 'utf8').replace(/\r\n/g, '\n');
const options = page.match(/const stickOptions = (\[[\s\S]*?\]);/)?.[1];
assert(options, 'gyro stick options exist');
assert.deepEqual(new Function('return ' + options)(), [
  { value: 'left', label: '左摇杆' }, { value: 'right', label: '右摇杆' },
]); checks++;
check(page.includes("const outputStick = ref<'left' | 'right'>('right');"), 'initial gyro target remains right');
check(page.includes('v-model="outputStick" :options="stickOptions"'), 'UI retains original data model binding');
const native = fs.readFileSync(path.join(root, 'native/main.cpp'), 'utf8').replace(/\r\n/g, '\n');
check(native.includes('{"axes", {normalizeAxis(pad.sThumbLX), normalizeAxis(pad.sThumbLY),\n                   normalizeAxis(pad.sThumbRX), normalizeAxis(pad.sThumbRY)}}'), 'native input axes retain original signs');
const snapshot = native.match(/static json virtualOutputRuntimeSnapshot\(bool sourceLive\) \{[\s\S]*?(?=\nstatic void gamepadReadStateUnlocked)/)?.[0];
assert(snapshot, 'virtual runtime publisher exists');
check(snapshot.includes('g_steamSettingsObserver.steamPresence()') && snapshot.includes('{"steamRunning", steamPresence < 0 ? json(nullptr) : json(steamPresence == 1)}'), 'runtime publishes cached tri-state Steam presence');
check(!/probeSteamPresence|sofSteamRunning|CreateToolhelp32Snapshot/.test(snapshot), 'no process enumeration on gamepad state path');
console.log(`CONTROLLER_DISPLAY_PASS checks=${checks} hardwareOperations=0 settingsWrites=0`);
