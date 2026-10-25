import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const workspace = path.resolve(repo, '../../..');
const fixture = path.join(repo, 'tools/fixtures/hc_legacy_motion_catalog');
const target = path.join(repo, 'native/hc_legacy_motion_catalog.h');
const dependency = 'deps/handheldcompanion-runtime';
const sourceRoot = `${dependency}/source`;
const validation = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017');
const args = process.argv.slice(2);
const option = name => { const at = args.indexOf(name); assert(at < 0 || args[at + 1] && !args[at + 1].startsWith('--'), `${name} requires a value`); return at < 0 ? null : args[at + 1]; };
const known = new Set(['--check', '--out', '--inputs', '--vcvars']);
for (let i = 0; i < args.length; i++) { assert(known.has(args[i]), `unknown argument ${args[i]}`); if (args[i] !== '--check') i++; }
const check = args.includes('--check');
const out = path.resolve(option('--out') || path.join(validation, 'LegacyMotion/catalog', new Date().toISOString().replaceAll(':', '-')));
assert(!fs.existsSync(out) || fs.readdirSync(out).length === 0, 'evidence directory must be fresh');
assert(out !== repo && !out.startsWith(fixture + path.sep), 'evidence must be outside the implementation fixtures');
fs.mkdirSync(out, { recursive: true });
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const read = p => fs.readFileSync(path.resolve(repo, p));
const json = v => JSON.stringify(v, null, 2) + '\n';
const save = (p, value) => { const full = path.join(out, p); fs.mkdirSync(path.dirname(full), { recursive: true }); fs.writeFileSync(full, value); };
const parseJSON = bytes => JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, ''));
const inputsPath = path.resolve(option('--inputs') || path.join(fixture, 'identity-inputs.json'));
const vcvars = option('--vcvars') || 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const samples = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1], [2, 3, 5], [-2, 3, -5], [7, -11, 13], [-7, -11, -13]];
const excluded = new Set(['DefaultDevice', 'Unknown', 'ASUS', 'SteamDeck', 'ClawA1M', 'ClawA2VM', 'ClawBZ2EM', 'ClawCG3EM', 'XboxROGAlly', 'XboxROGAllyX',
    'AYANEO3', 'AYANEOAIR2', 'AYANEOAIRPro1S', 'AYANEOAIRPlusNxx', 'AYANEOFlip11', 'OneXPlayerX2', 'OneXPlayerX2MiniPro', 'OneXPlayer3', 'AYANEONEXT2', 'SteamMachine']);

