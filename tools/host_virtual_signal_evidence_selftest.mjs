// Actual-source SDK boundary fixture. No Host process or SDK controller/context is started.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2),opts={};
for(let i=0;i<args.length;i+=2){if(!['--evidence','--baseline','--sd-baseline'].includes(args[i])||!args[i+1])throw Error('Use --evidence DIR --baseline Program.before.cs --sd-baseline ScreenTouchpadReport.before-sd.cs');opts[args[i]]=path.resolve(args[i+1]);}
const out=opts['--evidence']; if(!out||!opts['--baseline']||!opts['--sd-baseline'])throw Error('Evidence directory and before-sources required');
fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const names=['Program.cs','HostVirtualSignalEvidence.cs','MsiVirtualReportChain1410.cs','ScreenTouchpadReport.cs','MsiHostStageEvidence.cs','SteamDeckStateFields.cs','SteamDeckPadSubmissionEvidence.cs'];
const snapshots=Object.fromEntries(names.map(n=>[n,fs.readFileSync(path.join(repo,'InputHost',n))]));
const before=fs.readFileSync(opts['--baseline'],'utf8'), sdBefore=fs.readFileSync(opts['--sd-baseline'],'utf8');
const source=snapshots['Program.cs'].toString('utf8'), pads=snapshots['ScreenTouchpadReport.cs'].toString('utf8');
fs.mkdirSync(path.join(out,'snapshot'),{recursive:true});
for(const [n,b]of Object.entries(snapshots))fs.writeFileSync(path.join(out,'snapshot',n),b);
fs.writeFileSync(path.join(out,'snapshot','Program.before.cs'),before);
fs.writeFileSync(path.join(out,'snapshot','ScreenTouchpadReport.before-sd.cs'),sdBefore);
function mask(text){return text.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>m.replace(/[^\n\r]/g,' '));}
const members=[];
function extract(text,name,kind='method'){
 const re=new RegExp('^    +(?:private|internal) (?:static |sealed |readonly )*[^\\r\\n]*\\b'+name+(kind==='record'?'\\(':kind==='field'?'\\s*(?:=|;)':'\\('),'gm');
 const hits=[...text.matchAll(re)];if(hits.length!==1)throw Error('Expected unique member '+name+' got '+hits.length);
 const start=hits[0].index, clean=mask(text.slice(start));let end;
 if(kind==='field'){end=clean.indexOf(';')+1;}
 else{let paren=0,k=clean.indexOf('(');for(;k<clean.length;k++){if(clean[k]==='(')paren++;if(clean[k]===')'&&--paren===0){k++;break;}}
 while(/\s/.test(clean[k]))k++;
 if(clean.slice(k,k+2)==='=>'){end=clean.indexOf(';',k)+1;}
 else{if(clean[k]!=='{')throw Error('No body '+name);let depth=0;for(;k<clean.length;k++){if(clean[k]==='{')depth++;if(clean[k]==='}'&&--depth===0){end=k+1;break;}}}}
 if(!end)throw Error('Unclosed '+name);const code=text.slice(start,start+end);members.push({name,sha256:sha(code),line:text.slice(0,start).split('\n').length});return code;
}
const common=['TryGetVirtualOutputProfile','BuildEliteState','BuildDualSenseDataOnlyReport','QuantizeMotionAxis','WriteSonyImuLe16','AxisToByte','TriggerToByte','MapHatOctant','MapFaceButtons','MapDigitalButtons','MapDsEdgePaddles','UnitStickAxis','BuildSteamDeckStateFields'].map(n=>extract(source,n));
common.push(...['WriteDualSenseScreenPads','WriteDualSenseContact','WriteSonyContact','TakeScreenPadReportOwnership','BuildSteamDeckScreenPadReport','SubmitSteamDeckWithScreenPads'].map(n=>extract(pads,n)));
common.push(...['ScreenPadReportBuffer','_screenPadPacketCounter','_screenPadOwnsReport','ScreenPadSubmissionEvidence','ScreenPadLastEncoded','DeckButtonBits'].map(n=>extract(pads,n,'field')));
// A/B uses the original SD boundary verbatim except for its symbol name. Encoders/ownership are shared unchanged.
for(const n of ['TakeScreenPadReportOwnership','BuildSteamDeckScreenPadReport'])if(extract(pads,n)!==extract(sdBefore,n))throw Error('SD ownership/encoder unexpectedly edited: '+n);
common.push(extract(sdBefore,'SubmitSteamDeckWithScreenPads').replace('SubmitSteamDeckWithScreenPads(', 'SubmitSteamDeckWithScreenPadsBefore('));
const record=extract(source,'CommandEnvelope','record');
const currentNames=['ResetVirtualSignalEvidenceSafely','BeginVirtualSignalEvidenceSafely','CompleteVirtualSignalEvidenceSafely','PrepareReportChainInvocation1410','CloseReportChain1410Safely','SubmitSonyReportChain1410','SubmitEliteReportChain1410','TrySubmitFrame','TrySubmitNeutral'];
const current=currentNames.map(n=>extract(source,n)).join('\n');
const baseline=['PrepareReportChainInvocation1410','SubmitSonyReportChain1410','SubmitEliteReportChain1410','TrySubmitFrame','TrySubmitNeutral'].map(n=>extract(before,n)).join('\n').replaceAll('SubmitSteamDeckWithScreenPads(', 'SubmitSteamDeckWithScreenPadsBefore(');
// Also assert the actual dynamic flag/lifecycle wiring, not a test-only reconstruction.
for(const needle of ['if (!_costFlagEnabled) ResetVirtualSignalEvidenceSafely();','ResetVirtualSignalEvidenceSafely();\n                _tuple = envelope;','_controller = null;\n                ResetVirtualSignalEvidenceSafely();'])if(!source.replaceAll('\r\n','\n').includes(needle))throw Error('Missing lifecycle wire '+needle);
if(source.split('if (!_costFlagEnabled) ResetVirtualSignalEvidenceSafely();').length!==3)throw Error('Both cost flag update routes must invalidate');
const templatePath=path.join(repo,'tools','host_virtual_signal_evidence_selftest','host_virtual_signal_evidence_selftest.cs');
const template=fs.readFileSync(templatePath,'utf8');
const generated=template.replace('/*ACTUAL_COMMON*/',record+'\n'+common.join('\n')).replace('/*ACTUAL_CURRENT*/',current).replace('/*ACTUAL_BASELINE*/',baseline);
fs.writeFileSync(path.join(out,'Fixture.cs'),generated);
const esc=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;');
const dll=path.join(repo,'deps','hidmaestro','HIDMaestro.Core.dll');
fs.writeFileSync(path.join(out,'Fixture.csproj'),`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><PlatformTarget>x64</PlatformTarget><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><UseSharedCompilation>false</UseSharedCompilation></PropertyGroup><ItemGroup><Compile Include="Fixture.cs"/>${['HostVirtualSignalEvidence.cs','MsiVirtualReportChain1410.cs','MsiHostStageEvidence.cs','SteamDeckStateFields.cs','SteamDeckPadSubmissionEvidence.cs'].map(n=>`<Compile Include="snapshot/${n}"/>`).join('')}<Reference Include="HIDMaestro.Core"><HintPath>${esc(dll)}</HintPath></Reference></ItemGroup></Project>`);
fs.writeFileSync(path.join(out,'NuGet.Config'),'<configuration><packageSources><clear/></packageSources></configuration>');
const manifest={members,sourceFiles:Object.fromEntries(Object.entries(snapshots).map(([n,b])=>[n,sha(b)])),baselineSha256:sha(before),sdBaselineSha256:sha(sdBefore),templateSha256:sha(template),runnerSha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),sdkDataAssemblySha256:sha(fs.readFileSync(dll)),scope:'Exact current and before Sony/Elite wrappers plus SD partial raw boundary, builders, ownership and live/neutral paths; actual helper/pad evidence and real SDK state types. Fake HMController/HMContext/HMProfile and idle-clock stop boundary only; no SDK controller/context is constructed. Baseline SD declaration/calls renamed solely to compile A/B together; SD ownership/encoder are asserted byte-identical.'};
fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
function run(label,args){const p=spawnSync('dotnet',args,{cwd:out,encoding:'utf8',windowsHide:true,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,label+'.log'),(p.stdout??'')+(p.stderr??''));if(p.status!==0){console.error((p.stdout??'')+(p.stderr??''));throw Error(label+' failed '+p.status);}}
run('build',['build','Fixture.csproj','-c','Release','--nologo']);
run('selftest',[path.join(out,'bin','Release','net10.0-windows','Fixture.dll'),out]);
for(const[n,b]of Object.entries(snapshots))if(sha(fs.readFileSync(path.join(repo,'InputHost',n)))!==sha(b))throw Error('Source changed during test: '+n);
const result=JSON.parse(fs.readFileSync(path.join(out,'result.json'),'utf8'));
console.log(JSON.stringify(result,null,2));
