import fs from 'node:fs';
import path from 'node:path';

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(`CUSTOM_STEAM_LIBRARY_INPUT_ASSERT: ${message}`);
}

const root = process.cwd();
const native = fs.readFileSync(path.join(root, 'native', 'main.cpp'), 'utf8');
const bridge = fs.readFileSync(path.join(root, 'src', 'bridge', 'customSteamLibrary.ts'), 'utf8');
const engine = fs.readFileSync(path.join(root, 'src', 'gamepad', 'engine.ts'), 'utf8');

assert(native.includes('return @($definitions.ToArray())'), 'PowerShell layout roots must return a plain array');
assert(native.includes('installRoot + L"\\\\YeManCC\\\\CustomSteamLibrary"'), 'native updater canonical child path is missing');
assert(!native.includes("CustomSteamLibrary target is fixed"), 'CustomSteamLibrary target is still hard-coded to the legacy sibling');
assert(bridge.includes("C:\\\\SOFT\\\\YeMan\\\\YeManCC\\\\CustomSteamLibrary"), 'bridge canonical child path is missing');
assert(bridge.indexOf("joinWindowsPath(exeDir, 'CustomSteamLibrary\\\\CustomSteamLibrary.exe')") >= 0, 'bridge must prefer the nested child beside YeManCC.exe');
assert(bridge.includes('LEGACY_CUSTOM_STEAM_LIBRARY_ROOT'), 'bridge legacy path fallback is missing');

// The native YeManCC gamepad loop is the only arbitration owner. This test is
// intentionally a source contract check: a renderer-side gate must not be
// reintroduced as a second input state machine.
assert(native.includes('customSteamLibraryProcessMatches'), 'native child executable identification is missing');
assert(native.includes('customSteamLibraryParentOwned'), 'native parent-owner validation is missing');
assert(native.includes('customSteamLibrarySendSemanticAction'), 'native direct semantic forwarding is missing');
assert(native.includes('if (customSteamLibraryChildForeground())'), 'native child ownership branch is missing');
assert(native.includes('g_customSteamLibraryInputDeadline'), 'native launch/return deadline is missing');
assert(native.includes('gamepad.input-owner'), 'native ownership notification is missing');
assert(!native.includes('gamepadEmitUiAction("dropdown")'), 'native X button still emits dropdown');
assert(!engine.includes("if (pressed(2))"), 'renderer fallback still maps X to dropdown');
assert(!engine.includes("'dropdown'"), 'renderer still exposes dropdown as a native face-button action');
assert(!bridge.includes('customSteamLibraryInputGate'), 'renderer input gate is still imported');
assert(!bridge.includes('customSteamLibrarySessionActive'), 'renderer still decides input ownership');
assert(!engine.includes('customSteamLibrarySessionActive'), 'gamepad engine still delegates arbitration to renderer');
assert(engine.includes('nativeChildInputOwned'), 'renderer does not honor native ownership notification');
for (const action of [
  'navigate-left', 'navigate-right', 'navigate-up', 'navigate-down',
  'accept', 'back', 'tab-previous', 'tab-next', 'edit',
]) {
  assert(native.includes(`"${action}"`), `native semantic action is missing: ${action}`);
}

console.log('CUSTOM_STEAM_LIBRARY_NATIVE_ARBITRATION_SELFTEST_OK');
