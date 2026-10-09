import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  compareVersions,
  updatePackageUrl,
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

// The public 0.0.28 client requests YeManCC.zip; every later release retains it.
for (const [newer, older] of [['0.0.33','0.0.28'],['0.0.34','0.0.33'],['0.0.100','0.0.33'],['0.1.0','0.0.33'],['1.0.0','0.0.33']]) {
  assert.equal(compareVersions(newer, older), 1);
  assert.equal(compareVersions(older, newer), -1);
  assert.equal(updatePackageUrl(newer), 'https://github.com/DaVeZhouMK/YeManCC/releases/download/v'+newer+'/YeManCC.zip');
}
assert.equal(compareVersions('0.0.33','0.0.33'),0);
assert.throws(() => updatePackageUrl('../0.0.34'));

assert.equal(isValidSha256(goodSha), true);
assert.equal(isValidSha256('a'.repeat(63)), false);
assert.equal(isValidSha256('g'.repeat(64)), false);
assert.equal(validateUpdateManifest({ version: '0.0.8', sha256: goodSha }).sha256, goodSha);
assert.throws(() => validateUpdateManifest({ version: '0.0.8', sha256: '' }));
assert.throws(() => validateUpdateManifest({ version: '0.0.8' }));

const root = process.cwd();
const native = readFileSync(resolve(root, 'native/main.cpp'), 'utf8');
const release = readFileSync(resolve(root, 'tools/package-release.ps1'), 'utf8');
const fanHostInstaller = readFileSync(resolve(root, 'PowerControl/fan-host/install-fan-host-payload.ps1'), 'utf8');
const fanHostVerifier = readFileSync(resolve(root, 'tools/verify-r5v9-fan-host-payload.ps1'), 'utf8');
const fanHostPayloadManifest = JSON.parse(readFileSync(resolve(root, 'PowerControl/fan-host/YeManFanHost.payload.json'), 'utf8').replace(/^\uFEFF/, ''));
const workflow = readFileSync(resolve(root, '.github/workflows/release.yml'), 'utf8');
assert.ok(workflow.includes('Release/Packages/YeManCC.zip'));
assert.ok(workflow.includes("$_.name -eq 'YeManCC-Complete.zip'"), 'CI must reject the incorrect alternate asset name');
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
  'update-manifest.json',
  'Get-UpdateLayoutRoots',
  'return @($definitions.ToArray())',
  'YeManCC\\\\CustomSteamLibrary',
  'Register-AdditionalLayoutRootsForRollback',
  'Copy-AdditionalLayoutRootsChecked',
  'CustomSteamLibrary',
  '$customSteamLibrarySource',
  'Get-CustomManagedPaths',
  'Register-CustomManagedFilesForRollback',
  'Copy-CustomManagedFilesChecked',
  'Get-GreenChildManagedPaths',
  'Register-GreenChildManagedFiles',
  'Copy-GreenChildManagedFilesChecked',
  'Invoke-CustomSteamLibraryHealthCheck',
  'Close-CustomSteamLibraryHealthProcess',
  'customHealthTimeoutSeconds = 60',
  '--update-health-only',
  'CustomSteamLibrary startup health handshake timed out',
  'dataRootWritable',
  'Test-CustomSteamLibraryDataRoot',
  'required root has no definition',
  'validateZipArchiveEntries',
  'g_updateOperationMtx',
  'Stop-ResidentProcesses',
  'Get-ResidentUpdaterProcesses',
  'Stop-Process -Id $process.Id -Force',
  'could not be force-closed before update',
  'YeManFanHost',
  'YeManInputHost',
  'YeManRecoveryService',
  'YeManTdpCtl',
  'param([switch]$Elevated)',
  'WindowsBuiltInRole',
  'update helper must run elevated to force-close resident processes',
  '$gracefulStopSeconds = 30',
  '$fanHostPackagePresent',
  'replace',
  'unable to verify the CustomSteamLibrary process executable path',
  'declared update root process path',
  'phase\\":\\"committed',
  '$indexed = @($manifest.fileIndex)',
  'CustomSteamLibrary fileIndex SHA256 mismatch',
  'Stop-CustomSteamLibraryProcesses',
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
  'response.bodyLengthKnown && receivedLength >= response.bodyEnd',
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
assert.ok(
  native.indexOf('Invoke-CustomSteamLibraryHealthCheck') < native.indexOf("Start-Process -FilePath $exePath"),
  'CustomSteamLibrary health must be verified before YeManCC commit launch',
);

