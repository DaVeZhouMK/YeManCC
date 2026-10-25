// Static routing checks complement the executed publication regression fixture.
// No app, hardware, Steam, or installed process execution.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
const root=process.cwd(),header=fs.readFileSync('native/monitor_snapshot_contract.h','utf8'),main=fs.readFileSync('native/main.cpp','utf8'),detail=fs.readFileSync('native/monitor_detail_log.h','utf8'),checks={};
const check=(name,value)=>{checks[name]=Boolean(value);assert(value,name);};
function cut(a,b){const i=main.indexOf(a),j=main.indexOf(b,i);assert(i>=0&&j>i,a);return main.slice(i,j);}
const publish=cut('static void monitorSnapshotPublish(','static void monitorSnapshotPublishFrames('),fast=cut(main.includes('static void monitorSnapshotPublishFramesBorrowed(')?'static void monitorSnapshotPublishFramesBorrowed(':'static void monitorSnapshotPublishFrames(','static json monitorSnapshotServeFn()'),commit=cut('static void monitorWriteSnapshotGuarded(','static void monitorDeleteSnapshotGuarded('),worker=cut('static void nativeMonitorLoop()','static void nativeMonitorSetMode(');
check('independent-top-and-fps-caps-are-16-kib',header.includes('topMaxBytes = 16 * 1024')&&header.includes('fpsMaxBytes = 16 * 1024'));
check('combined-text-cap-is-24-kib',header.includes('combinedMaxBytes = 24 * 1024'));
check('full-publication-releases-or-compacts-before-lock',publish.indexOf('monitorsnapshot::retain(topText')<publish.indexOf('std::lock_guard')&&publish.indexOf('monitorsnapshot::retain(fpsText')<publish.indexOf('std::lock_guard'));
const frameCommitAt=fast.includes('g_monitorSnapshot.fpsText.swap(')?fast.indexOf('g_monitorSnapshot.fpsText.swap('):fast.indexOf('g_monitorSnapshot.fpsText =');
check('frame-combined-cap-check-is-in-cache-commit',frameCommitAt>=0&&fast.indexOf('g_monitorSnapshot.generation !=')<fast.indexOf('monitorsnapshot::textFits(g_monitorSnapshot.topText, fpsText)')&&fast.indexOf('monitorsnapshot::textFits(g_monitorSnapshot.topText, fpsText)')<frameCommitAt);
check('completed-file-content-is-checked-at-commit-before-writing',commit.indexOf('monitorsnapshot::topFits(content)')<commit.indexOf('sgWriteMonitorSnapshotDurable(')&&commit.indexOf('monitorsnapshot::fpsFits(content)')<commit.indexOf('sgWriteMonitorSnapshotTransient('));
check('top-construction-is-not-gated-by-empty-unconstructed-text',!worker.includes('topFits(monitorTopText)')&&worker.indexOf('monitorTopText = nativeMonitorTopPayloadText')<worker.indexOf('monitorWriteSnapshotGuarded(MONITOR_TOP_JSON'));
check('fps-text-is-serialized-once-and-moved-without-an-extra-copy',(worker.includes('monitorFpsText = frameOutput.packetTextView(nowMs);')||worker.includes('monitorFpsText = fpsJson.is_null() ? std::string{} : fpsJson.dump();'))&&!worker.includes('monitorFpsText=fpsText;'));
check('oversized-buffers-are-released-instead-of-cleared',header.includes('std::string{}.swap(text)')&&header.includes('text.capacity() > maxBytes'));
check('diagnostic-snapshots-use-only-high-frequency-stream',detail.includes('{"lane","high-frequency"}')&&!cut('static bool nativeLifecycleEventIsBase(const char* event) {','static void flushNativeLifecycleLogs(bool closeDetail) {').includes('monitor-data-snapshot'));
check('bounded-diagnostic-queue-and-file-caps-remain',detail.includes('recordBytes=32*1024')&&detail.includes('queueBytes=256*1024')&&detail.includes('fileBytes=2*1024*1024'));
check('fast-loop-never-logs-snapshots',!worker.slice(worker.indexOf('if (fps && game.pid && g_monitorFastFrames')).includes('monitorRecordDetailedSnapshot('));
// Export now names diagnostic raw bytes separately from the general snapshot
// payload cap. Verify each ordered binding in the actual manifest expression;
// do not require the obsolete single hard-coded 24 KiB export string.
check('diagnostic-raw-text-cap-is-12-kib',detail.includes('rawSnapshotBytes=12*1024'));
const exportCaps=cut('                manifest << "monitorSnapshots=', '                manifest << "monitorExportSummaryMaxBytes=');
const exportBindings=['monitor-detail.log;high-frequency-gate;max1Hz;payloadMax',
 'ymcc::monitordetail::rawSnapshotBytes','";snapshotPayloadMax" << ymcc::monitorsnapshot::combinedMaxBytes',
 '";recordMax" << ymcc::monitordetail::Limits{}.recordBytes',
 '";queueMax" << ymcc::monitordetail::Limits{}.queueBytes',
 '";queueRowsMax" << ymcc::monitordetail::Limits{}.queueRows',
 '";fileMax" << ymcc::monitordetail::Limits{}.fileBytes',';retainedFiles2'];
let exportAt=-1;
check('export-retains-explicit-snapshot-caps',exportBindings.every(binding=>{
 const next=exportCaps.indexOf(binding,exportAt+1);if(next<0)return false;exportAt=next;return true;
}));
console.log(JSON.stringify({ok:true,checks,scope:'Static routing only. Actual production boundary behavior is covered separately by monitor_snapshot_publication_selftest.mjs.'},null,2));
