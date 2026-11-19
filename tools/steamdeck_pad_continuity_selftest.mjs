import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const tools = path.dirname(fileURLToPath(import.meta.url));
const repo = path.dirname(tools);
assert(process.argv.length === 3 || process.argv.length === 4, 'Use: node steamdeck_pad_continuity_selftest.mjs <fresh-output-dir> [dotnet-exe]');
const root = path.resolve(process.argv[2]);
const dotnet = process.argv[3] ?? 'dotnet';
fs.mkdirSync(root); // Never reuse an existing result directory.
const wire = path.join(root, 'wire');
const save = (name, value) => fs.writeFileSync(path.join(root, name), value, { flag: 'wx' });
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const run = (name, exe, args, env = process.env, cwd = root, expectedStatus = 0) => {
    const result = spawnSync(exe, args, { env, cwd, encoding: 'utf8', windowsHide: true, timeout: 120_000, maxBuffer: 12 * 1024 * 1024 });
    const output = (result.stdout ?? '') + (result.stderr ?? '') + (result.error ? `\n${result.error.message}\n` : '');
    save(`${name}.log`, output);
    assert.equal(result.status, expectedStatus, `${name}: unexpected exit code; see ${path.join(root, `${name}.log`)}`);
    assert.equal(result.error, undefined, `${name}: child did not finish normally`);
    return output;
};

run('prepare-serializers', process.execPath, [path.join(tools, 'virtual_typed_submit_selftest.mjs'), wire, '--prepare-only']);
const production = fs.readFileSync(path.join(wire, 'production.inc'), 'utf8');
const open = '\nnamespace ymcc::screenpads{using Json=json;struct Contact';
const close = '\nstatic const char* kInputHostProtocolConfigHash=';
const start = production.indexOf(open), end = production.indexOf(close, start);
assert(start >= 0 && end > start && production.slice(start, end).includes('inline Json snapshotJson('), 'Expected fixture-only screenpad shim');
save('production-real-pads.inc', production.slice(0, start) + production.slice(end) + '\n' +
    fs.readFileSync(path.join(wire, 'typed_submit_candidate.inc'), 'utf8'));
const pad = path.join(repo, 'native', 'screen_touchpads.h');
assert.equal(hash(fs.readFileSync(pad)), hash(fs.readFileSync(path.join(wire, 'Frozen', 'screen_touchpads.h'))));

const setup = run('msvc-environment', path.join(process.env.SystemRoot, 'System32', 'cmd.exe'), ['/d', '/c', path.join(wire, 'compiler-environment.cmd')]);
const env = { ...process.env };
const key = name => Object.keys(env).find(k => k.toLowerCase() === name.toLowerCase()) ?? name;
for (const line of setup.split(/\r?\n/)) {
    const index = line.indexOf('=');
    if (index > 0) env[key(line.slice(0, index))] = line.slice(index + 1);
}
const sdk = 'C:/Program Files (x86)/Windows Kits/10', version = '10.0.26100.0';
env[key('INCLUDE')] = ['ucrt', 'shared', 'um', 'winrt', 'cppwinrt'].map(dir => path.join(sdk, 'Include', version, dir)).concat(env[key('INCLUDE')] ?? '').join(';');
env[key('LIB')] = ['ucrt', 'um'].map(dir => path.join(sdk, 'Lib', version, dir, 'x64')).concat(env[key('LIB')] ?? '').join(';');
for (const name of ['CL', '_CL_', 'LINK', '_LINK_']) delete env[key(name)];
const exe = path.join(root, 'pad-native-fixture.exe');
const source = path.join(tools, 'steamdeck_pad_continuity_fixture.cpp');
const hostSource = path.join(tools, 'steamdeck_pad_continuity_fixture.cs');
const sourceHashes = [source, hostSource, pad].map(file => ({ path: file, sha256: hash(fs.readFileSync(file)) }));
run('build-native', path.join(env[key('VCToolsInstallDir')], 'bin', 'Hostx64', 'x64', 'cl.exe'), [
    '/nologo', '/std:c++20', '/EHsc', '/utf-8', '/MT', '/O2', '/showIncludes', '/DNDEBUG', '/DUNICODE', '/D_UNICODE',
    '/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1', '/I' + root, '/I' + wire, '/I' + path.join(repo, 'native'), '/I' + path.join(wire, 'Frozen'),
    source, '/Fo' + path.join(root, 'native-fixture.obj'), '/Fe' + exe, '/link', 'user32.lib', 'gdi32.lib', 'ole32.lib', 'shell32.lib'
], env);
const vectors = path.join(root, 'native-frames.json');
run('native', exe, [vectors]);