function run(label, exe, argv, cwd = out, expected = 0) {
    const result = spawnSync(exe, argv, { cwd, encoding: 'utf8', windowsHide: true, timeout: 120000, maxBuffer: 24 * 1024 * 1024 });
    save(label + '.log', (result.stdout || '') + (result.stderr || '') + (result.error ? '\n' + result.error : ''));
    assert.equal(result.status, expected, `${label} failed; see ${path.join(out, label + '.log')}`);
    return result.stdout;
}
function block(text, marker, from = 0) {
    const start = text.indexOf(marker, from);
    assert(start >= 0, `missing production block: ${marker}`);
    const masked = text.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\r\n]*|\/\*[\s\S]*?\*\//g, m => m.replace(/[^\r\n]/g, ' '));
    const open = masked.indexOf('{', start);
    let depth = 0;
    for (let i = open; i < masked.length; i++) {
        if (masked[i] === '{') depth++;
        if (masked[i] === '}' && --depth === 0) return { code: text.slice(start, i + 1), start, end: i + 1, sha256: sha(text.slice(start, i + 1)) };
    }
    throw Error(`unclosed production block: ${marker}`);
}
function extract(main) {
    return [
        block(main, 'enum class YmccFamily : int'),
        block(main, 'struct MachineIdentity {'),
        block(main, 'static std::string ymccDmiNormalize('),
        block(main, 'static void ymccResolveFamily(MachineIdentity& id)'),
        block(main, 'static bool ymccParseAxisToken('),
        block(main, 'static bool ymccSplitAxisRecipe('),
        block(main, 'static bool ymccApplyNamedImuMatrix(', main.indexOf('static bool ymccSplitAxisRecipe(')),
    ];
}
function csProject(directory, source, roslyn = false) {
    save(`${directory}/Program.cs`, source);
    save(`${directory}/Probe.csproj`, `<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><RestoreIgnoreFailedSources>true</RestoreIgnoreFailedSources></PropertyGroup>${roslyn ? '<ItemGroup><Reference Include="Microsoft.CodeAnalysis"><HintPath>$(MSBuildSDKsPath)/../Roslyn/bincore/Microsoft.CodeAnalysis.dll</HintPath></Reference><Reference Include="Microsoft.CodeAnalysis.CSharp"><HintPath>$(MSBuildSDKsPath)/../Roslyn/bincore/Microsoft.CodeAnalysis.CSharp.dll</HintPath></Reference></ItemGroup>' : ''}</Project>`);
    save(`${directory}/NuGet.Config`, '<configuration><packageSources><clear /></packageSources></configuration>');
    run(`${directory}/build`, 'dotnet', ['build', path.join(out, directory, 'Probe.csproj'), '--nologo', '-v:q']);
    return path.join(out, directory, 'bin/Debug/net10.0/Probe.dll');
}
function compile(label, cpp, flags = '/O2 /DNDEBUG /MT') {
    const dir = label;
    save(`${dir}/harness.cpp`, cpp);
    // All paths are tool/user-supplied filesystem paths, quoted as cmd arguments; no shell interpolation of source text.
    for (const p of [out, repo, vcvars]) assert(!/["%\r\n]/.test(p), 'unsupported shell path');
    save(`${dir}/build.cmd`, `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc ${flags} "${path.join(out, dir, 'harness.cpp')}" /I"${path.join(repo, 'deps/json')}" /Fo"${path.join(out, dir, 'harness.obj')}" /Fe"${path.join(out, dir, 'harness.exe')}"\r\nexit /b %errorlevel%\r\n`);
    run(`${dir}/build`, 'cmd.exe', ['/d', '/c', path.join(out, dir, 'build.cmd')]);
    return path.join(out, dir, 'harness.exe');
}

function apply(matrix, input) {
    const output = [0, 0, 0];
    for (let i = 0; i < 3; i++) output[matrix.swap[i]] = input[i];
    return output.map((v, i) => v * matrix.axis[i]);
}
function same(a, b) { return JSON.stringify(a) === JSON.stringify(b); }
function tuple(row) { return JSON.stringify([row.deviceClass, row.gyroMatrixId, row.accelMatrixId]); }

function header(recipes) {
    return `#pragma once\n#include <string_view>\n\n// Generated by tools/hc_legacy_motion_catalog.mjs; run it with --check to verify.\n// Fixed HC 0.32.x source declarations, independently compared with native transforms.\n// Recipes do not prove sensor presence, provider admission or exact physical SKU.\n// Controller motion has priority; Windows providers still need runtime admission.\nnamespace ymcc::hc_legacy_motion {\nstruct Recipe {\n    std::string_view deviceClass, selectorId, gyroMatrixId, accelMatrixId;\n    std::string_view sourceScope, sourcePath, sourceSha256;\n    bool controllerPreferred;\n};\ninline constexpr Recipe recipes[] = {\n${recipes.map(r => `    {${['deviceClass', 'selectorId', 'gyroMatrixId', 'accelMatrixId', 'sourceScope', 'sourcePath', 'sourceSha256'].map(k => JSON.stringify(r[k])).join(', ')}, ${r.controllerPreferred}},`).join('\n')}\n};\ninline constexpr const Recipe* find(std::string_view deviceClass, std::string_view gyroMatrixId, std::string_view accelMatrixId) noexcept {\n    for (const auto& recipe : recipes)\n        if (recipe.deviceClass == deviceClass && recipe.gyroMatrixId == gyroMatrixId && recipe.accelMatrixId == accelMatrixId) return &recipe;\n    return nullptr;\n}\ninline constexpr bool containsClass(std::string_view deviceClass) noexcept {\n    for (const auto& recipe : recipes) if (recipe.deviceClass == deviceClass) return true;\n    return false;\n}\n} // namespace ymcc::hc_legacy_motion\n`;
}

try {
    const baselineBytes = read(`${dependency}/HC-BASELINE.json`), baseline = parseJSON(baselineBytes);
    const indexBytes = read(`${dependency}/source-files.sha256`);
    assert(/^0\.32\./.test(baseline.upstreamProjectVersion), 'only the pinned HC 0.32.x baseline is supported');
    assert.equal(sha(indexBytes), baseline.sourceFilesIndexSha256.toLowerCase(), 'HC source index differs from HC-BASELINE.json');
    const baselineIndex = new Map(indexBytes.toString('utf8').trim().split(/\r?\n/).map(line => { const m = line.match(/^([a-f0-9]{64})\s+(.+)$/i); assert(m, 'malformed source hash index'); return [m[2], m[1].toLowerCase()]; }));
    const sourceHashes = [];
    function pinned(relative) {
        const bytes = read(`${sourceRoot}/${relative}`), actual = sha(bytes);
        assert(baselineIndex.has(relative), `HC source missing from baseline index: ${relative}`);
        assert.equal(actual, baselineIndex.get(relative), `HC source drift: ${relative}`);
        if (baseline.coreFiles[relative]) assert.equal(actual, baseline.coreFiles[relative].toLowerCase(), `HC core hash differs: ${relative}`);
        sourceHashes.push({ path: `${sourceRoot}/${relative}`, sha256: actual, baselineSha256: baselineIndex.get(relative) });
        save(`raw-before/${sourceRoot}/${relative}`, bytes);
        return bytes;
    }
    save(`raw-before/${dependency}/HC-BASELINE.json`, baselineBytes);
    save(`raw-before/${dependency}/source-files.sha256`, indexBytes);
    const deviceFiles = fs.readdirSync(path.join(repo, sourceRoot, 'Devices'), { recursive: true }).filter(f => f.endsWith('.cs')).map(f => 'Devices/' + f.replaceAll('\\', '/')).sort();
    for (const file of deviceFiles) pinned(file);
    const supportFiles = ['Sensors/IMUGyrometer.cs', 'Sensors/IMUAccelerometer.cs', 'Managers/SensorsManager.cs'];
    for (const file of supportFiles) pinned(file);
    const mainBytes = read('native/main.cpp'), main = mainBytes.toString('utf8');
    save('raw-before/native/main.cpp', mainBytes);
    for (const p of ['native/hc_legacy_motion_catalog.h', 'tools/fixtures/hc_legacy_motion_catalog/catalog.json', 'tools/hc_legacy_motion_catalog.mjs', 'tools/fixtures/hc_legacy_motion_catalog/SourceReader.cs', ...fs.readdirSync(fixture).filter(p => p !== 'catalog.json').map(p => 'tools/fixtures/hc_legacy_motion_catalog/' + p)]) {
        if (fs.existsSync(path.join(repo, p))) save('raw-before/' + p, read(p));
    }
    const inputBytes = fs.readFileSync(inputsPath), inputs = parseJSON(inputBytes);
    assert(Array.isArray(inputs) && inputs.length > 0, 'identity inputs are empty');
    assert(new Set(inputs.map(x => x.id)).size === inputs.length, 'duplicate identity input id');
    save('inputs.json', inputBytes);
    const followupFile = path.join(fixture, 'followup-matrix-classes.json');
    const followupBytes = fs.readFileSync(followupFile), followup = parseJSON(followupBytes);
    save('raw-before/followup-matrix-classes.json', followupBytes);
    save('source-hashes.json', json({ baselineSha256: sha(baselineBytes), sourceIndexSha256: sha(indexBytes), mainSha256: sha(mainBytes), inputsSha256: sha(inputBytes), followupSha256: sha(followupBytes), files: sourceHashes }));
    const reader = csProject('source-reader', fs.readFileSync(path.join(fixture, 'SourceReader.cs'), 'utf8'), true);
    const inventory = JSON.parse(run('source-reader/inventory', 'dotnet', [reader, path.join(out, 'raw-before', sourceRoot)]));
    save('source-inventory.json', json(inventory));
    for (const file of inventory.files) assert.equal(file.sha256, baselineIndex.get(file.path), 'reader/baseline hash mismatch');
    const parserFaults = JSON.parse(run('source-reader/faults', 'dotnet', [reader, '--faults', path.join(fixture, 'parser-faults.json')]));
    save('parser-faults.json', json(parserFaults));
    const sourceFaults = JSON.parse(run('source-reader/source-faults', 'dotnet', [reader, '--source-faults', path.join(fixture, 'source-faults.json'), path.join(out, 'source-reader/scenarios')]));
    save('source-faults.json', json(sourceFaults));
    const factoryOracle = csProject('factory-oracle', `using System.Text.Json;\nvar rows=JsonSerializer.Deserialize<List<Input>>(File.ReadAllText(args[0]))!;\nConsole.WriteLine(JsonSerializer.Serialize(rows.Select(row=>new {row.id,deviceClass=Resolve(row)})));\nstring Resolve(Input row){IDevice? device=null;var ManufacturerName=row.manufacturer.ToUpperInvariant();var ProductName=row.product;var SystemName=row.systemProduct;var SystemModel=row.systemProduct;var Processor=row.processor;var Version=row.version;var NumberOfCores=row.cores;${inventory.factorySwitch}\nreturn device is AsusDevice ? "ASUS" : device?.GetType().Name ?? "DefaultDevice";}\nrecord Input(string id,string manufacturer,string product,string systemProduct,string processor,string version,int cores);class IDevice{}\n${inventory.factoryClasses.filter(n => n !== 'IDevice').map(n => `class ${n === 'ASUS' ? 'AsusDevice' : n}:IDevice{}`).join('\n')}`);
    const oracle = JSON.parse(run('factory-oracle/identities', 'dotnet', [factoryOracle, path.join(out, 'inputs.json')]));
    assert.equal(oracle.length, inputs.length);
    // This executes only extracted factory control flow with inert stub classes, never HC constructors.
    const factoryCoverage = [...new Set(oracle.map(x => x.deviceClass))].sort();
    const absentFactoryClasses = inventory.factoryClasses.filter(c => !factoryCoverage.includes(c));
    assert.equal(factoryCoverage.length, 80, 'locked HC factory class count changed; review scope');
    assert.equal(absentFactoryClasses.length, 0, `uncovered HC factory classes: ${absentFactoryClasses}`);
    const extracted = extract(main);
    const bindings = extracted.map(({ code, ...binding }) => ({ ...binding, marker: code.slice(0, code.indexOf('{')).trim() }));
    save('source-bindings.json', json({ main: 'native/main.cpp', mainSha256: sha(mainBytes), bindings, HCAssemblyLoaded: false, hardwareWrites: false }));
    const probeCode = fs.readFileSync(path.join(fixture, 'native-probe.cpp'), 'utf8')
        .replace('@PRODUCTION_BLOCKS@', () => extracted.map(x => x.code + ';').join('\n'))
        .replace('@SAMPLES@', () => JSON.stringify(samples));
    const native = JSON.parse(run('native-probe/results', compile('native-probe', probeCode), [path.join(out, 'inputs.json')]));
    save('native-metadata.json', json(native));
    assert.equal(native.length, inputs.length);
    const devices = new Map(inventory.classes.map(c => [c.name, c]));
    const sources = new Map(sourceHashes.map(x => [x.path, x]));
    function resolveSource(name, active = new Set()) {
        assert(!active.has(name), `inheritance cycle ${name}`);
        const d = devices.get(name); assert(d, `unknown source class ${name}`);
        assert(d.issues.length === 0, d.issues.join('\n'));
        const next = new Set([...active, name]);
        const parent = d.parent ? resolveSource(d.parent, next) : { inheritance: [], gyro: null, accel: null, overrides: [] };
        const result = structuredClone(parent); result.inheritance.push({ class: name, path: `${sourceRoot}/${d.path}`, sha256: sources.get(`${sourceRoot}/${d.path}`).sha256 });
        for (const a of d.assignments) {
            const kind = a.kind === 'GyroMatrix' ? 'gyro' : 'accel';
            const declaration = { sourceClass: name, path: `${sourceRoot}/${d.path}`, line: a.line, expression: a.expression, ...a.matrix };
            result[kind] = declaration;
            result.overrides.push({ kind, ...declaration });
        }
        assert(result.gyro && result.accel, `incomplete matrix initialization for ${name}`);
        return result;
    }
    const groups = new Map(), excludedRows = [], uncertain = [], comparison = [];
    for (let i = 0; i < inputs.length; i++) {
        assert.equal(native[i].id, inputs[i].id); assert.equal(oracle[i].id, inputs[i].id);
        const row = native[i], cls = oracle[i].deviceClass, leaf = row.deviceClass.split('.').at(-1);
        const tupleKey = tuple(row);
        if (excluded.has(cls) || excluded.has(leaf)) { excludedRows.push({ id: row.id, sourceClass: cls, nativeClass: row.deviceClass, reason: 'excluded-scope-or-unknown' }); continue; }
        if (!row.resolved || !row.gyroMatrixId || !row.accelMatrixId) { excludedRows.push({ id: row.id, sourceClass: cls, nativeClass: row.deviceClass, reason: 'native-no-matrix-pair' }); continue; }
        let source;
        try { source = resolveSource(cls); }
        catch (error) { uncertain.push({ id: row.id, sourceClass: cls, tuple: tupleKey, reason: error.message }); continue; }
        const expected = samples.map(s => ({ gyro: apply(source.gyro, s), accel: apply(source.accel, s) }));
        const mismatches = row.vectors.flatMap((actual, j) => ['gyro', 'accel'].filter(k => !actual[k + 'Ok'] || !same(actual[k], expected[j][k])).map(kind => ({ kind, input: samples[j], expected: expected[j][kind], actual: actual[kind], nativeAccepted: actual[kind + 'Ok'] })));
        comparison.push({ id: row.id, sourceClass: cls, nativeClass: row.deviceClass, gyroMatrixId: row.gyroMatrixId, accelMatrixId: row.accelMatrixId, expected, actual: row.vectors, mismatches });
        let group = groups.get(tupleKey);
        if (!group) { group = { deviceClass: row.deviceClass, gyroMatrixId: row.gyroMatrixId, accelMatrixId: row.accelMatrixId, aliases: new Map(), inputs: [], failures: [] }; groups.set(tupleKey, group); }
        group.inputs.push(row.id); group.aliases.set(cls, source);
        if (mismatches.length) { group.failures.push(row.id); uncertain.push({ id: row.id, sourceClass: cls, tuple: tupleKey, reason: 'declared-source/native-transform-mismatch', mismatches }); }
    }
    // An uncertain alias poisons its entire tuple; matching aliases cannot silently cover it.
    for (const problem of uncertain) if (groups.has(problem.tuple)) groups.get(problem.tuple).failures.push(problem.id);
    const recipes = [], bundles = {};
    const commonPaths = supportFiles.map(p => `${sourceRoot}/${p}`);
    const scope = `${baseline.baselineId}/${baseline.upstreamProjectVersion}/source-declarations`;
    for (const [key, group] of [...groups].sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)) {
        if (group.failures.length) continue;
        const aliases = [...group.aliases].sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0).map(([sourceClass, source]) => ({ sourceClass, ...source }));
        const allPaths = [...new Set([...commonPaths, ...aliases.flatMap(a => a.inheritance.map(x => x.path))])].sort();
        const bundleSources = allPaths.map(p => ({ path: p, sha256: sources.get(p).sha256 }));
        const manifest = bundleSources.map(s => `${s.sha256}  ${s.path}\n`).join('');
        const sourceSha256 = sha(manifest);
        bundles[sourceSha256] = { digestAlgorithm: 'SHA256 of UTF-8 lines: lowercase file sha256, two spaces, repo-relative POSIX path, LF; sources sorted by path', manifest, sources: bundleSources };
        recipes.push({ deviceClass: group.deviceClass, selectorId: `hc-legacy-0.32:model:${group.deviceClass.split('.').at(-1)}:${sha(key).slice(0, 16)}`, gyroMatrixId: group.gyroMatrixId, accelMatrixId: group.accelMatrixId,
            sourceScope: scope, sourcePath: `tools/fixtures/hc_legacy_motion_catalog/catalog.json#bundles/${sourceSha256}`, sourceSha256,
            controllerPreferred: group.deviceClass.startsWith('HandheldCompanion.Devices.LegionGo'), aliases, identityInputs: group.inputs.sort() });
    }
    assert(recipes.length > 0, 'no proven recipes');
    assert.equal(new Set(recipes.map(r => r.selectorId)).size, recipes.length, 'selector collision');
    const coveredNativeClasses = [...new Set(recipes.map(r => r.deviceClass))].sort();
    const coveredSourceClasses = [...new Set(recipes.flatMap(r => r.aliases.map(a => a.sourceClass)))].sort();
    const unresolvedFollowup = followup.classes.filter(c => !coveredNativeClasses.includes('HandheldCompanion.Devices.' + c));
    const metadata = { schema: 1, scope: 'legacy declared matrix recipes; no provider/sensor/SKU claim', baseline: { id: baseline.baselineId, version: baseline.upstreamProjectVersion, sha256: sha(baselineBytes), sourceIndexSha256: sha(indexBytes) },
        samples, semantics: 'new IMUMatrix resets both Axis and AxisSwap; base constructors first, derived assignments in lexical order; output[swap[input]]=raw[input], then output *= Axis; no HC constructor execution',
        runtimeCaveat: 'IMUMatrix initializes cached _axisRemapIndices to identity; object initializers assign AxisSwap without invalidating that cache. This audit compares declared AxisSwap recipes, not execution of HC sensor callbacks. No claim of HC runtime remap parity is made.',
        controllerPolicy: 'SensorsManager prefers controller.HasMotionSensor(); Windows requires runtime InternalSensor from IDevice.PullSensors/GetDefault. All providers need main-thread admission. controllerPreferred marks Lenovo family only; false does not override global controller priority.',
        recipeCount: recipes.length, coveredNativeClasses, coveredSourceClasses, bundles, recipes,
        excludedClasses: [...new Set(excludedRows.map(r => r.sourceClass))].sort(),
        unproven: uncertain.map(({ id, sourceClass, tuple, reason, mismatches }) => ({ id, sourceClass, tuple, reason, ...(mismatches ? { mismatches } : {}) })),
        unresolvedFollowupClasses: unresolvedFollowup.sort() };
    const expectedHeader = header(recipes), expectedMetadata = json(metadata);
    save('generated/hc_legacy_motion_catalog.h', expectedHeader); save('generated/catalog.json', expectedMetadata);
    save('matrix-comparison.json', json({ samples, cases: comparison, excludedRows, uncertain }));
    // Bind output to the frozen bytes even if another task edits unrelated parts of main.cpp.
    assert.deepEqual(extract(read('native/main.cpp').toString('utf8')).map(x => x.sha256), extracted.map(x => x.sha256), 'production identity/matrix functions changed during generation');
    for (const file of sourceHashes) assert.equal(sha(read(file.path)), file.sha256, 'HC source changed during generation');
    if (check) {
        assert.equal(fs.readFileSync(target, 'utf8'), expectedHeader, 'generated header is stale');
        assert.equal(fs.readFileSync(path.join(fixture, 'catalog.json'), 'utf8'), expectedMetadata, 'generated metadata is stale');
    }
    // Exhaustive exact-tuple and cross-pair negatives use independent JSON expectations.
    const expectations = { recipes: recipes.map(r => ({ deviceClass: r.deviceClass, gyroMatrixId: r.gyroMatrixId, accelMatrixId: r.accelMatrixId, selectorId: r.selectorId, sourceScope: r.sourceScope, sourcePath: r.sourcePath, sourceSha256: r.sourceSha256, controllerPreferred: r.controllerPreferred })), excluded: [...excluded].map(c => 'HandheldCompanion.Devices.' + c) };
    save('catalog-expectations.json', json(expectations));
    const headerHarness = include => fs.readFileSync(path.join(fixture, 'lookup.cpp'), 'utf8')
        .replace('@CATALOG_HEADER@', () => include.replaceAll('\\', '/'));
    const checks = [];
    for (const [config, flags] of [['release', '/O2 /DNDEBUG /MT'], ['checked', '/Od /RTC1 /Zi /D_DEBUG /MTd']]) {
        const exe = compile(`lookup-${config}`, headerHarness(path.join(out, 'generated/hc_legacy_motion_catalog.h')), flags);
        checks.push({ config, ...JSON.parse(run(`lookup-${config}/results`, exe, [path.join(out, 'catalog-expectations.json')])) });
    }
    const mutations = [
        ['ignore-accel-id', expectedHeader.replace(' && recipe.accelMatrixId == accelMatrixId', '')],
        ['ignore-gyro-id', expectedHeader.replace(' && recipe.gyroMatrixId == gyroMatrixId', '')],
        ['class-first-only', expectedHeader.replace('recipe.gyroMatrixId == gyroMatrixId && recipe.accelMatrixId == accelMatrixId', 'true')],
        ['lose-controller-priority', expectedHeader.replace(/, true\},/g, ', false},')],
        ['corrupt-source-digest', expectedHeader.replace(JSON.stringify(recipes[0].sourceSha256), JSON.stringify('0'.repeat(64)))],
    ];
    const mutationResults = [];
    for (const [name, mutant] of mutations) {
        assert.notEqual(mutant, expectedHeader, `mutation did not apply: ${name}`);
        save(`fault-${name}/catalog.h`, mutant);
        const exe = compile(`fault-${name}`, headerHarness(path.join(out, `fault-${name}/catalog.h`)));
        mutationResults.push({ name, ...JSON.parse(run(`fault-${name}/results`, exe, [path.join(out, 'catalog-expectations.json')], out, 7)) });
    }
    if (!check) { fs.writeFileSync(target, expectedHeader); fs.writeFileSync(path.join(fixture, 'catalog.json'), expectedMetadata); }
    const summary = { ok: true, mode: check ? 'check' : 'generate', recipeCount: recipes.length, nativeClasses: coveredNativeClasses.length, sourceClasses: coveredSourceClasses.length,
        identityCases: inputs.length, hcFactoryClasses: factoryCoverage.length, comparedCases: comparison.length, independentVectorComparisons: comparison.length * samples.length * 2,
        priorFollowupMatrixClasses: followup.classes.length, followupCovered: followup.classes.length - unresolvedFollowup.length,
        unresolvedFollowupClasses: metadata.unresolvedFollowupClasses, unprovenCases: uncertain.length, parserFaultCases: parserFaults.length, sourceFaultCases: sourceFaults.length, lookupChecks: checks, mutationResults,
        HCAssemblyLoaded: false, hardwareWrites: false, evidence: out, generatedFiles: ['native/hc_legacy_motion_catalog.h', 'tools/fixtures/hc_legacy_motion_catalog/catalog.json'] };
    save('artifact-hashes.json', json({ files: ['native/hc_legacy_motion_catalog.h', 'tools/hc_legacy_motion_catalog.mjs', ...fs.readdirSync(fixture).sort().map(f => 'tools/fixtures/hc_legacy_motion_catalog/' + f)].map(p => ({ path: p, sha256: sha(read(p)) })) }));
    save('summary.json', json(summary)); console.log(json(summary));
} catch (error) { save('failure.json', json({ ok: false, error: error.stack })); console.error(error.stack); process.exitCode = 1; }