assert.ok(release.includes('Version mismatch: version.json=$version, package.json=$packageVersion'));
assert.ok(release.includes('CustomSteamLibrary') && release.includes('Update ZIP roots do not match update-manifest.json'));
assert.ok(release.includes('updateLayoutManifest') && release.includes('requiredUpdateRoots'));
assert.ok(release.includes('reject-unless-declared'));
assert.ok(release.includes('$includeFanHostInFullTestPackage = $true'));
assert.ok(release.includes("$fanHostUpdatePolicy = 'preserve-existing'"));
assert.ok(release.includes("$fanHostV2UpdatePolicy = 'replace'"));
assert.ok(release.includes('Legacy fan-host must not ship'));
assert.ok(native.includes('Fan Host whole-directory rollback'));
assert.ok(native.includes('Fan Host V2 directory binding is missing or invalid'));
assert.ok(release.includes('Full release package must contain PowerControl/fan-host-v2'));
assert.ok(release.includes('YeManCC.zip must not contain PowerControl/fan-host-v2 entries'));
assert.ok(release.includes('YeManCC.zip must not contain PowerControl/fan-host-quarantine entries'));
assert.ok(release.includes("^handheldcompanion-runtime\\\\'"), 'HC shared runtime must be included in release templates');
assert.ok(
  release.includes('YeManCC.zip generator must export PowerControl/feature-assets/virtual-gamepad and gyro-motion'),
  'complete test packages must include both GyroVirtual feature-asset directories',
);
assert.ok(release.includes("gyroVirtualAssets = 'include'"), 'single complete package ships the gyro/virtual assets to the updater');
assert.ok(!release.includes('Fan Host V2 is missing from release staging area'));
assert.ok(fanHostInstaller.includes('$manifest.schemaVersion -ne 2'));
assert.ok(!fanHostInstaller.includes('expectedV2ManifestSha256'));
assert.ok(fanHostInstaller.includes('$runtimeManifest.files'));
// 920 §8.0-D B5 remap: this line used to pin the literal Host DLL hash
// 6D9547F2…, a superseded value that the 920 §8.0-C authorised payload re-base
// legitimately replaced. Intent preserved and made re-base-proof: the verifier
// must still pin the Host DLL by an explicit SHA-256 literal, and that literal
// must equal the DLL the frozen payload manifest declares — it must not merely
// re-read the manifest it is verifying.
const verifierDllPin = /'YeManFanHost\.dll'\s*=\s*'([0-9A-Fa-f]{64})'/.exec(fanHostVerifier)?.[1];
const payloadDllEntry = (fanHostPayloadManifest.files as Array<{ path: string; sha256: string }> | undefined)
  ?.find((entry) => entry.path === 'YeManFanHost.dll');
assert.ok(verifierDllPin && payloadDllEntry && verifierDllPin.toUpperCase() === payloadDllEntry.sha256.toUpperCase(),
  'payload verifier must pin the Host DLL with an explicit literal equal to the manifest-declared DLL');
assert.ok(native.includes('Assert-FanHostV2Payload'));
assert.ok(native.includes('expectedFanHostV2ManifestFileCount = 9'));
assert.ok(native.includes('$binding = $layout.rules.fanHostPayload'));
assert.ok(native.includes('Fan Host incoming payload binding is missing or invalid'));
assert.ok(native.includes('HC runtime manifest binding SHA256 mismatch'));
assert.ok(release.includes('fanHostPayload = [ordered]@{'));
assert.ok(release.includes('runtimeManifestSha256 = Get-Sha256'));
assert.ok(native.includes('expectedFanHostV2LhmSha256'));
assert.ok(workflow.includes("$expectedTag = \"v$version\""));
assert.ok(workflow.includes('$requiredRoots = @($layoutManifest.requiredRoots'));
assert.ok(workflow.includes('foreach ($definition in @($layoutManifest.roots))'));
assert.ok(workflow.includes('Published asset SHA-256 mismatch'));
assert.ok(workflow.includes('Refusing to replace main/version.json'));
assert.ok(workflow.includes('name: Commit version manifest back to main via API'));
assert.ok(workflow.includes('gh api --method PUT'));

// These anchors bind the native fixture-tested resume policy to the actual updater entry.
const resumePolicy = readFileSync(resolve('native/update_download_resume.h'), 'utf8');
for (const token of ['parseUnsigned64', 'parseContentRange', 'resumeValidator', 'Content-Length does not match Content-Range', 'Resume entity validator changed']) {
  assert.ok(resumePolicy.includes(token), `resume policy missing: ${token}`);
}
for (const token of ['#include "update_download_resume.h"', 'responseReady(result)', 'persistResumeMetadata();', 'Cannot save download resume metadata', 'knownComplete', 'completedHash', 'acceptedResumeBytes', 'Cannot hash the cached partial package']) {
  assert.ok(native.includes(token), `native resume integration missing: ${token}`);
}
assert.ok(!native.includes('WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER'), 'download length must not be limited to DWORD');
const updateManagerSource = readFileSync(resolve('src/bridge/updateManager.ts'), 'utf8');
assert.ok(updateManagerSource.includes("['checking', 'downloading', 'validating', 'installing'].includes(updateSnapshot.phase)"), 'large-file hashing must keep the update operation busy');
console.log('updater policy self-test: PASS');
