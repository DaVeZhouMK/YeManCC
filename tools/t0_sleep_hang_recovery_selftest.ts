import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const native = readFileSync(resolve(process.cwd(), 'native/main.cpp'), 'utf8');

function requireNative(token: string): void {
  assert.ok(native.includes(token), `missing T0 recovery guard: ${token}`);
}

function section(start: string, end: string): string {
  const begin = native.indexOf(start);
  assert.ok(begin >= 0, `missing section start: ${start}`);
  const finish = native.indexOf(end, begin + start.length);
  assert.ok(finish > begin, `missing section end: ${end}`);
  return native.slice(begin, finish);
}

// Static evidence: lifecycle logging must be independent of WebView/debug tracing,
// and a successful new launch must still run the established marker recovery path.
requireNative('static void appendNativeLifecycleLog(const char* event, json detail = json::object());');
requireNative('native-lifecycle.log');
requireNative('appendNativeLifecycleLog("sleep-orphan-recovery-start")');
requireNative('const SgResumeResult recovered = sgResumeTrackedAll();');
requireNative('appendNativeLifecycleLog("sleep-orphan-recovery-complete"');

// Static evidence: never treat the first 0-HWND observation as a zombie.
requireNative('waitForExistingInstanceWindow(title, 1500, existing)');
requireNative('Sleep(100);');

// Static evidence: stale-process termination is constrained to exact image + session
// and revalidates windowlessness immediately before TerminateProcess.
const discovery = section(
  'static std::vector<DWORD> findWindowlessSameImageInstances(',
  'static bool waitForExistingInstanceWindow(',
);
assert.ok(discovery.includes('ProcessIdToSessionId'), 'candidate discovery must be session scoped');
assert.ok(discovery.includes('queryFullProcessImagePath'), 'candidate discovery must compare full image path');
assert.ok(discovery.includes('_wcsicmp(image.c_str(), currentImage.c_str()) != 0'),
  'candidate discovery must require exact executable path');
assert.ok(discovery.includes('findExistingInstanceWindowForPid(pid, title)'),
  'candidate discovery must exclude a real hidden/tray main window');

const terminator = section(
  'static bool terminateConfirmedWindowlessInstance(',
  'static std::wstring readEnvironmentString(',
);
assert.ok(terminator.includes('ProcessIdToSessionId(pid, &candidateSession)'),
  'termination must revalidate session');
assert.ok(terminator.includes('queryFullProcessImagePath(pid, candidateImage)'),
  'termination must revalidate executable path');
assert.ok(terminator.includes('findExistingInstanceWindowForPid(pid, title)) return false'),
  'termination must revalidate that the candidate is still windowless');
assert.ok(terminator.includes('TerminateProcess(process, 0)'),
  'termination is available only after revalidation');

const takeover = section(
  '// T0 sleep-hang repair: normal single-instance behavior remains unchanged,',
  'appendNativeLifecycleLog("boot-single-instance-acquired")',
);
assert.ok(takeover.includes('if (candidates.size() == 1)'),
  'takeover must require exactly one candidate');
assert.ok(takeover.includes('MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2'),
  'takeover must require explicit opt-in confirmation');
assert.ok(takeover.includes('else if (candidates.size() > 1)'),
  'ambiguous candidates must have an explicit no-kill path');
assert.ok(takeover.includes('single-instance-windowless-ambiguous'),
  'ambiguous candidates must be logged');
assert.ok(!takeover.includes('for (const DWORD pid : candidates)'),
  'one confirmation must never terminate multiple candidates');

// Manual state model. This models only the new admission/termination gate; it does
// not alter the existing sleep pause state machine.
type Candidate = {
  sameSession: boolean;
  sameImage: boolean;
  hasMainWindow: boolean;
  terminateSucceeds: boolean;
};
type Decision = 'activate' | 'wait' | 'takeover' | 'decline' | 'ambiguous' | 'exit';

function modelTakeover(args: {
  windowAppearsDuringGrace: boolean;
  candidates: Candidate[];
  confirmed: boolean;
}): Decision {
  if (args.windowAppearsDuringGrace) return 'activate';
  const safe = args.candidates.filter((c) => c.sameSession && c.sameImage && !c.hasMainWindow);
  if (safe.length === 0) return 'exit';
  if (safe.length > 1) return 'ambiguous';
  if (!args.confirmed) return 'decline';
  return safe[0].terminateSucceeds ? 'takeover' : 'exit';
}

const stale: Candidate = {
  sameSession: true, sameImage: true, hasMainWindow: false, terminateSucceeds: true,
};
assert.equal(modelTakeover({ windowAppearsDuringGrace: true, candidates: [stale], confirmed: true }), 'activate',
  'just-starting instance wins during the grace window and is never terminated');
assert.equal(modelTakeover({ windowAppearsDuringGrace: false, candidates: [stale], confirmed: true }), 'takeover',
  'one confirmed, revalidated zombie can release the mutex');
assert.equal(modelTakeover({ windowAppearsDuringGrace: false, candidates: [stale], confirmed: false }), 'decline',
  'declining confirmation must leave the other process untouched');
assert.equal(modelTakeover({ windowAppearsDuringGrace: false, candidates: [], confirmed: true }), 'exit',
  'no identifiable candidate must never be terminated');
assert.equal(modelTakeover({ windowAppearsDuringGrace: false, candidates: [stale, stale], confirmed: true }), 'ambiguous',
  'multiple matching candidates must never be terminated');
assert.equal(modelTakeover({
  windowAppearsDuringGrace: false,
  candidates: [{ ...stale, sameImage: false }],
  confirmed: true,
}), 'exit', 'a different executable path must never be terminated');
assert.equal(modelTakeover({
  windowAppearsDuringGrace: false,
  candidates: [{ ...stale, sameSession: false }],
  confirmed: true,
}), 'exit', 'a different session must never be terminated');
assert.equal(modelTakeover({
  windowAppearsDuringGrace: false,
  candidates: [{ ...stale, hasMainWindow: true }],
  confirmed: true,
}), 'exit', 'a tray-hidden/real main window must never be terminated');
assert.equal(modelTakeover({
  windowAppearsDuringGrace: false,
  candidates: [{ ...stale, terminateSucceeds: false }],
  confirmed: true,
}), 'exit', 'failed termination must not pretend takeover succeeded');

console.log('T0 sleep-hang recovery self-test: PASS (static guards + 9 scenario checks)');