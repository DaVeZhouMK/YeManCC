'use strict';
const assert = require('node:assert/strict');
const h = require('./msi_focus_capture_segments_20261012_fa1322.cjs');
let cases = 0;
function check(name, f) { f(); ++cases; console.log('PASS', name); }
const m = h.manifest('processId=16908\nexportedAtLocal=2026-10-12T13:13:49\nnativeSha256=' + 'a'.repeat(64));
const row = (pid, time, event='event') => ({pid,time,event});
const parsed = h.jsonLines('\uFEFF' + [
 row(9844,'2026-10-12T13:06:03','boot-single-instance-acquired'),
 row(16908,'2026-10-11T08:00:00','boot-single-instance-acquired'),
 row(16908,'2026-10-12T13:12:29.063','boot-single-instance-acquired'),
 row(9844,'2026-10-12T13:10:41.661','session-clean-exit'),
 row(16908,'2026-10-12T13:13:42.222','msi-native-foreground-isolation'),
 {time:'2026-10-12T13:13:42.223',event:'focus-foreground-changed'},
 {kind:'sample',t:123}
].map(JSON.stringify).join('\n') + '\n{unfinished\n[]\n');
const w = h.currentWindow(parsed.rows,m);
check('manifest-PID-and-latest-retained-boot-required',()=>assert.equal(w.start,'2026-10-12T13:12:29.063'));
check('old-PID-in-after-file-is-history',()=>assert.equal(h.attribution(parsed.rows[0].row,w),'prior-or-other-explicit-PID'));
check('reused-manifest-PID-old-session-is-not-current',()=>assert.equal(h.attribution(parsed.rows[1].row,w),'same-PID-outside-current-session'));
check('PIDless-wall-time-does-not-gain-PID-proof',()=>assert.equal(h.attribution(parsed.rows[5].row,w),'current-time-window-only-NOT-PID-proof'));
check('PIDless-tick-only-capture-not-joined-to-wall-time',()=>assert.equal(h.attribution(parsed.rows[6].row,w),'unattributed-no-PID'));
check('malformed-tail-and-array-rejected-not-invented',()=>assert.deepEqual(parsed.rejectedLines,[8,9]));
check('second-precision-export-includes-final-second',()=>assert.equal(h.attribution(row(16908,'2026-10-12T13:13:49.999'),w),'current-explicit-PID-and-time'));
check('post-export-rows-not-current',()=>assert.equal(h.attribution(row(16908,'2026-10-12T13:13:50'),w),'same-PID-outside-current-session'));
check('no-boot-boundary-stays-explicitly-incomplete',()=>assert.equal(h.attribution(row(16908,'2026-10-12T13:13:42'),h.currentWindow([],m)),'manifest-PID-without-retained-session-boundary'));
check('current-event-counts-exclude-old-and-PIDless-records',()=>assert.deepEqual(h.summarize(parsed,w).currentExplicitPidEvents,{'boot-single-instance-acquired':1,'msi-native-foreground-isolation':1}));
const steam = h.steamSplit('[2026-10-12 13:11:41] Client version: 1\n[2026-10-12 13:11:41] Local Device Found\n  type: 0db0 1901\n  path: sdl://1\n[2026-10-12 13:11:41] !! Steam controller device opened for index 0.\n[2026-10-12 13:12:33] Controller PollState Changed from 2 to 1',w);
check('Steam-prelaunch-open-not-mixed-with-export-effects',()=>assert.equal(steam.preLaunch.length,4));
check('untimestamped-device-block-inherits-only-Steam-time',()=>assert.equal(steam.preLaunch.find(x=>x.text.includes('path:')).timestampExplicit,false));
check('export-session-Steam-churn-split',()=>assert.equal(steam.duringExportSession.length,1));
check('Steam-start-marker-not-called-reboot-or-process-absence',()=>assert.match(steam.preLaunchScope,/not-proof-of-YMCC-process-absence-or-input-delivery/));
check('missing-manifest-pid-fails-closed',()=>assert.throws(()=>h.manifest('exportedAtLocal=2026-10-12T13:13:49\nnativeSha256='+'a'.repeat(64))));
check('missing-binary-identity-fails-closed',()=>assert.throws(()=>h.manifest('processId=16908\nexportedAtLocal=2026-10-12T13:13:49')));
console.log('CAPTURE_SEGMENT_SELFTEST cases='+cases+' failures=0 fake-only');
