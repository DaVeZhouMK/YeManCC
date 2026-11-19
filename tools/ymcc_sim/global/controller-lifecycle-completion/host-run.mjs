import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {CASES,REPO,INFRASTRUCTURE} from './host-adapter.mjs';
import {toolchainFiles} from '../inputhost-protocol/adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
import {managedFixtureEnvironment,managedFixtureLookupGuards as captureHostProtocolLookupGuards} from '../controller-signal-evidence/environment.mjs';
const need=(v,m)=>{if(!v)throw Error(m);};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'OUTPUT_LINK');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
const HOST_CASE_NAMES=Object.freeze({"parent":["healthy-idle","pipe-break","parent-loss-during-read","parent-loss-during-write","parent-loss-during-connect","pipe-break-log-blocked","handle-exception-log-blocked","parent-loss-log-blocked","shutdown-log-blocked","release-failure-log-blocked"],"protocol":["HELLO PREPARE NEUTRAL FRAME QUIESCE RELEASE SHUTDOWN actual dispatcher","dualsense-edge current target lifecycle and barrier retry","elite current target lifecycle and barrier retry","current parser rejects all three obsolete wrong-pair inputs","output stop failure retains resources and RELEASE retry disposes","output stop failure rejects SHUTDOWN until successful retry","output stop failure prevents RECREATE from creating second target","idle stop failure prevents output stop and destruction","partial PREPARE retains objects when release barrier fails; retry gates create","frame backend failure releases and makes next frame terminal","frame backend failure with stalled output keeps resources for retry","target tuple change cannot replace retained target or release pins","idempotent PREPARE replay creates once; changed replay rejected","idle CTS capture failure keeps controller and blocks SDK Dispose","idle CTS disposal follows successful SDK return and retries same pending resource","SDK Dispose exception never authorizes idle CTS explicit disposal","SDK Dispose controller exception survives second recreate without false release","SDK Dispose context exception survives second recreate without false release","RECREATE preserves MSI owned absence requirement"]});
const HOST_NEGATIVE_NAMES=Object.freeze({"parent":["parent-loss-during-connect","parent-loss-during-read","parent-loss-during-write","parent-loss-log-blocked"],"protocol":["SDK Dispose context exception survives second recreate without false release","SDK Dispose controller exception survives second recreate without false release"]});
const HOST_LEGACY_FAILURE_NAMES=Object.freeze(["flag rejected wrong persona/profile: (dualshock4, dualshock-4-v2)","flag rejected wrong persona/profile: (steamdeck, wrong-profile)","flag rejected wrong persona/profile: (xbox360, xbox-360-wired)"]);
// Deliberately bounded C# declaration reader; no execution, imports, or receipt dependency.
export function declaredLegacyCaseNames(source) {
  const fail = message => { throw new Error('declaredLegacyCaseNames: ' + message); };
  if (typeof source !== 'string') fail('source must be a string');
  const lex = /\s+|\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|(?:\$@|@\$|@)"(?:[^"]|"")*"|\$?"(?:\\[\s\S]|[^"\\\r\n])*"|'(?:\\[\s\S]|[^'\\\r\n])'|[A-Za-z_]\w*|\d+|[^\s]/gy;
  const tree = [], stack = [{ items: tree }], closing = { '(': ')', '[': ']', '{': '}' };
  for (let m; (m = lex.exec(source));) {
    const raw = m[0], list = stack.at(-1).items;
    if (/^\s|^\/\/|^\/\*/.test(raw)) continue;
    if (raw === '#' || source.startsWith('/*', m.index) || /^["']$/.test(raw) || source.startsWith('"""', m.index)) fail('unsupported or unfinished lexical form at ' + m.index);
    if (Object.hasOwn(closing, raw)) {
      const group = { open: raw, items: [] }; list.push(group); stack.push(group);
      if (stack.length > 128) fail('declaration nesting too deep');
    } else if (')]}'.includes(raw)) {
      if (stack.length === 1 || closing[stack.pop().open] !== raw) fail('unbalanced delimiters');
    } else list.push(/^(?:["'@$])/.test(raw) ? { text: raw } : raw);
  }
  if (stack.length !== 1) fail('unfinished delimiters');
  const group = (x, open) => x?.open === open;
  const empty = (x, open) => group(x, open) && !x.items.length;
  const literal = x => typeof x?.text === 'string' && /^(?:"|@")/.test(x.text);
  const decode = x => {
    if (!literal(x)) fail('expected a non-interpolated string literal');
    try { return x.text.startsWith('@') ? x.text.slice(2, -1).replace(/""/g, '"') : JSON.parse(x.text); }
    catch { fail('unsupported C# string escape'); }
  };
  const split = items => {
    const chunks = [[]]; for (const item of items) item === ',' ? chunks.push([]) : chunks.at(-1).push(item);
    if (!chunks.at(-1).length) chunks.pop(); // C# initializer trailing comma; arguments checked separately.
    if (!chunks.length || chunks.some(x => !x.length)) fail('empty declaration element');
    return chunks;
  };
  const value = xs => {
    if (xs.length === 1 && literal(xs[0])) return decode(xs[0]);
    if (xs.length === 1 && ['true', 'false', 'null'].includes(xs[0])) return JSON.parse(xs[0]);
    if (xs.length === 1 && /^\d+$/.test(xs[0]) && Number(xs[0]) <= 2147483647) return Number(xs[0]);
    if (xs.length === 1 && group(xs[0], '(')) {
      const pair = split(xs[0].items);
      if (xs[0].items.at(-1) === ',' || pair.length !== 2 || pair.some(p => p.length !== 1 || !literal(p[0]))) fail('only two-string tuples supported');
      return { tuple: pair.map(p => decode(p[0])) };
    }
    if (xs.length === 7 && xs.slice(0, 6).join('') === 'Array.Empty<int>' && empty(xs[6], '(')) return [];
    if (xs.length === 2 && xs[0] === 'new' && group(xs[1], '{')) {
      const object = Object.create(null);
      for (const field of split(xs[1].items)) {
        if (field.length !== 3 || !/^[A-Za-z_]\w*$/.test(field[0]) || field[1] !== '=' || Object.hasOwn(object, field[0])) fail('unsupported anonymous object field');
        object[field[0]] = value(field.slice(2));
      }
      return object;
    }
    fail('unsupported foreach initializer value');
  };
  const csharpText = v => {
    if (typeof v === 'string') return v;
    if (typeof v === 'boolean') return v ? 'True' : 'False';
    if (v?.tuple) return '(' + v.tuple.join(', ') + ')';
    fail('unsupported C# concatenation value');
  };
  const jsonText = v => {
    const safe = x => x === null || typeof x === 'number' || typeof x === 'boolean' ||
      (typeof x === 'string' ? /^[A-Za-z0-9 ._:\/(),?-]*$/.test(x) :
        Array.isArray(x) ? !x.length : !Object.hasOwn(x, 'tuple') && Object.entries(x).every(([k, y]) => /^[A-Za-z_]\w*$/.test(k) && safe(y)));
    if (!safe(v)) fail('unsupported JsonSerializer escaping or value');
    return JSON.stringify(v);
  };
  const mains = [];
  const visit = (xs, fn) => xs.forEach((x, i) => { fn(x, i, xs); if (x?.items) visit(x.items, fn); });
  visit(tree, (x, i, xs) => { if (x === 'Main') { if (xs[i - 2] !== 'static' || xs[i - 1] !== 'int' || !empty(xs[i + 1], '(') || !group(xs[i + 2], '{')) fail('unsupported Main declaration'); mains.push(xs[i + 2].items); } });
  if (mains.length !== 1) fail('expected exactly one Main');
  const main = mains[0], names = []; let seen = 0, all = 0;
  visit(main, (x, i, items) => { if (x === 'Test' && group(items[i + 1], '(')) all++; });
  const register = (xs, i, loop) => {
    if (xs[i] !== 'Test' || !group(xs[i + 1], '(') || xs[i + 2] !== ';') fail('unsupported Test statement');
    const args = split(xs[i + 1].items);
    if (xs[i + 1].items.at(-1) === ',' || args.length !== 2 || args[1].length !== 4 || !empty(args[1][0], '(') || args[1][1] !== '=' || args[1][2] !== '>' || !group(args[1][3], '{')) fail('expected Test(name, () => { body })');
    const expr = args[0], prefix = decode(expr[0]); seen++;
    if (expr.length === 1) names.push(...(loop ? loop.values.map(() => prefix) : [prefix]));
    else if (loop && expr[1] === '+' && expr.length === 3 && expr[2] === loop.name) names.push(...loop.values.map(v => prefix + csharpText(v)));
    else if (loop && expr.length === 6 && expr.slice(1, 5).join('') === '+JsonSerializer.Serialize' && group(expr[5], '(') && expr[5].items.length === 1 && expr[5].items[0] === loop.name) names.push(...loop.values.map(v => prefix + jsonText(v)));
    else fail('unsupported Test name expression');
    return i + 3;
  };
  // Registration identity is independent of Main's setup, summary printing and wrapper layout.
  const hasCall = xs => { let found = false; visit(xs, (x, i, items) => { if (x === 'Test' && group(items[i + 1], '(')) found = true; }); return found; };
  const scan = (body, outerLoop) => {
    for (let i = 0; i < body.length;) {
      if (body[i] === 'Test' && group(body[i + 1], '(')) { i = register(body, i, outerLoop); continue; }
      if (body[i] === 'foreach' && group(body[i + 1], '(')) {
        const block = body[i + 2];
        if (group(block, '{') && !hasCall(block.items)) { i += 3; continue; }
        if (outerLoop) fail('nested registration loops require review');
        const h = body[i + 1].items, offset = h.length === 8 && h[4] === 'object' && h[5] === '?' ? 6 : 4;
        if (h.length !== offset + 2 || h[0] !== 'var' || !/^[A-Za-z_]\w*$/.test(h[1]) || h[2] !== 'in' || h[3] !== 'new' || !empty(h[offset], '[') || !group(h[offset + 1], '{')) fail('unsupported foreach declaration');
        const loop = { name: h[1], values: split(h[offset + 1].items).map(value) };
        if (group(block, '{')) { scan(block.items, loop); i += 3; }
        else i = register(body, i + 2, loop);
        continue;
      }
      if (body[i]?.items) scan(body[i].items, outerLoop);
      i++;
    }
  };
  scan(main);
  if (seen !== all) fail('unrecognized or nested Test reference');
  if (!names.length || names.some(n => !n.length) || new Set(names).size !== names.length) fail('empty or duplicate case names');
  return names;
}

export function validateHostSourceBindings(rows,id,sourcePins,{name='receipt'}={}){
 const spec=CASES[id];need(spec,'UNREVIEWED_CASE');
 const key=p=>{need(typeof p==='string'&&p.trim().length>0,'SOURCE_BINDING_PATH_REQUIRED:'+name);return path.resolve(REPO,p).toLowerCase();};
 const wanted=spec.inputs.map(key);need(wanted.length>0&&new Set(wanted).size===wanted.length,'SOURCE_BINDING_DECLARATION_INVALID:'+name);
 need(Array.isArray(sourcePins),'SOURCE_BINDING_PINS_REQUIRED:'+name);
 const pinned=new Map();for(const p of sourcePins){need(p&&typeof p.sha256==='string'&&/^[a-f0-9]{64}$/.test(p.sha256),'SOURCE_BINDING_PIN_INVALID:'+name);const k=key(p.path);need(!pinned.has(k),'SOURCE_BINDING_PIN_DUPLICATE:'+name);pinned.set(k,p.sha256);}
 need(wanted.every(k=>pinned.has(k)),'SOURCE_BINDING_PIN_MISSING:'+name);
 need(Array.isArray(rows),'PRODUCER_SOURCE_SET_MISMATCH:'+name);
 const actual=rows.map(p=>{need(p&&typeof p==='object','PRODUCER_SOURCE_SET_MISMATCH:'+name);return key(p.path);});
 need(actual.length===wanted.length&&new Set(actual).size===wanted.length&&actual.every(k=>wanted.includes(k)),'PRODUCER_SOURCE_SET_MISMATCH:'+name);
 need(rows.every((p,i)=>p.sha256===pinned.get(actual[i])),'PRODUCER_INPUT_NOT_BOUND:'+name);
 return true;
}

export function validateHostManagedEnvironmentReceipt(r,out,spec){
 const m=r.managedEnvironment,root=path.resolve(out),key=p=>path.resolve(p).toLowerCase();
 need(m?.schemaVersion===1&&m.mode==='fixed-owned-managed-v1','MANAGED_ENVIRONMENT_PROOF_MISSING');
 const expected=managedFixtureEnvironment({ambient:process.env,dotnetRoot:spec.dotnetRoot,out:root});
 const variables=Object.fromEntries(Object.entries(expected).sort(([a],[b])=>a.localeCompare(b)));
 need(JSON.stringify(m.variables)===JSON.stringify(variables),'MANAGED_ENVIRONMENT_VARIABLES_CHANGED');
 const platform={platform:process.platform,arch:process.arch,release:os.release(),version:os.version()};
 need(JSON.stringify(m.platform)===JSON.stringify(platform),'MANAGED_ENVIRONMENT_PLATFORM_CHANGED');
 const normalized=Object.fromEntries(Object.entries(variables).map(([name,value])=>[name,value.replaceAll(root,'{fixtureRoot}')]));
 need(m.normalizedIdentity===crypto.createHash('sha256').update(JSON.stringify({variables:normalized,platform})).digest('hex'),'MANAGED_ENVIRONMENT_IDENTITY_CHANGED');
 const verify=(row,wanted)=>{need(row&&key(row.path)===key(wanted)&&Number.isInteger(row.bytes)&&/^[a-f0-9]{64}$/.test(row.sha256??''),'MANAGED_PROOF_PIN_INVALID');const bytes=fs.readFileSync(row.path);need(bytes.length===row.bytes&&crypto.createHash('sha256').update(bytes).digest('hex')===row.sha256,'MANAGED_PROOF_CHANGED');return bytes.toString('utf8').replace(/^\uFEFF/,'');};
 need(Array.isArray(m.selectors)&&m.selectors.length===3,'MANAGED_SELECTOR_SET_INVALID');
 const texts={};for(const name of ['global.json','Directory.Build.props','Directory.Build.targets']){const rows=m.selectors.filter(row=>key(row.path)===key(path.join(root,name)));need(rows.length===1,'MANAGED_SELECTOR_MISSING');texts[name]=verify(rows[0],path.join(root,name));}
 need(JSON.stringify(JSON.parse(texts['global.json']))===JSON.stringify({sdk:{version:spec.sdkVersion,rollForward:'disable',allowPrerelease:false}}),'MANAGED_SDK_SELECTOR_CHANGED');
 need(texts['Directory.Build.props']==='<Project><PropertyGroup><UseSharedCompilation>false</UseSharedCompilation><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>'&&texts['Directory.Build.targets']==='<Project />','MANAGED_LOCAL_IMPORT_SELECTOR_CHANGED');
 const versions=relative=>fs.readdirSync(path.join(spec.dotnetRoot,relative),{withFileTypes:true}).filter(e=>e.isDirectory()).map(e=>e.name).sort();
 const membership={sdk:versions('sdk'),framework:versions('shared/Microsoft.NETCore.App'),fxr:versions('host/fxr')};need(JSON.stringify(m.resolverMembership)===JSON.stringify(membership),'MANAGED_RESOLVER_MEMBERSHIP_CHANGED');
 const fxr=path.join(spec.dotnetRoot,'host/fxr',spec.runtimeVersion,'hostfxr.dll');verify(m.hostfxr,fxr);need(spec.pinned.some(row=>key(row.path)===key(fxr)&&row.sha256===m.hostfxr.sha256),'MANAGED_HOSTFXR_UNDECLARED');
 need(m.declaredSdk===spec.sdkVersion&&m.declaredRuntime===spec.runtimeVersion,'MANAGED_DECLARED_VERSION_CHANGED');
 const c=r.compilerInputs;need(c&&Array.isArray(c.commandLines)&&Array.isArray(c.properties)&&c.commandLines.length===r.configs.length&&c.properties.length===r.configs.length,'MANAGED_CSC_DETAILS_MISSING');
 const properties=[];
 for(const config of r.configs){
  const commandFile=path.join(root,'compiler-commandline-'+config+'.txt'),propertyFile=path.join(root,'compiler-properties-'+config+'.txt');
  const commands=c.commandLines.filter(row=>key(row.path)===key(commandFile)),props=c.properties.filter(row=>key(row.path)===key(propertyFile));need(commands.length===1&&props.length===1,'MANAGED_CSC_DETAILS_SET_INVALID');
  const command=verify(commands[0],commandFile);need(command.trim().length>0&&command.includes('/reference:'),'MANAGED_CSC_ARGUMENTS_EMPTY');
  const values={};for(const line of verify(props[0],propertyFile).split(/\r?\n/)){if(!line.trim())continue;const [name,...parts]=line.split('\t');need(!Object.hasOwn(values,name),'MANAGED_CSC_PROPERTY_DUPLICATE');values[name]=parts.join('\t');}
  need(values.NETCoreSdkVersion===spec.sdkVersion&&values.RuntimeFrameworkVersion===spec.runtimeVersion&&values.RollForward==='Disable','MANAGED_ACTUAL_BUILD_VERSION_CHANGED');
  need(values.ProvideCommandLineArgs==='true'&&values.MSBuildEnableWorkloadResolver==='false'&&values.SkipCompilerExecution!=='true','MANAGED_CSC_BUILD_POLICY_CHANGED');
  need(key(values.DirectoryBuildPropsPath)===key(path.join(root,'Directory.Build.props'))&&key(values.DirectoryBuildTargetsPath)===key(path.join(root,'Directory.Build.targets')),'MANAGED_ACTUAL_IMPORT_PATH_CHANGED');
  properties.push({config,values});
 }
 need(r.processes.every(p=>p.toolchain?.environmentMode==='fixed-owned-managed-v1'&&p.toolchain.environmentQualified===true&&key(p.command)===key(path.join(spec.dotnetRoot,'dotnet.exe'))),'MANAGED_COMMAND_ENVIRONMENT_CHANGED');
 return {mode:m.mode,normalizedIdentity:m.normalizedIdentity,selectors:3,properties,resolverMembership:membership,nativeResolverSelectionQualified:membership.fxr.length===1&&membership.fxr[0]===spec.runtimeVersion&&membership.framework.length===1&&membership.framework[0]===spec.runtimeVersion};
}

export function validateHostCompilerInputReceipt(r,out,tools){
 const c=r.compilerInputs,root=path.resolve(out),key=p=>path.resolve(p).toLowerCase();
 need(c?.status==='RECORDED'&&c.referenceItem==='ReferencePathWithRefAssemblies','COMPILER_INPUT_PROOF_MISSING');
 need(Array.isArray(r.configs)&&r.configs.length>0&&r.configs.every(v=>['Release','Debug'].includes(v))&&new Set(r.configs).size===r.configs.length,'COMPILER_CONFIG_SET_INVALID');
 const names=r.configs.map(config=>'evaluated-inputs-'+config+'.txt').sort();
 need(Array.isArray(c.ledgerFiles)&&c.ledgerFiles.length===names.length&&new Set(c.ledgerFiles.map(row=>row.path)).size===names.length,'COMPILER_LEDGER_SET_INVALID');
 const external=new Map(tools.map(row=>[key(row.path),row.sha256]));
 const verifyPin=(row,wanted)=>{
  need(row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&key(row.path)===key(wanted)&&/^[a-f0-9]{64}$/.test(row.sha256??'')&&Number.isInteger(row.bytes),'COMPILER_RECEIPT_PIN_INVALID');
  const stat=fs.lstatSync(row.path);need(stat.isFile()&&!stat.isSymbolicLink(),'COMPILER_RECEIPT_NOT_REGULAR');
  const bytes=fs.readFileSync(row.path);need(bytes.length===row.bytes&&crypto.createHash('sha256').update(bytes).digest('hex')===row.sha256,'COMPILER_RECEIPT_CHANGED');return bytes;
 };
 const manifestPath=path.join(root,'evaluated-compiler-inputs.json');
 const graph=JSON.parse(verifyPin(c.manifest,manifestPath).toString('utf8').replace(/^\uFEFF/,''));
 need(graph.schemaVersion===1&&graph.ledgers===names.length&&Array.isArray(graph.rows),'COMPILER_GRAPH_INVALID');
 const expected=[];
 for(const name of names){
  const file=path.join(root,name),pins=c.ledgerFiles.filter(row=>key(row.path)===key(file));need(pins.length===1,'COMPILER_LEDGER_NAME_MISMATCH');
  const bytes=verifyPin(pins[0],file);
  for(const line of bytes.toString('utf8').replace(/^\uFEFF/,'').split(/\r?\n/)){
   if(!line.trim())continue;const [kind,...parts]=line.split('\t'),p=parts.join('\t');
   if(kind==='Import'&&p===''&&parts.length<=1)continue;
   need(['Compile','Reference','Analyzer','Project','Import'].includes(kind)&&p&&path.isAbsolute(p),'COMPILER_LEDGER_ROW_INVALID');
   const full=path.resolve(p),relative=path.relative(root,full),owned=relative===''||!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);
   const stat=fs.lstatSync(full);need(stat.isFile()&&!stat.isSymbolicLink(),'COMPILER_INPUT_NOT_REGULAR');
   const digest=hash(full);need(owned||external.get(key(full))===digest,'COMPILER_INPUT_UNDECLARED_OR_CHANGED');
   expected.push({ledger:name,kind,path:full,sha256:digest});
  }
  need(expected.some(row=>row.ledger===name&&row.kind==='Compile')&&expected.some(row=>row.ledger===name&&row.kind==='Reference'),'COMPILER_REQUIRED_INPUTS_MISSING');
 }
 const canonical=rows=>rows.map(row=>JSON.stringify([row.ledger,row.kind,typeof row.path==='string'?key(row.path):null,row.sha256])).sort();
 need(JSON.stringify(canonical(graph.rows))===JSON.stringify(canonical(expected)),'COMPILER_LEDGER_GRAPH_MISMATCH');
 need(c.ledgers===names.length&&c.compileRows===expected.filter(row=>row.kind==='Compile').length&&c.referenceRows===expected.filter(row=>row.kind==='Reference').length,'COMPILER_INPUT_SUMMARY_MISMATCH');
 return {ledgers:names.length,rows:expected.length,compileRows:c.compileRows,referenceRows:c.referenceRows};
}

export function validateHostLifecycleReceipt(r,mode,{negative=false,name='receipt'}={}){
 const wanted=HOST_CASE_NAMES[mode];need(wanted,'UNREVIEWED_CASE');
 const legacyNames=mode==='protocol'&&!negative?declaredLegacyCaseNames(fs.readFileSync(sourcePath(REPO,'tools/msi_steamdeck_release_fence_1726_selftest.cs'),'utf8')):null;
 need(r.sourceStable===true&&Array.isArray(r.profiles)&&r.profiles.length===2&&r.profiles.every(p=>p&&['Release','Debug'].includes(p.config))&&new Set(r.profiles.map(p=>p.config)).size===2,'SOURCE_OR_PROFILES_INVALID:'+name);
 for(const p of r.profiles){
  const result=p.result;
  need(result&&result.hardwareIo===false&&Array.isArray(result.cases)&&result.cases.length===wanted.length,'CASE_RECEIPTS_INCOMPLETE:'+name);
  need(result.cases.every(t=>t&&wanted.includes(t.name)&&typeof t.pass==='boolean')&&new Set(result.cases.map(t=>t.name)).size===wanted.length,'CASE_RECEIPT_SET_MISMATCH:'+name);
  const failed=result.cases.filter(t=>!t.pass).map(t=>t.name).sort();
  if(negative)need(p.exit===1&&result.passed===false&&JSON.stringify(failed)===JSON.stringify([...HOST_NEGATIVE_NAMES[mode]].sort()),'WRONG_PARENT_NEGATIVE_FAILURES:'+p.config);
  else{
   need(p.exit===0&&result.passed===true&&failed.length===0,'CURRENT_CASE_FAILED:'+p.config);
   if(mode==='parent')for(const row of result.cases.filter(c=>c.name.endsWith('-log-blocked'))){
    const origin=row.name==='handle-exception-log-blocked'?4:['parent-loss-log-blocked','shutdown-log-blocked'].includes(row.name)?0:5;
    const terminal=row.name==='release-failure-log-blocked'?'terminal-cleanup-failed':'terminal-cleanup-complete';
    need(row.logEntered===true&&row.releasedWhileLogBlocked===1&&row.releasesAfterLog===1&&row.terminalStillWaitingForLog===true&&row.exit===origin&&
     Array.isArray(row.fatal)&&row.fatal.filter(k=>k===terminal).length===1,'TERMINAL_LOG_BLOCKED_RELEASE_UNPROVEN:'+p.config+':'+row.name);
   }
   if(mode==='protocol'){
    const old=result.legacyFailures?.map(t=>t.name).sort();
    need(Array.isArray(result.legacyCases)&&result.legacyCases.length===57&&JSON.stringify(old)===JSON.stringify([...HOST_LEGACY_FAILURE_NAMES].sort()),'UNREVIEWED_LEGACY_FAILURE_CHANGE');
    need(result.legacyCases.every(t=>t&&typeof t.name==='string'&&t.name.trim().length>0)&&new Set(result.legacyCases.map(t=>t.name)).size===result.legacyCases.length,'LEGACY_CASE_NAMES_INVALID:'+p.config);
    need(JSON.stringify(result.legacyCases.map(t=>t.name).sort())===JSON.stringify([...legacyNames].sort()),'LEGACY_CASE_SET_MISMATCH:'+p.config);
    need(result.legacyExit===0&&result.legacyCases.every(t=>t&&typeof t.pass==='boolean')&&JSON.stringify(result.legacyCases.filter(t=>!t.pass).map(t=>t.name).sort())===JSON.stringify(old),'LEGACY_FAILURE_RECEIPTS_MISMATCH:'+p.config);
   }
  }
 }
 return true;
}
export async function execute(id,root){
 const c=CASES[id];need(c,'UNREVIEWED_CASE');root=path.resolve(root);fs.mkdirSync(path.dirname(root),{recursive:true});fs.mkdirSync(root);
 const sources=[...new Set([...c.inputs,...INFRASTRUCTURE])].map(p=>({path:p,sha256:hash(sourcePath(REPO,p))}));
 const tools=toolchainFiles().map(p=>({path:p,sha256:hash(p)}));
 const spec=read(sourcePath(REPO,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'));
  const steps=[],parentLookupGuards=[];
 async function suite(name,negative=false){
  const out=path.join(root,name);const args=[sourcePath(REPO,c.runner),'--out',out,...(negative?['--fault',c.mode==='parent'?'ignore-parent-cancellation':'drop-dispose-retention']:[])];
   const lookup=c.mode==='parent'?{schemaVersion:1,stage:'before',atUtc:new Date().toISOString(),snapshot:captureHostProtocolLookupGuards(REPO,path.dirname(root),{negativeGroup:'negative-cancellation-omitted'})}:null;
   const lookupBefore=path.join(root,name+'.lookup-before.json');
   if(lookup)fs.writeFileSync(lookupBefore,JSON.stringify(lookup,null,2)+'\n',{flag:'wx'});
  const environment=c.mode==='protocol'?managedFixtureEnvironment({ambient:process.env,dotnetRoot:spec.dotnetRoot,out}):{...process.env,PATH:spec.dotnetRoot+path.delimiter+process.env.PATH,DOTNET_ROOT:spec.dotnetRoot};
  const proc=await runProcess(process.execPath,args,{cwd:REPO,timeoutMs:120000,env:environment});
  steps.push({name,args,...proc});fs.writeFileSync(path.join(root,name+'.stdout.log'),proc.stdout);fs.writeFileSync(path.join(root,name+'.stderr.log'),proc.stderr);
   if(lookup){
    const after={schemaVersion:1,stage:'after',atUtc:new Date().toISOString(),snapshot:captureHostProtocolLookupGuards(REPO,path.dirname(root),{negativeGroup:'negative-cancellation-omitted'})};
    const lookupAfter=path.join(root,name+'.lookup-after.json');fs.writeFileSync(lookupAfter,JSON.stringify(after,null,2)+'\n',{flag:'wx'});
    need(JSON.stringify(lookup.snapshot)===JSON.stringify(after.snapshot),'PARENT_LOOKUP_INPUTS_CHANGED:'+name);
    parentLookupGuards.push({name,before:{path:lookupBefore,sha256:hash(lookupBefore)},after:{path:lookupAfter,sha256:hash(lookupAfter)}});
   }
  need(!proc.timedOut&&!proc.launchError&&!proc.signal&&!proc.outputTruncated,'RUNNER_NOT_COMPLETED:'+name);
  const r=read(path.join(out,'result.json'));
  validateHostSourceBindings(r.sources,id,sources,{name});
   if(c.mode==='parent'){
    const managedFiles=['tools/ymcc_sim/global/controller-signal-evidence/environment.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'];
    const key=file=>path.resolve(REPO,file).toLowerCase(),pins=new Map(sources.map(row=>[key(row.path),row.sha256]));
    need(Array.isArray(r.managedSourceInputs)&&r.managedSourceInputs.length===managedFiles.length&&
     new Set(r.managedSourceInputs.map(row=>key(row.path))).size===managedFiles.length&&
     r.managedSourceInputs.every(row=>managedFiles.some(file=>key(file)===key(row.path))&&row.sha256===pins.get(key(row.path))),
     'PARENT_MANAGED_SOURCE_BINDING_INVALID:'+name);
    need(r.generatedStable===true,'PARENT_GENERATED_INPUTS_CHANGED:'+name);
    validateHostManagedEnvironmentReceipt(r,out,spec);
    validateHostCompilerInputReceipt(r,out,tools);
   }
  if(c.mode==='protocol')validateHostManagedEnvironmentReceipt(r,out,spec);
  if(c.mode==='protocol')validateHostCompilerInputReceipt(r,out,tools);
  validateHostLifecycleReceipt(r,c.mode,{negative,name});
  need(proc.exitCode===(negative?1:0)&&r.status===(negative?'FAIL':'PASS'),'WRONG_FINAL_STATUS:'+name);
  return {path:path.join(out,'result.json'),sha256:hash(path.join(out,'result.json')),profiles:r.profiles.map(p=>({config:p.config,exit:p.exit,cases:p.result.cases,legacyFailures:p.result.legacyFailures??null})),expectedRejection:negative};
 }
 try{
  write(path.join(root,'inputs-before.json'),{sources,tools});
  const positive=await suite('positive');const negative=await suite(c.mode==='parent'?'negative-cancellation-omitted':'negative-dispose-retention-omitted',true);
  need(sources.every(s=>hash(sourcePath(REPO,s.path))===s.sha256),'SOURCE_CHANGED_DURING_RUN');need(tools.every(t=>hash(t.path)===t.sha256),'TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});
  const result={schemaVersion:1,id,status:'PASS',passed:true,positive,negative,legacyWholeFixtureStatus:c.mode==='protocol'?'FAIL_RETAINED_PROTOCOL_REJECTION_DETAIL_DRIFT':null,sourceHashes:[...sources,...walk(root).map(p=>({path:p,sha256:hash(p)}))],toolchain:tools,boundary:{productMainExecuted:false,sdkLoaded:false,hardwareIo:false,realBclPipes:c.mode==='parent',ownedChildProcesses:c.mode==='parent',physicalReturnValidated:false,fullLifecycleAcceptance:false,cpuReductionMeasured:false}};
   if(c.mode==='parent')result.managedLookupGuards=parentLookupGuards;
   write(path.join(root,'result.json'),result);return result;
 }catch(error){
  try{write(path.join(root,'failure.json'),{status:'FAIL',error:error.message,steps,originalFailurePreserved:true});}
  catch(recordingError){
   try{
    const output=value=>({utf8Bytes:Buffer.byteLength(value),sha256:crypto.createHash('sha256').update(value).digest('hex')});
    console.error('YMCC_SIM_LIFECYCLE_FAILURE_RECORDING_FAILED '+JSON.stringify({
     schemaVersion:1,status:'INCOMPLETE_RECORDING',file:path.join(root,'failure.json'),durableFallbackClaimed:false,
     originalError:{code:error?.code??null,message:String(error?.message??error).slice(0,1600)},
     recordingError:{code:recordingError?.code??null,message:String(recordingError?.message??recordingError).slice(0,1600)},
     steps:steps.map(step=>({name:step.name,pid:step.pid,exitCode:step.exitCode,signal:step.signal,timedOut:step.timedOut,
      launchError:step.launchError,outputTruncated:step.outputTruncated,durationMs:step.durationMs,
      stdout:output(step.stdout),stderr:output(step.stderr)}))
    }));
   }catch{}
  }
  throw error;
 }
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{need(process.argv.length===6&&process.argv[2]==='--case'&&process.argv[4]==='--evidence-dir','CASE_AND_EVIDENCE_REQUIRED');const r=await execute(process.argv[3],process.argv[5]);console.log(JSON.stringify({id:r.id,status:r.status,profiles:2,groupsPerProfile:HOST_CASE_NAMES[CASES[r.id].mode].length,hardwareIo:false}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=1;}
}
