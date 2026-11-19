// Windows x64, pure CPU/header validation. No GamepadMotion.dll, device, Host,
// WinRT, power operation, runtime process, or network input is used.
// node tools/legion_motion_identity_scope_selftest.mjs [--out <fresh ControllerMotion/LegionFamilies/IdentityScope child>] [--vcvars <batch>]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { extractSources } from './fixtures/legion_motion_identity_scope/source.mjs';

const runner = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(runner), '..');
const fixtures = path.join(repo, 'tools/fixtures/legion_motion_identity_scope');
let workspace = repo;
while (!fs.existsSync(path.join(workspace, 'Mainline/YeManCC-source/YeManCC/native'))) {
  const parent = path.dirname(workspace);
  if (parent === workspace) throw Error('Cannot locate shared workspace root');
  workspace = parent;
}
const options = {}, args = process.argv.slice(2), seenOptions = new Set();
for (let i = 0; i < args.length; ++i) {
  const key=args[i];
  if(seenOptions.has(key))throw Error(`Repeated option: ${key}`);
  seenOptions.add(key);
  if (['--full-raw-resolver','--prepare-only','--normal-only'].includes(key)) options[key.slice(2)]=true;
  else if (['--out', '--vcvars','--resume-release-from'].includes(key) && args[i + 1] && !args[i+1].startsWith('--')) options[key.slice(2)] = args[++i];
  else throw Error(`Unknown/incomplete option: ${key}`);
}
if(options['full-raw-resolver']&&!options['prepare-only']&&!options['normal-only'])throw Error('Full raw resolver requires explicit --prepare-only or --normal-only; full semantic-control route is not validated');
if(options['prepare-only']&&options['normal-only'])throw Error('--prepare-only and --normal-only are mutually exclusive');
const canonicalCases=[...fs.readFileSync(path.join(fixtures,'harness.cpp'),'utf8').split('int main(int argc,char**argv)')[1].matchAll(/\{"([\w-]+)",test\w+\}/g)].map(m=>m[1]);
if(options['resume-release-from']&&(!options['normal-only']||options['prepare-only']||options['full-raw-resolver']||!canonicalCases.includes(options['resume-release-from'])))throw Error('Resume requires exact default-prefix normal-only Release case');

const validation = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionFamilies/IdentityScope');
const out = path.resolve(options.out || path.join(validation, new Date().toISOString().replace(/[:.]/g, '-')));
const inside = (base, target) => {
  const relative = path.relative(base, target);
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
};
if (!inside(validation, out) || path.relative(validation, out) === '') throw Error('Evidence requires a fresh ControllerMotion/LegionFamilies/IdentityScope child');
fs.mkdirSync(validation, { recursive: true });
const realRoot = fs.realpathSync(validation);
if (!inside(validation, realRoot) || !inside(realRoot, validation)) throw Error('Validation root is redirected');
let parent = out;
while (!fs.existsSync(parent)) parent = path.dirname(parent);
if (!inside(realRoot, fs.realpathSync(parent))) throw Error('Evidence parent is redirected');
fs.mkdirSync(out, { recursive: true });
if (fs.existsSync(path.join(out, 'source-bindings.json'))) throw Error('Choose a fresh directory; previous evidence is preserved');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const receipt = { schemaVersion: 1, status: 'running', passed:false, compilerInvocations:0, businessExecutions:0,
  selection:{fullRawResolver:!!options['full-raw-resolver'],prepareOnly:!!options['prepare-only'],normalOnly:!!options['normal-only']},
  semanticControlsStatus:(options['prepare-only']||options['normal-only'])?'NOT_RUN':'PENDING', startedAt: new Date().toISOString(),
  scope: options['full-raw-resolver']?'complete extracted raw resolver with original interface/ancestor/cache/observe consumers; memory OS leaves only':'actual extracted InterfaceIdentity/ancestor/evict/invalidate with bounded raw-cache fragments; not full raw resolver', integratedIntoMain: false,
  actualControllerFeatureCompleted: false, hardwareIo: false, dllLoaded: false, network: false,
  callbacksMayThrow: 'unsupported; not tested', configurations: [], mutations: [] };
