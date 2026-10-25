// Actual Host motor decoder/lifecycle/serializer, inert SDK event/PnP boundaries.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
const args=process.argv.slice(2),option=(name,fallback)=>{const i=args.indexOf(name);return i<0?fallback:args[i+1];};
const repo=path.resolve(option('--repo',path.join(path.dirname(fileURLToPath(import.meta.url)),'..')));
const source=path.resolve(option('--source-root',path.join(repo,'InputHost')));
const catalog=path.resolve(option('--game-catalog',path.join(repo,'capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs')));
const generator=path.resolve(option('--generator',path.join(repo,'tools/msi_steamdeck_release_fence_1726_selftest.mjs')));
const observationProducer=path.resolve(option('--observation-producer',path.join(repo,'tools/inputhost_game_output_observation_selftest.mjs')));
const testSource=path.resolve(option('--test-source',path.join(path.dirname(fileURLToPath(import.meta.url)),'inputhost_game_motor_receipt_selftest.cs')));
const out=path.resolve(option('--out','./host-game-motor-receipt'));
const configs=option('--configs','Release,Debug').split(',');
if(configs.some(c=>!['Release','Debug'].includes(c)))throw Error('Unsupported configuration');
if(fs.existsSync(out))throw Error('Fresh evidence required');fs.mkdirSync(out,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex');
const inputFiles=[...fs.readdirSync(source).filter(n=>n.endsWith('.cs')).map(n=>path.join(source,n)),catalog,generator,observationProducer,testSource,fileURLToPath(import.meta.url),path.join(repo,'tools/inputhost_game_output_observation_selftest.cs'),path.join(repo,'tools/msi_steamdeck_release_fence_1726_selftest.cs'),path.join(repo,'tools/inputhost_lifecycle_selftest.cs'),path.join(repo,'capabilities/CONTROLLER-GAME-OUTPUT-OBSERVATION.json'),path.join(repo,'deps/hidmaestro/HIDMaestro.Core.dll')];
const inputs=[...new Set(inputFiles)].map(file=>({file,sha256:sha(fs.readFileSync(file))}));
function run(label,exe,argv,cwd=repo){const r=spawnSync(exe,argv,{cwd,encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(out,label+'.log'),(r.stdout??'')+(r.stderr??''));if(r.error)throw r.error;return r;}
const prerequisites=run('original-observation-lifecycle',process.execPath,[observationProducer,'--repo',repo,'--generator',generator,'--game-catalog',catalog,'--source-root',source,'--configs',configs.join(','),'--out',path.join(out,'prerequisites')]);
if(prerequisites.status!==0)throw Error('Original observation/lifecycle prerequisites failed: '+out);
const generated=path.join(out,'motor-fixture');fs.mkdirSync(generated);
for(const name of fs.readdirSync(path.join(out,'prerequisites')).filter(n=>n.endsWith('.cs')||n==='Fixture.csproj'||n==='NuGet.Config'))fs.copyFileSync(path.join(out,'prerequisites',name),path.join(generated,name));
const entry=path.join(generated,'Entry.cs'),text=fs.readFileSync(entry,'utf8');
if(text.split('private static int Main(').length!==2)throw Error('Original entry anchor');
fs.writeFileSync(entry,text.replace('private static int Main(','private static int OriginalObservationEntry(')+'\ninternal static partial class Program { private static int Main(string[] args)=>MotorMain(args[0]); }\n');
fs.copyFileSync(testSource,path.join(generated,'MotorTests.cs'));
const profiles=[];
for(const config of configs){const build=run(config+'-build','C:/Program Files/dotnet/dotnet.exe',['build','Fixture.csproj','-c',config,'--nologo','-v:q'],generated);if(build.status!==0)throw Error('Motor fixture build failed: '+out);
const wire=path.join(out,'wire-'+config),execution=run(config+'-tests','C:/Program Files/dotnet/dotnet.exe',[path.join(generated,'bin',config,'net10.0/Fixture.dll'),wire],generated);let result;try{result=JSON.parse(execution.stdout.trim().split(/\r?\n/).at(-1));}catch{}
profiles.push({config,buildExit:build.status,executionExit:execution.status,result});}
const stable=inputs.every(i=>sha(fs.readFileSync(i.file))===i.sha256);
const result={status:stable&&profiles.every(p=>p.executionExit===0&&p.result?.status==='PASS')?'PASS':'FAIL',stable,inputs,profiles,sourceRoot:source,hostStarted:false,sdkConstructed:false,physicalAcceptance:false,gameFeedbackForwardingProven:false,fullNativeRequestExecuted:false};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({status:result.status,stable,profiles:profiles.map(p=>({config:p.config,build:p.buildExit,exit:p.executionExit,cases:p.result?.cases.length,failed:p.result?.cases.filter(c=>!c.pass).map(c=>({name:c.name,error:c.error?.split('\n')[0]}))}))}));
if(result.status!=='PASS')process.exitCode=1;