// Build the unchanged Host project with a fixture-only entry point. No Host daemon or driver is launched.
fs.copyFileSync(hostSource, path.join(root, 'host-fixture.cs'), fs.constants.COPYFILE_EXCL);
save('host-fixture.targets', '<Project><ItemGroup><Compile Include="$(MSBuildThisFileDirectory)host-fixture.cs" /></ItemGroup></Project>\n');
const hostProject = path.join(repo, 'InputHost', 'YeManInputHost.csproj');
const managedSources = [...fs.readdirSync(path.dirname(hostProject)).filter(n => n.endsWith('.cs')).map(n => path.join(path.dirname(hostProject), n)),
    hostProject, path.join(repo, 'deps', 'hidmaestro', 'HIDMaestro.Core.dll')];
sourceHashes.push(...managedSources.map(file => ({ path: file, sha256: hash(fs.readFileSync(file)) })));
const hostOutput = path.join(root, 'host-out');
run('build-host', dotnet, ['build', hostProject, '-c', 'Release', '--nologo', '--disable-build-servers', '--artifacts-path', path.join(root, 'artifacts'),
    '-o', hostOutput, '-p:StartupObject=SteamDeckPadContinuityEntry', '-p:UseAppHost=false', '-p:NuGetAudit=false',
    '-p:CustomAfterMicrosoftCommonTargets=' + path.join(root, 'host-fixture.targets')], process.env, repo);
const sdkSha256 = hash(fs.readFileSync(path.join(hostOutput, 'HIDMaestro.Core.dll')));
assert.equal(sdkSha256, sourceHashes.find(entry => entry.path.endsWith('HIDMaestro.Core.dll')).sha256, 'Fixture must use the repository SDK bytes');
const hostResult = path.join(root, 'host-result.json');
run('host', dotnet, [path.join(hostOutput, 'YeManInputHost.dll'), vectors, hostResult]);
const mutated = JSON.parse(fs.readFileSync(vectors, 'utf8'));
const interrupted = mutated.frames.find(frame => frame.stage === 'swipe' && frame.left.active && frame.right.active);
assert(interrupted, 'Expected a held dual-pad frame');
const missingContact = JSON.parse(interrupted.compact);
delete missingContact.touchpads;
interrupted.compact = JSON.stringify(missingContact);
save('negative-drop-touch.json', JSON.stringify(mutated));
const negativeResult = path.join(root, 'negative-result.json');
const negative = run('host-negative-drop-touch', dotnet, [path.join(hostOutput, 'YeManInputHost.dll'),
    path.join(root, 'negative-drop-touch.json'), negativeResult], process.env, root, 1);
assert(negative.includes(`frame ${interrupted.sequence}, swipe: compact parsed pad mismatch`) && !fs.existsSync(negativeResult),
    'A validly serialized held-frame touch dropout must fail on the contact oracle');
const missingWriteResult = path.join(root, 'negative-sdk-write-result.json');
const missingWrite = run('host-negative-skip-sdk-write', dotnet, [path.join(hostOutput, 'YeManInputHost.dll'),
    vectors, missingWriteResult, '--negative-skip-sdk-write'], process.env, root, 1);
assert(missingWrite.includes(`frame 18, swipe: compact SDK slot sequence, report size, lane, guard, or payload mismatch`) &&
    !fs.existsSync(missingWriteResult), 'A held frame that parses but is not published must fail at the SDK slot oracle');
for (const entry of sourceHashes) assert.equal(hash(fs.readFileSync(entry.path)), entry.sha256, `Fixture input changed: ${entry.path}`);
const result = JSON.parse(fs.readFileSync(hostResult, 'utf8'));
assert.equal(result.ok, true);
assert.equal(result.sdkWrites, result.encoded);
assert.equal(result.sdkReaderMatches, result.encoded);
assert.equal(result.delayedReaderSawLatestOnly, true);
const summary = { ...result, sdkSha256, dropoutDetectedAt: interrupted.sequence, missingSdkWriteDetectedAt: 18 };
save('receipt.json', JSON.stringify({ ...summary, nativeInputInjected: 0, outputSha256: hash(fs.readFileSync(vectors)), sourceHashes }, null, 2) + '\n');
console.log(JSON.stringify(summary));
