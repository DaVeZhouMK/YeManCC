/** Actual file backend for the original baseline consumer; no assertion copies. */
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createSettingsFixture} from './settings_baseline.mjs';
import {prepareLockedSettings} from '../boot_modes_fixture/lockedSettings.mjs';

const hash=raw=>createHash('sha256').update(raw).digest('hex');
export function prepareBaselineNative(root,out){
 const directory=path.resolve(out);fs.mkdirSync(directory);const fixtures=[];
 const write=(name,value)=>fs.writeFileSync(path.join(directory,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 const native=prepareLockedSettings(path.join(directory,'native'),{root,storageRoot:directory});
 let closed=false,closing;
 return {
  evidence:{...native.evidence,sourcePins:native.sourcePins,nativeMode:'canonical-direct-CLI',installedConfigurationIO:false},
  createFixture:(code,initial,backup)=>{
   assert(!closed,'Closed native baseline fixture cannot create another settings owner');
   const location=path.join(directory,'cases',String(fixtures.length+1).padStart(3,'0'),'PowerControl');fs.mkdirSync(location,{recursive:true});const trace=[];
   const storage={directory:location,
    seed:(file,raw)=>fs.writeFileSync(native.safe(file),raw),
    snapshot:()=>fs.readdirSync(location).map(name=>{const file=path.join(location,name);return [file,fs.readFileSync(native.safe(file),'utf8')];}),
    read:async args=>{const result=await native.invoke(args.path,{operation:'read',stamp:args.stamp||''});trace.push({operation:'read',args,result});return result;},
    write:async args=>{const {path:file,...request}=args;const result=await native.invoke(file,{operation:'write',...request});trace.push({operation:'write',args,result});return result;}};
   const fixture=createSettingsFixture(code,initial,backup,{storage});fixtures.push({location,trace,fixture});return fixture;
  },
  recordResult:result=>write('NATIVE-BASELINE-RESULT.json',{...result,phase:'ORIGINAL_ASSERTIONS_ONLY_BEFORE_NATIVE_CLOSE'}),
  close:async()=>{
   if(closed)return closing;closed=true;await native.close();
   const process=JSON.parse(fs.readFileSync(path.join(directory,'native/native-request-count.json'),'utf8'));
   assert.equal(process.pending,0);assert.equal(process.exitCode,0);assert.equal(process.signalCode,null);
   for(let i=0;i<fixtures.length;i++){
    const {fixture:f,location,trace}=fixtures[i];
    write('CASE-'+String(i+1).padStart(3,'0')+'.json',{location,trace,writeCalls:f.state.writes,recovery:f.repo.getSettingsRecoveryState(),
     files:[...f.state.files].map(([file,raw])=>({path:file,bytes:Buffer.byteLength(raw),sha256:hash(raw)}))});
   }
   const inputs=native.sourceIntake();
   closing={nativeProcess:process,inputs,sourceUnchanged:inputs.every(row=>row.unchanged),realHardwareOperations:0,installedConfigurationIO:false};
   write('SOURCE-CLOSING.json',closing);return closing;
  }
 };
}
