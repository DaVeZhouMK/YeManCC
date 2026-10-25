// Source-exact Win5 slot refresh observation regression. No SDK/Host/device execution.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),workspace=path.resolve(repo,'../../..');
const options={current:path.join(repo,'native/main.cpp'),baseline:null,out:path.join(workspace,'Build/Validation/VirtualInput-Submission-R113-20261016/win5-slot-outcome/regression')};
for(let i=2;i<process.argv.length;i++){const arg=process.argv[i];if(arg==='--help'){console.log('node tools/win5_slot_refresh_outcome_selftest.mjs [--current MAIN_CPP] [--baseline OLD_MAIN_CPP] [--out DIR]');process.exit(0);}assert(['--current','--baseline','--out'].includes(arg),'Unknown '+arg);assert(process.argv[i+1]&&!process.argv[i+1].startsWith('--'),'Missing path');options[arg.slice(2)]=path.resolve(process.argv[++i]);}
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const load=file=>{const bytes=fs.readFileSync(file);return {file,bytes,text:bytes.toString('utf8'),sha256:sha(bytes)};};
function extract(source){
 const text=source.text,masked=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,s=>s.replace(/[^\r\n]/g,' ')),ranges=[];
 function end(brace){let depth=0;for(let i=brace;i<masked.length;i++){if(masked[i]==='{')depth++;if(masked[i]==='}'&&--depth===0)return i+1;}throw Error('Unclosed source block');}
 function record(name,start,finish){assert(start>=0&&finish>start,name);const code=text.slice(start,finish);ranges.push({name,startLine:text.slice(0,start).split('\n').length,endLine:text.slice(0,finish).split('\n').length,sha256:sha(code),code});return code;}
 function bounds(name){const match=new RegExp('static [^;{}\\n]+\\b'+name+'\\([^;{}]*\\)\\s*\\{').exec(text);assert(match,name);const begin=match.index;return [begin,end(masked.indexOf('{',begin))];}
 function fn(name){return record(name,...bounds(name));}
 const helper=bounds('ymccOpenXInputCycleDisplacedPhysical'),hasOutcome=text.includes('struct YmccSlotPhysicalRefreshOutcome {'),begin=hasOutcome?text.indexOf('struct YmccSlotPhysicalRefreshOutcome {'):helper[0];
 const refresh=record('physical refresh helper and optional outcome',begin,helper[1]);
 const budgetStart=text.indexOf('struct YmccSlotIntentBudget {'),retry=text.indexOf('static constexpr ULONGLONG kYmccSlotRetryCooldownMs',budgetStart),budgetEnd=text.indexOf('\n',retry);
 let budget=record('existing slot intent budget declarations',budgetStart,budgetEnd)+'\n';
 for(const name of ['kYmccSlotSegmentMaxMs','kYmccSlotProbeSpacingMs']){const m=text.match(new RegExp('^static constexpr ULONGLONG '+name+' = [^;]*;','m'));assert(m,name);budget+=record(name,m.index,m.index+m[0].length)+'\n';}
 for(const name of ['ymccSlotIntentNoteReadbackSlot0','ymccSlotIntentNoteConverged','ymccSlotIntentReserveOperation','ymccSlotIntentAtLimit','ymccSlotIntentExhaust','ymccSlotIntentOperations'])budget+=fn(name)+'\n';
 const [stepStart,stepEnd]=bounds('ymccOpenXInputSlotHandoffStep'),stage=text.indexOf('    case 4: {',stepStart);assert(stage>stepStart&&stage<stepEnd);const brace=masked.indexOf('{',stage),close=end(brace);assert(close<stepEnd);const body=record('slot handoff case 4 exact body',brace+1,close-1);
 return {hasOutcome,parts:{REFRESH:refresh,BUDGET:budget,CASE4:body},ranges};
}
fs.mkdirSync(options.out,{recursive:true});const run=path.join(options.out,'runs',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid);fs.mkdirSync(run,{recursive:true});
const save=(name,obj)=>fs.writeFileSync(path.join(run,name),JSON.stringify(obj,null,2)+'\n'),sources={current:load(options.current)};if(options.baseline)sources.baseline=load(options.baseline);
const slices=Object.fromEntries(Object.entries(sources).map(([name,source])=>[name,extract(source)])),fixture=load(path.join(repo,'tools/fixtures/win5_slot_refresh_outcome_fixture.cpp')),json=load(path.join(repo,'deps/json/json.hpp'));
assert(slices.current.hasOutcome,'Current production source lacks physical refresh outcome; only --baseline may carry old source');
const inc=path.join(run,'extracted');fs.mkdirSync(inc,{recursive:true});fs.writeFileSync(path.join(inc,'json.hpp'),json.bytes);
for(const [name,slice]of Object.entries(slices))for(const range of slice.ranges)fs.writeFileSync(path.join(inc,name+'-'+range.name.replace(/[^\w-]/g,'_')+'.txt'),range.code);
save('source-bindings.json',{sources:Object.fromEntries(Object.entries(sources).map(([name,source])=>[name,{file:source.file,sha256:source.sha256,hasOutcome:slices[name].hasOutcome,ranges:slices[name].ranges.map(({code,...range})=>range)}])),fixture:{file:fixture.file,sha256:fixture.sha256},dependencies:[{file:json.file,sha256:json.sha256}],productionRewrites:[],scope:'Actual physical-refresh helper + case4 + budget functions; inert AssignSlot/GetUserIndex/cycle/clock/scheduling and admitted gate; no full handoff scheduling or device acceptance.'});
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert(fs.existsSync(vcvars));
const quote=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
function command(label,exe,args,dir,expected){const r=spawnSync(exe,args,{cwd:dir,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(dir,label+'.stdout.log'),r.stdout??'');fs.writeFileSync(path.join(dir,label+'.stderr.log'),r.stderr??'');fs.writeFileSync(path.join(dir,label+'.command.json'),JSON.stringify({exe,args,cwd:dir,status:r.status,expected,error:r.error?String(r.error):null},null,2));assert(!r.error&&r.status===expected,label+' failed: '+r.status+'; '+dir);return r.stdout;}
const results={},executed={};
for(const [name,slice]of Object.entries(slices))for(const diagnostic of [false,true]){
 const label=name+'-diagnostic-'+(diagnostic?'on':'off'),dir=path.join(run,label);fs.mkdirSync(dir,{recursive:true});let cpp=fixture.text;
 for(const [key,code]of Object.entries(slice.parts)){const mark='@@'+key+'@@';assert.equal(cpp.split(mark).length,2,key);cpp=cpp.replace(mark,()=>code);}assert(!/@@\w+@@/.test(cpp));fs.writeFileSync(path.join(dir,'fixture.cpp'),cpp);
 const compile=path.join(dir,'compile.cmd');fs.writeFileSync(compile,'@echo off\r\ncall '+quote(vcvars)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /WX /O2 /DYMCC_SLOT_REFRESH_OUTCOME='+(slice.hasOutcome?1:0)+(diagnostic?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC':'')+' '+quote(path.join(dir,'fixture.cpp'))+' /I'+quote(inc)+' /Fo'+quote(path.join(dir,'fixture.obj'))+' /Fe'+quote(path.join(dir,'fixture.exe'))+'\r\nexit /b %errorlevel%\r\n');command('build','cmd.exe',['/d','/c',compile],dir,0);
 const old=!slice.hasOutcome,result=JSON.parse(command('execute',path.join(dir,'fixture.exe'),[],dir,old?1:0));
 const failed=result.cases.flatMap(c=>c.checks.filter(k=>!k.passed));assert(old?failed.length>0:failed.length===0,'Expected observation proof');assert(!old||failed.every(k=>k.observation),'Legacy behavior changed in baseline');save(label+'.results.json',result);executed[label]=result;
 results[label]={cases:result.caseCount,checks:result.checks,failedCases:result.failedCases,failedChecks:result.failedChecks,expectedMissingObservation:old};
}
const equivalence=[];
if(sources.baseline)for(const diagnostic of ['off','on']){const current=executed['current-diagnostic-'+diagnostic],baseline=executed['baseline-diagnostic-'+diagnostic];assert.equal(current.cases.length,baseline.cases.length);for(let i=0;i<current.cases.length;i++){assert.equal(current.cases[i].name,baseline.cases[i].name);assert.deepEqual(current.cases[i].legacyBehavior,baseline.cases[i].legacyBehavior,'Behavior changed: '+current.cases[i].name);equivalence.push({diagnostic,case:current.cases[i].name,legacyBehaviorEqual:true});}}
assert.deepEqual(executed['current-diagnostic-off'],executed['current-diagnostic-on'],'Diagnostic modes differ');
const sourceStability={};for(const [name,source]of Object.entries(sources)){const current=load(source.file),again=extract(current);assert(slices[name].ranges.every(r=>again.ranges.some(a=>r.name===a.name&&r.sha256===a.sha256)),'Tested source scope changed');sourceStability[name]={wholeFileChanged:current.sha256!==source.sha256,relatedRangesStable:true};}
assert.equal(load(fixture.file).sha256,fixture.sha256,'Fixture drift');
const summary={at:new Date().toISOString(),passed:true,run,profiles:results,equivalence,sourceStability,diagnosticResultsIdentical:true,productionModifiedByHarness:false,productionRewrittenByHarness:false,deviceIOExecuted:false,claim:'Observation semantics and unchanged bool/rollback/budget/readback within actual helper/case4; not Win5 slot0 functional recovery or full scheduling/Host acceptance.'};save('summary.json',summary);fs.writeFileSync(path.join(options.out,'summary.json'),JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify({passed:true,run,profiles:results,legacyComparisons:equivalence.length,sourceStability},null,2));