const save = () => fs.writeFileSync(path.join(out, 'source-bindings.json'), JSON.stringify(receipt, null, 2) + '\n');
save();
function logged(log, result) {
  fs.mkdirSync(path.dirname(path.join(out, log)), { recursive: true });
  fs.writeFileSync(path.join(out, log), (result.stdout || '') + (result.stderr || ''));
  if (result.error) throw result.error;
  return result;
}
function run(exe, argv, log, cwd = out, env = process.env) {
  return logged(log, spawnSync(exe, argv, { cwd, env, windowsHide: true, encoding: 'utf8', timeout: 30000, maxBuffer: 4 * 1024 * 1024 }));
}
function success(result, label) {
  if (result.status !== 0) throw Error(`${label}: exit ${result.status}`);
  return result.stdout;
}
async function compile(argv, log, cwd, env) {
  const result = await new Promise((resolve, reject) => {
    const child = spawn('cl.exe', argv, { cwd, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '', stderr = '';
    const timer = setTimeout(() => { child.kill(); reject(Error(`${log}: compiler timeout`)); }, 180000);
    child.stdout.on('data', value => { stdout += value; });
    child.stderr.on('data', value => { stderr += value; });
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.on('close', status => { clearTimeout(timer); resolve({ status, stdout, stderr }); });
  });
  success(logged(log, result), log);
}
function replaceOnce(source, from, to) {
  if (source.split(from).length !== 2) throw Error(`Mutation anchor is not unique: ${from}`);
  return source.replace(from, to);
}

try {
  if (process.platform !== 'win32' || process.arch !== 'x64') throw Error('This selftest requires the actual Windows x64 MSVC toolchain');
  const mainPath=path.join(repo,'native/main.cpp'),extractionOptions={fullRawResolver:!!options['full-raw-resolver'],catalogRoot:path.join(repo,'capabilities')},extracted=extractSources(mainPath,extractionOptions);
  receipt.extraction={mainPath,mainSha256AtRead:extracted.mainSha256AtRead,scope:extracted.scope,fragments:extracted.fragments.map(({body,...meta})=>meta)};
  const inputs=[runner,...['harness.cpp','memory-boundaries.h','source.mjs','README.md'].map(name=>path.join(fixtures,name)),...extracted.dependencies];
  receipt.inputs = inputs.map(file => ({ path: file, sha256: sha(fs.readFileSync(file)) }));
  const source = extracted.header;
  const sourceDir = path.join(out, 'sources'); fs.mkdirSync(sourceDir);
  for (const file of inputs) fs.copyFileSync(file, path.join(sourceDir, path.basename(file)));
  if(!options['full-raw-resolver']){
    const harnessPath=path.join(sourceDir,'harness.cpp'),originalHarness=fs.readFileSync(harnessPath,'utf8');
    // The paired source adds a complete-miss boundary group. Prefix mode has
    // always stopped before miss enumeration; keep that existing scope.
    const prefixHarness=replaceOnce(originalHarness,'    testRawNameBoundaries();for(unsigned kind=0;kind<4;++kind){','    for(unsigned kind=0;kind<4;++kind){');
    fs.writeFileSync(harnessPath,prefixHarness);
    receipt.prefixHarnessAdapter={sourceSha256:sha(originalHarness),generatedSha256:sha(prefixHarness),onlyChange:'Exclude full-only testRawNameBoundaries call from prefix probe scenario; original four interface probe checks remain',fullHarnessUntouched:true};
  }
  fs.writeFileSync(path.join(sourceDir,'identity-source.generated.h'),source);
  fs.writeFileSync(path.join(out,'production-fragments.json'),JSON.stringify(extracted.fragments,null,2)+'\n');
  fs.writeFileSync(path.join(out,'raw-resolver.full-source-only.cpp.txt'),extracted.raw);
  fs.writeFileSync(path.join(out,'raw-cache-prefix.executed.cpp.txt'),extracted.prefix);
  fs.writeFileSync(path.join(out,'raw-cache-fill.executed.cpp.txt'),extracted.fill);
  receipt.headerSha256 = sha(source); save();

  const common = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', '/WX', '/permissive-', '/fp:precise'];
  const configurations = [
    ['Release', ['/O2', '/DNDEBUG', '/MD']],
    ['Checked', ['/Od', '/RTC1', '/Zi', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MDd']]
  ];
  function compileArguments(dir, sourceFile, executable, flags) {
    return [...common, ...flags, ...(options['full-raw-resolver']?['/DYMCC_FIXTURE_FULL_RAW']:[]), sourceFile, `/Fo${path.join(dir, 'harness.obj')}`, `/Fd${path.join(dir, 'compiler.pdb')}`, `/Fe${executable}`, '/link', '/INCREMENTAL:NO'];
  }
  receipt.compileRecipes=configurations.map(([name,flags])=>({name,program:'cl.exe',arguments:compileArguments(path.join(out,name),path.join(sourceDir,'harness.cpp'),path.join(out,name,'legion_motion_identity_scope.exe'),flags),compileMaximumMs:180000,runMaximumMs:30000,fullRawResolverMacro:!!options['full-raw-resolver']}));
  fs.writeFileSync(path.join(out,'compile-recipes.json'),JSON.stringify(receipt.compileRecipes,null,2)+'\n');
  if(options['prepare-only']){
    receipt.status='prepared-only';receipt.passed=false;receipt.finishedAt=new Date().toISOString();
    receipt.configurations=receipt.compileRecipes.map(row=>({name:row.name,status:'NOT_RUN',compileArguments:row.arguments}));
    receipt.mutationsStatus='NOT_RUN';save();console.log('PREPARED_ONLY passed=false compilerInvocations=0 businessExecutions=0');
  }else{
  let vcvars = options.vcvars;
  if (!vcvars) {
    const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe');
    const install = success(run(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], 'toolchain-discovery.log'), 'VS discovery').trim();
    vcvars = path.join(install, 'VC/Auxiliary/Build/vcvars64.bat');
  }
  if (!fs.existsSync(vcvars) || /[\r\n"%]/.test(vcvars)) throw Error('Local MSVC environment batch is unavailable or has unsupported path characters');
  const environmentCmd = path.join(out, 'compiler-environment.cmd');
  if (/[&|<>^%\r\n"]/.test(environmentCmd)) throw Error('Unsupported batch output path characters');
  fs.writeFileSync(environmentCmd, `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  // Do not persist the full inherited environment, which may contain credentials.
  const environmentResult = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/c', environmentCmd], { cwd: out, windowsHide: true, encoding: 'utf8', timeout: 30000 });
  if (environmentResult.error) throw environmentResult.error;
  const environmentText = success(environmentResult, 'MSVC setup');
  fs.writeFileSync(path.join(out, 'environment-setup-status.log'), 'MSVC environment initialized; inherited values intentionally omitted.\n');
  const env = { ...process.env };
  for (const line of environmentText.split(/\r?\n/)) {
    const at = line.indexOf('='); if (at <= 0) continue;
    const key = line.slice(0, at), existing = Object.keys(env).find(value => value.toLowerCase() === key.toLowerCase());
    env[existing || key] = line.slice(at + 1);
  }
  const compilerTemp = path.join(out, 'compiler-temp'); fs.mkdirSync(compilerTemp);
  for (const key of ['TMP', 'TEMP']) {
    const existing = Object.keys(env).find(value => value.toLowerCase() === key.toLowerCase()); env[existing || key] = compilerTemp;
  }
  receipt.compiler = { vcvars, vcToolsVersion: env.VCToolsVersion, windowsSdkVersion: env.WindowsSDKVersion, target: 'x64', language: 'C++17' };
  for (const [name, flags] of configurations) {
    const dir = path.join(out, name); fs.mkdirSync(dir);
    const executable = path.join(dir, 'legion_motion_identity_scope.exe');
    const argv = compileArguments(dir, path.join(sourceDir, 'harness.cpp'), executable, flags);
    ++receipt.compilerInvocations;await compile(argv, `${name}/compile.log`, dir, env);
    const runArguments=name==='Release'&&options['resume-release-from']?['--from',options['resume-release-from']]:[];
    ++receipt.businessExecutions;const output = success(run(executable, runArguments, `${name}/run.log`, dir, env), `${name} harness`);
    const summary = output.match(/^SUMMARY cases=(\d+) checks=(\d+)$/m);
    const caseNames = [...output.matchAll(/^PASS ([\w-]+) checks=\d+$/gm)].map(match => match[1]);
    const expectedCases=name==='Release'&&options['resume-release-from']?canonicalCases.slice(canonicalCases.indexOf(options['resume-release-from'])):canonicalCases;
    if (!summary || caseNames.length !== Number(summary[1])||JSON.stringify(caseNames)!==JSON.stringify(expectedCases)) throw Error(`${name}: incomplete semantic output`);
    receipt.configurations.push({ name, status: 'pass', cases: Number(summary[1]), checks: Number(summary[2]), caseNames,
      runArguments,expectedCases,compileArguments: argv, executableSha256: sha(fs.readFileSync(executable)) }); save();
    console.log(`${name}: PASS (${summary[1]} cases, ${summary[2]} checks; real extracted functions/fragments)`);
  }

  const controls = [
    {name:'vendor-predicate-forgets-capture-flag',case:'default-vendor-rejection-and-publication',expected:'default scope must reject FFA0 vendor collection',
      change:s=>replaceOnce(s,'const bool legionMotionCollection=allowLegionMotionCollection &&\n        legionMotionCollectionAdmitted','const bool legionMotionCollection=legionMotionCollectionAdmitted')},
    {name:'raw-hit-reuses-default-cache-for-capture',case:'cache-namespaces',expected:'capture must hit its Physical cache independently',
      change:s=>{const from=extracted.prefix;
        const changed=replaceOnce(from,'auto& cache=allowLegionMotionCollection ? g_legionMotionResolveCache : g_hidResolveCache;','auto& cache=g_hidResolveCache;');
        return replaceOnce(s,from,changed);}},
    {name:'invalidate-leaves-capture-cache',case:'invalidate-and-fill-generation-fence',expected:'invalidate must clear both scope namespaces and diagnostics',
      change:s=>replaceOnce(s,'    g_legionMotionResolveCache.clear();\n','')},
    {name:'capture-evicts-default-namespace',case:'namespace-capacity-eviction',expected:'capture capacity must evict only capture namespace',
      change:s=>{const body=extracted.fragments.find(f=>f.name==='hidHideEvictOldestResolveEntryLocked').body;
        return replaceOnce(s,body,replaceOnce(body,'auto& cache=allowLegionMotionCollection ? g_legionMotionResolveCache : g_hidResolveCache;',
          'static_cast<void>(allowLegionMotionCollection); auto& cache=g_hidResolveCache;'));}},
    {name:'fill-ignores-generation-fence',case:'invalidate-and-fill-generation-fence',expected:'stale resolver generation was filled into new scope cache',
      change:s=>replaceOnce(s,'(genAtStart == g_hidResolveCacheGeneration)','(genAtStart != 0)')},
    {name:'capture-refreshes-default-xusb-route',case:'capture-no-xusb-default-refresh',expected:'capture must not refresh XUSB/default route',
      change:s=>replaceOnce(s,'!allowLegionMotionCollection && entry.kind == HidResolveKind::Physical','entry.kind == HidResolveKind::Physical')},
    {name:'virtual-markers-bypassed',case:'virtual-marker-refusal',expected:'capture vendor must still reject actual virtual marker predicate',
      change:s=>{const body=extracted.fragments.find(f=>f.name==='hidHideIsVirtualIdentity').body;
        return replaceOnce(s,body,'static bool hidHideIsVirtualIdentity(const std::string& deviceInstanceId,const std::string& base) { static_cast<void>(deviceInstanceId); static_cast<void>(base); return false; }');}},
    {name:'capture-bypasses-base-evidence',case:'base-ancestor-evidence',expected:'capture must reject missing/unproven base ancestor',
      change:s=>replaceOnce(s,'if (base.empty()) {','if (base.empty() && !allowLegionMotionCollection) {')}
  ];
  // Each edit compiles in BOTH configurations. A crash, build failure, or wrong
  // failure message is an invalid control, never counted as a detected mutant.
  for (const control of options['normal-only']?[]:controls) {
    const mutantRoot = path.join(out, 'mutations', control.name); fs.mkdirSync(mutantRoot, { recursive: true });
    const mutated = control.change(source);
    fs.writeFileSync(path.join(mutantRoot, 'identity-source.generated.h'), mutated);
    for(const name of ['harness.cpp','memory-boundaries.h',...extracted.dependencies.map(file=>path.basename(file))])fs.copyFileSync(path.join(sourceDir,name),path.join(mutantRoot,name));
    const mutation = { name: control.name, status: 'running', expectedCase: control.case, expectedFailure: control.expected,
      mutatedHeaderSha256: sha(mutated), configurations: [] };
    receipt.mutations.push(mutation); save();
    for (const [name, flags] of configurations) {
      const dir = path.join(mutantRoot, name); fs.mkdirSync(dir);
      const executable = path.join(dir, 'mutant.exe'), base = `mutations/${control.name}/${name}`;
      const argv = compileArguments(dir, path.join(mutantRoot, 'harness.cpp'), executable, flags);
      await compile(argv, `${base}/compile.log`, dir, env);
      const result = run(executable, ['--case', control.case], `${base}/run.log`, dir, env);
      const expectedLine = `FAIL ${control.case}: ${control.expected}`;
      if (result.status !== 1 || !result.stderr.includes(expectedLine)) throw Error(`${control.name}/${name}: missing specified semantic rejection`);
      mutation.configurations.push({ name, compiled: true, status: 'detected', exitCode: result.status, failure: expectedLine,
        compileArguments: argv, executableSha256: sha(fs.readFileSync(executable)) }); save();
    }
    mutation.status = 'detected'; save(); console.log(`Mutant: ${control.name} detected in Release + Checked`);
  }
  for (const input of receipt.inputs) if (sha(fs.readFileSync(input.path)) !== input.sha256) throw Error(`Owned input changed during validation: ${input.path}`);
  const fresh=extractSources(mainPath,extractionOptions);
  if(JSON.stringify(fresh.fragments.map(f=>[f.name,f.sha256]))!==JSON.stringify(extracted.fragments.map(f=>[f.name,f.sha256])))throw Error('Bound main fragments changed during validation');
  receipt.status = 'pass';receipt.passed=true;receipt.semanticControlsStatus=options['normal-only']?'NOT_RUN':'DETECTED';receipt.mutationsStatus=options['normal-only']?'NOT_RUN':'DETECTED'; receipt.finishedAt = new Date().toISOString(); save();
  console.log(options['normal-only']?`PASS: directed normal-only Release + Checked; semantic mutants NOT_RUN. Evidence: ${out}`:`PASS: ${controls.length} semantic mutants, both configurations. Evidence: ${out}`);
  }
} catch (error) {
  receipt.status = 'fail'; receipt.error = error.message; receipt.finishedAt = new Date().toISOString(); save();
  console.error(error.stack || error.message); process.exitCode = 1;
}
