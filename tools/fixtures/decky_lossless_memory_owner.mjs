// Exact original quickapp owner; file/process/shell boundaries are strictly in memory.
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {root} from './decky_task_paths.mjs';
const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
function load(text,modules={}){const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,console,Uint8Array,btoa:value=>Buffer.from(value,'binary').toString('base64'),require:name=>{assert.ok(Object.hasOwn(modules,name),'Unmocked original LS dependency '+name);return modules[name];}});return module.exports;}
export function createOriginalLosslessMemoryOwner({withProfile=true}={}){
 const profiles=load(fs.readFileSync(path.join(root,'src/bridge/losslessProfiles.ts'),'utf8'));
 const file='Q:\\LocalData\\Lossless Scaling\\Settings.xml',exe='C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe';
 const xml='<Settings><Unknown>retained</Unknown><GameProfiles><Profile><Title>Game</Title><Path>C:\\Games\\Game.exe</Path><AutoScale>true</AutoScale><ScalingType>FSR</ScalingType><FrameGeneration>LSFG3</FrameGeneration></Profile></GameProfiles></Settings>';
 const initialXml=withProfile?xml:"<Settings>\n  <Unknown>retained</Unknown>\n  <GameProfiles>\n  </GameProfiles>\n</Settings>";let memoryWrites=0;
 const files=new Map([[file,initialXml],[exe,'inert-asset']]),events=[];let running=false,title='Game',button='缩放',launchDenied=false;
 const api={fs:{exists:async p=>files.has(p),readTextFile:async p=>{assert.ok(files.has(p),'Undeclared memory LS file');return files.get(p);},writeTextFileAtomic:async(p,text)=>{assert.ok(p===file||p===file+'.ymccbak');if(withProfile)throw Error('Existing-profile test must not modify profiles');if(p===file)memoryWrites++;files.set(p,text);}},registry:{read:async()=>null},proc:{findExact:async p=>({found:p===exe&&running,pid:running?71:0}),identity:async pid=>({valid:running&&pid===71,pid,processCreated:'123',path:exe})},shell:{execute:async(p,args)=>{assert.equal(p,'cmd');assert.equal(args[0],'/c');assert.equal(args[1],'start');assert.ok(args.includes(exe));assert.ok(args.includes('-path')&&args.includes('-auto'));assert.ok(args.some(v=>/game\.exe$/i.test(v)));if(launchDenied){events.push('original-launch-refused');return false;}events.push('original-launch');const selected=profiles.matchLsProfile(profiles.parseLsProfiles(files.get(file)),'C:\\Games\\Game.exe');if(selected?.title)title=selected.title;running=true;button='取消缩放';return true;},run:async(p,args)=>{
  assert.equal(p,'powershell');if(args.includes('Write-Output $env:LOCALAPPDATA'))return {exitCode:0,stdout:'Q:\\LocalData',stderr:''};
  const source=args.includes('-EncodedCommand')?Buffer.from(args.at(-1),'base64').toString('utf16le'):args.join(' ');
  if(source.includes('CloseMainWindow')){assert.match(source,/StartTime.*ToFileTimeUtc/);events.push('original-normal-close');running=false;button='缩放';return {exitCode:0,stdout:'',stderr:''};}
  if(source.includes('原开启动作不重发')){events.push('bounded-start-observation');return {exitCode:0,stdout:'',stderr:''};}
  if(source.includes('UIAutomationClient')){assert.ok(!source.includes('.Invoke('));events.push('readonly-status');return {exitCode:0,stdout:JSON.stringify({title,button}),stderr:''};}
  throw Error('Undeclared actual LS shell operation');
 }}};
 const owner=load(fs.readFileSync(path.join(root,'src/bridge/quickapp.ts'),'utf8'),{'./api':api,'./losslessProfiles':profiles,'./gamedetect':{}});
 return {owner,setLaunchDenied:value=>{assert.equal(typeof value,'boolean');launchDenied=value;},stats:()=>({running,events:[...events],profileXmlUnchanged:files.get(file)===initialXml,memoryProfileWrites:memoryWrites,profilePresent:!!profiles.matchLsProfile(profiles.parseLsProfiles(files.get(file)),'C:\\Games\\Game.exe'),realFilesWritten:0,realProcessesStarted:0,physicalHardwareWrites:0})};
}
