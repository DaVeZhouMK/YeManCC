import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';

// Only the three requested sources are edited. All frozen inputs, mutants,
// compiler products and receipts go to the external validation directory.
const runner = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(runner), '..');
const out = path.resolve(process.env.YMCC_INPUT_TIMER_EVIDENCE_OUT ||
    'G:/YeManCC-Work/Build/Validation/VC-TIMER-EVIDENCE-20261022/recorder');
assert.equal(process.platform, 'win32', 'This runner uses the local MSVC vcvars toolchain');
fs.mkdirSync(out, { recursive: true });
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const write = (name, content) => {
    const target = path.join(out, name);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, content);
};
const json = (name, value) => write(name, JSON.stringify(value, null, 2) + '\n');
const sources = ['native/input_timer_evidence.h', 'tools/input_timer_evidence_selftest.cpp',
    'tools/input_timer_evidence_selftest.mjs', 'deps/json/json.hpp'];
const bindings = sources.map(name => {
    const bytes = fs.readFileSync(path.join(repo, name));
    write('source/' + name, bytes);
    return { path: name, bytes: bytes.length, sha256: sha(bytes) };
});
const receipt = {
    schema: 1, pass: false, startedAt: new Date().toISOString(), sourceBindings: bindings,
    scope: 'Pure C++ memory execution of the frozen production Recorder header; no production integration or physical input acceptance',
    productionMainModified: false, realTimerOrInputIO: false, productionLaunched: false,
    callbacksDrainedProven: false, steamConsumptionProven: false, raceDetectorRun: false,
    configurations: [], negatives: [],
    api: {
        namespace: 'ymcc::inputTimerEvidence', kind: { multimedia: 1, waitable: 2, sleep: 3 },
        methods: ['ownerBegin(owner,nowMs,eventHandle,kind,intervalMs)',
            'createResult(owner,nowMs,eventHandle,timerId,errorOrResult=0)',
            'killResult(owner,nowMs,eventHandle,timerId,result)',
            'enter(enabled,timerId,eventHandle,nowMs,threadId,powerGeneration,stopObserved)->Token',
            'leave(Token,nowMs,outcome,busOrdinal=0)', 'snapshot()->Snapshot', 'summary()->Summary',
            'snapshotJson(const Snapshot&)->nlohmann::json (cold path only)'],
        outcome: { 1: 'bus-called (count unproven)', 2: 'busy-skip', 3: 'stop-return', 4: 'exception/unknown' },
        summaryFields: ['known', 'accountingComplete', 'inFlightKnown', 'ordinal', 'ownerBegins', 'createResults',
            'killResults', 'entered', 'exited', 'matchedLeaves', 'unmatchedLeaves', 'busCalled', 'busySkip',
            'stopReturn', 'exceptionOrUnknown', 'inFlight', 'maxInFlight', 'clockRollbacks', 'ownerCount',
            'groupCount', 'activeCount', 'ownerCapacityDrop', 'groupCapacityDrop', 'activeCapacityDrop',
            'firstCapacityDrop', 'recentOverwrite', 'boundaryFirstCapacityDrop', 'boundaryRecentOverwrite', 'drops'],
        snapshotFields: ['summary', 'owners[8]', 'groups[8]', 'first[16]', 'recent[64]',
            'boundaryFirst[8]', 'boundaryRecent[16]', 'firstCount', 'recentCount', 'recentStart',
            'boundaryFirstCount', 'boundaryRecentCount', 'boundaryRecentStart'],
        atomicity: 'All plain fields under one try-lock; independently sampled lock-free atomic drop counters; quiescent conservation',
        alignment: 'POD, natural member alignment, struct alignment at least 8; no packed/binary wire contract',
    },
};
const save = () => json('RESULT.json', receipt);
const invoke = (exe, args, log, env, cwd = out, timeout = 60000) => {
    const result = spawnSync(exe, args, { cwd, env, encoding: 'utf8', windowsHide: true,
        timeout, maxBuffer: 8 * 1024 * 1024 });
    write(log, (result.stdout || '') + (result.stderr || ''));
    if (result.error) throw result.error;
    assert.equal(result.signal, null, `${log}: interrupted`);
    return result;
};
const replaceOnce = (text, before, after) => {
    assert.equal(text.split(before).length - 1, 1, 'Mutation must target one production statement');
    return text.replace(before, after);
};
try {
    save();
    const systemRoot = process.env.SystemRoot || 'C:/Windows';
    // vcvars must start from a short PATH, not the app's accumulated tool paths.
    const baseEnv = Object.fromEntries(Object.entries(process.env).filter(([key]) =>
        !['path', 'include', 'lib', 'libpath', 'cl', '_cl_', 'link', '_link_',
            '__vscmd_preinit_path', 'vscmd_ver', 'vscmd_arg_tgt_arch', 'vscmd_arg_host_arch']
            .includes(key.toLowerCase())));
    baseEnv.Path = [path.join(systemRoot, 'System32'), systemRoot,
        path.join(systemRoot, 'System32/Wbem')].join(';');
    baseEnv.VSCMD_SKIP_SENDTELEMETRY = '1';
    const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)',
        'Microsoft Visual Studio/Installer/vswhere.exe');
    const discovery = invoke(vswhere, ['-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
    'toolchain-discovery.log', baseEnv);
    assert.equal(discovery.status, 0, 'MSVC installation discovery');
    const installation = discovery.stdout.trim();
    assert.ok(installation && !/[\r\n]/.test(installation), 'One concrete MSVC installation');
    const vcvars = path.join(installation, 'VC/Auxiliary/Build/vcvars64.bat');
    assert.ok(fs.existsSync(vcvars), 'vcvars64.bat exists');
    for (const value of [vcvars, out]) assert.ok(!/[\r\n"%&|<>^!]/.test(value), 'Safe literal batch path');
    const keys = ['VCToolsInstallDir', 'VCToolsVersion', 'WindowsSDKVersion', 'INCLUDE', 'LIB', 'LIBPATH', 'PATH'];
    write('compiler-environment.cmd', '@echo off\r\n' + `call "${vcvars}" >nul\r\n` +
        'if errorlevel 1 exit /b %errorlevel%\r\n' + keys.map(k => `echo ${k}=%${k}%\r\n`).join(''));
    const setup = invoke(path.join(systemRoot, 'System32/cmd.exe'),
        ['/d', '/c', path.join(out, 'compiler-environment.cmd')], 'compiler-environment.log', baseEnv, out, 30000);
    assert.equal(setup.status, 0, 'vcvars environment initialization');
    const env = { ...baseEnv };
    for (const line of setup.stdout.split(/\r?\n/)) {
        const at = line.indexOf('=');
        if (at <= 0) continue;
        const key = line.slice(0, at);
        if (!keys.includes(key)) continue;
        for (const old of Object.keys(env)) if (old.toLowerCase() === key.toLowerCase()) delete env[old];
        env[key] = line.slice(at + 1);
    }
    for (const key of Object.keys(env)) if (['cl', '_cl_', 'link', '_link_'].includes(key.toLowerCase())) delete env[key];
    assert.ok(env.VCToolsInstallDir && env.INCLUDE && env.LIB && env.PATH, 'Concrete compiler environment');
    const compiler = path.join(env.VCToolsInstallDir, 'bin/Hostx64/x64/cl.exe');
    receipt.compiler = { path: compiler, sha256: sha(fs.readFileSync(compiler)),
        vcToolsVersion: env.VCToolsVersion, windowsSdkVersion: env.WindowsSDKVersion,
        initialPathCharacters: baseEnv.Path.length, compilerPathCharacters: env.PATH.length };
    const frozen = path.join(out, 'source');
    const header = fs.readFileSync(path.join(frozen, 'native/input_timer_evidence.h'), 'utf8');
    const mutations = [
        { name: 'removed-disabled-gate', expected: 'CHECK_FAILED disabled-token-empty',
            header: replaceOnce(header, 'if (!enabled) return {};', '(void)enabled;') },
        { name: 'missing-recent-overwrite-accounting', expected: 'CHECK_FAILED recent-overwrite-count',
            header: replaceOnce(header, '++data_.summary.recentOverwrite;', '(void)0;') },
    ];
    for (const mutation of mutations) write(`negative/${mutation.name}/input_timer_evidence.h`, mutation.header);
    const modes = [['Release', ['/O2', '/DNDEBUG', '/MT']], ['Checked', ['/Od', '/RTC1', '/Zi', '/D_DEBUG', '/MTd']]];
    const buildAndRun = (mode, flags, variant, include) => {
        const folder = path.join(out, mode, variant);
        fs.mkdirSync(folder, { recursive: true });
        const exe = path.join(folder, 'selftest.exe');
        const build = invoke(compiler, ['/nologo', '/std:c++20', '/EHsc', '/utf-8', '/permissive-', '/W4', '/WX',
            ...flags, '/I' + include, '/I' + path.join(frozen, 'deps/json'),
            path.join(frozen, 'tools/input_timer_evidence_selftest.cpp'),
            '/Fo' + path.join(folder, 'selftest.obj'), '/Fd' + path.join(folder, 'compiler.pdb'), '/Fe' + exe,
            '/link', '/INCREMENTAL:NO', '/PDB:' + path.join(folder, 'selftest.pdb')],
        `${mode}/${variant}/build.log`, env, folder);
        assert.equal(build.status, 0, `${mode}/${variant} must compile before its behavior is tested; see build.log`);
        return invoke(exe, [], `${mode}/${variant}/run.log`, env, folder);
    };
    for (const [mode, flags] of modes) {
        const run = buildAndRun(mode, flags, 'baseline', path.join(frozen, 'native'));
        assert.equal(run.status, 0, `${mode} memory tests failed: ${run.stdout || ''}${run.stderr || ''}`);
        const result = JSON.parse(run.stdout.trim());
        assert.equal(result.pass, true);
        assert.equal(result.cases, 11);
        assert.ok(result.checks >= 1000, 'Substantive fixture assertions executed');
        assert.equal(result.watchedAllocations, 0);
        receipt.configurations.push({ mode, ...result });
        console.log(`${mode}: ${result.cases} cases, ${result.checks} checks, 100000 burst entries, zero hot allocations`);
        save();
        for (const mutation of mutations) {
            const negative = buildAndRun(mode, flags, mutation.name, path.join(out, 'negative', mutation.name));
            assert.equal(negative.status, 1, `${mode}/${mutation.name} must be rejected by the memory fixture`);
            assert.ok(negative.stderr.includes(mutation.expected), `${mode}/${mutation.name} must hit its intended assertion: ${negative.stderr}`);
            receipt.negatives.push({ mode, name: mutation.name, caught: true, expected: mutation.expected,
                exitCode: negative.status, headerSha256: sha(mutation.header) });
            console.log(`${mode}: caught ${mutation.name}: ${mutation.expected}`);
            save();
        }
    }
    assert.ok(bindings.every(b => sha(fs.readFileSync(path.join(repo, b.path))) === b.sha256),
        'Only evidence bound to unchanged current sources is valid');
    receipt.sourceStableDuringRun = true;
    receipt.totalBaselineChecks = receipt.configurations.reduce((sum, r) => sum + r.checks, 0);
    receipt.negativeRejections = receipt.negatives.length;
    receipt.pass = true;
    receipt.finishedAt = new Date().toISOString();
    save();
    console.log(JSON.stringify({ pass: true, baselineConfigurations: receipt.configurations.length,
        totalBaselineChecks: receipt.totalBaselineChecks, negativeRejections: receipt.negativeRejections,
        output: out, sourceStableDuringRun: true, api: receipt.api }));
} catch (error) {
    receipt.error = String(error.stack || error);
    save();
    console.error(receipt.error);
    process.exitCode = 1;
}
