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
const packageInfo = JSON.parse(readFileSync(resolve(root, 'package.json'), 'utf8'));
const versionInfo = JSON.parse(readFileSync(resolve(root, 'version.json'), 'utf8'));

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
]) {
  assert.ok(native.includes(token), `native updater policy missing: ${token}`);
}

assert.ok(release.includes('Version mismatch: version.json=$version, package.json=$packageVersion'));
assert.ok(workflow.includes("$expectedTag = \"v$version\""));
assert.ok(workflow.includes('Published asset SHA-256 mismatch'));
assert.ok(workflow.includes('Refusing to replace main/version.json'));
assert.ok(workflow.includes("shell: pwsh\n        run: |\n          git fetch origin main"));

console.log('updater policy self-test: PASS');
