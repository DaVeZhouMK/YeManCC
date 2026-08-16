import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  compareVersions,
  isValidSha256,
  parseStrictVersion,
  validateUpdateManifest,
} from '../src/bridge/yeman';

const goodSha = 'A'.repeat(64);

assert.deepEqual(parseStrictVersion('0.0.7'), [0, 0, 7]);
for (const bad of ['', '1', '1.2', '1.2.3.4', 'v1.2.3', '01.2.3', '1.2.x', ' 1.2.3', '2147483648.0.0']) {
  assert.throws(() => parseStrictVersion(bad), `strict version must reject ${JSON.stringify(bad)}`);
}
assert.equal(compareVersions('0.0.8', '0.0.7'), 1);
assert.equal(compareVersions('0.0.7', '0.0.7'), 0);
assert.equal(compareVersions('0.0.6', '0.0.7'), -1);

assert.equal(isValidSha256(goodSha), true);
assert.equal(isValidSha256('a'.repeat(63)), false);
assert.equal(isValidSha256('g'.repeat(64)), false);
assert.equal(validateUpdateManifest({ version: '0.0.8', sha256: goodSha }).sha256, goodSha);
assert.throws(() => validateUpdateManifest({ version: '0.0.8', sha256: '' }));
assert.throws(() => validateUpdateManifest({ version: '0.0.8' }));

const root = process.cwd();
const native = readFileSync(resolve(root, 'native/main.cpp'), 'utf8');
const release = readFileSync(resolve(root, 'tools/package-release.ps1'), 'utf8');
const workflow = readFileSync(resolve(root, '.github/workflows/release.yml'), 'utf8');
const yeman = readFileSync(resolve(root, 'src/bridge/yeman.ts'), 'utf8');
const updateManager = readFileSync(resolve(root, 'src/bridge/updateManager.ts'), 'utf8');
const settings = readFileSync(resolve(root, 'src/views/SettingsView.vue'), 'utf8');
const packageInfo = JSON.parse(readFileSync(resolve(root, 'package.json'), 'utf8').replace(/^\uFEFF/, ''));
const versionInfo = JSON.parse(readFileSync(resolve(root, 'version.json'), 'utf8').replace(/^\uFEFF/, ''));

assert.equal(packageInfo.version, versionInfo.version, 'package.json and version.json must match');

for (const token of [
  'isStrictUpdateSha256',
  'requireNewerUpdateVersion(version)',
  'sha256File(zip)',
  'Update package version does not match requested version',
  'updatePackageLayoutIsSafe(staging)',
  "$parentExitDeadline = (Get-Date).AddSeconds($parentExitTimeoutSeconds)",
  "throw ('parent YeManCC process did not exit within '",
  'Register-TreeForRollback',
  'Restore-OrdinaryFiles',
  '$rollbackAddedFiles',
  'Remove-Item -LiteralPath $rollbackRoot -Recurse -Force',
  'elseif ($rollbackSucceeded)',
  'UPDATE_HTTP_IO_TIMEOUT_MS = 120000',
  'deadline == 0 ? UPDATE_HTTP_IO_TIMEOUT_MS : DEFAULT_HTTP_TIMEOUT_MS',
  'UPDATE_RETRY_INTERVAL_MS = 5000',
  'downloadFileAttempt(url, part',
  'Range: bytes=',
  'If-Range:',
  'WINHTTP_QUERY_CONTENT_RANGE',
  'package.zip.part',
  'package.zip.part.json',
  'MoveFileExW(part.c_str(), dest.c_str()',
  'receivedLength >= expectedTotal',
  'result.restartRequired',
  '{"stage", "install"}',
  'Write-Utf8Atomic',
  '-WorkingDirectory $exeDir',
  '$handshakeTimeoutSeconds = 180',
  '$parentExitTimeoutSeconds = 180',
  '$recoveryProcess = Start-Process',
  'Failed to clear stale update staging',
  'helperScriptWritten',
  '下载中断，5秒后从 ',
  '下载失败，5秒后重新尝试第 ',
  '下载失败，已尝试 ',
  '{"retryInSeconds", UPDATE_RETRY_INTERVAL_MS / 1000}',
  '{"lastError", lastFailure.error}',
]) {
  assert.ok(native.includes(token), `native updater policy missing: ${token}`);
}
for (const token of [
  'UPDATE_RETRY_WINDOW_MS',
  'retryDeadline',
  'remainingAfterWaitMs',
  '5 分钟内仍未成功',
]) {
  assert.equal(native.includes(token), false, `obsolete fixed retry policy remains: ${token}`);
}
assert.ok(yeman.includes('{ timeoutMs: 0 }'), 'download IPC must not impose a wall-clock timeout');
assert.ok(updateManager.includes("['available', 'failed', 'interrupted']"), 'failed updates need a direct resume path');
assert.ok(updateManager.includes('retryInstall'), 'installation failures need an install-only retry path');
assert.ok(updateManager.includes('restoreSavedUpdateInfo'), 'restart must restore failed update metadata');
assert.ok(settings.includes("['available', 'failed', 'interrupted'].includes(updateSnapshot.phase)"), 'UI must keep the resume action visible');
assert.ok(settings.includes("updateSnapshot.stage === 'install' ? '重试安装'"), 'UI must distinguish install retry from download retry');

assert.ok(native.includes("$playerBlacklistPath = Join-Path $pcDir 'Sleep\\\\player-blacklist.txt'"));
assert.ok(native.includes("$playerBlacklistRollback = Join-Path $rollbackFiles 'PowerControl\\\\Sleep\\\\player-blacklist.txt'"));
assert.ok(native.includes('player blacklist preservation'));
assert.ok(native.includes("$systemBlacklistSource = Join-Path $powerControlSource 'Sleep\\\\system-blacklist.txt'"));
assert.ok(native.includes('system blacklist update'));
assert.equal(native.includes('SG_GAME_LEGACY_BLACKLIST'), false, 'legacy exclude.txt constant must be removed');
assert.equal(native.includes('sgMigrateLegacyGameBlacklist'), false, 'legacy exclude.txt migration must be removed');

const updaterStart = native.indexOf('ipc_on("app.downloadUpdate"');
const updaterEnd = native.indexOf('ipc_on("app.installUpdate"', updaterStart);
assert.ok(updaterStart >= 0 && updaterEnd > updaterStart, 'native download updater block must exist');
const updater = native.slice(updaterStart, updaterEnd);
assert.ok(
  updater.indexOf('downloadFileAttempt(url, part') < updater.indexOf('const auto got = sha256File(part)'),
  'each downloaded package must be checksummed inside the retry loop',
);
assert.ok(
  updater.includes('{"phase", "downloading"}') && updater.includes('{"message", retryMessage}'),
  'retry wait must remain in the downloading phase and report every retry',
);

assert.ok(release.includes('Version mismatch: version.json=$version, package.json=$packageVersion'));
assert.ok(workflow.includes("$expectedTag = \"v$version\""));
assert.ok(workflow.includes('Published asset SHA-256 mismatch'));
assert.ok(workflow.includes('Refusing to replace main/version.json'));
assert.ok(workflow.includes("shell: pwsh\n        run: |\n          git fetch origin main"));

console.log('updater policy self-test: PASS');
