/** Settings-only preparation for the existing disk helper. No test registry or runner. */
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {prepareDiskSettings,nativeInputs} from './diskSettings.mjs';
import {repo} from './harness.mjs';

const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const pin=file=>({path:file,bytes:fs.statSync(file).size,sha256:digest(fs.readFileSync(file))});
export function prepareLockedSettings(out,options={}){
 assert(!options.root||path.resolve(options.root)===repo,'Native source root must match the actual disk helper source');
 const storageRoot=path.resolve(options.storageRoot??out);
 const namespace='Local\\YMCC.OwnedSettings.'+digest(Buffer.from(storageRoot)).slice(0,24);
 const before=[...nativeInputs.map(name=>path.join(repo,name)),...(options.mainSource?[path.resolve(options.mainSource)]:[])].map(pin);
 const native=prepareDiskSettings(out,{storageRoot,mainSource:options.mainSource,settingsPreparation:({slice,groups})=>{
  const originalGuard=slice('class SettingsFileGuard {','// Defined beside the effective input consumer.');
  const originalWriter=slice('static bool ymSettingsWriteDocument(','// Explicit-read metadata validation;');
  assert.equal(originalWriter.split('SettingsFileGuard guard;').length,2,'Original public writer must still own its settings lock');
  groups.push(originalWriter);
  const guard='static std::timed_mutex g_settingsFileMtx;\n'+
   'static HANDLE WINAPI fixtureMutex(LPSECURITY_ATTRIBUTES security,BOOL owned,LPCWSTR original){\n'+
   ' if(!original||std::wstring(original)!=L"Local\\\\YeManCC-UnifiedSettings")throw std::runtime_error("unexpected production mutex");\n'+
   ' return ::CreateMutexW(security,owned,L'+JSON.stringify(namespace)+');\n}\n'+
   '#define CreateMutexW fixtureMutex\n'+originalGuard+'\n#undef CreateMutexW\n';
  fs.writeFileSync(path.join(out,'LOCKED-SOURCE.json'),JSON.stringify({namespace,
   originalGuardSha256:digest(Buffer.from(originalGuard)),originalWriterSha256:digest(Buffer.from(originalWriter)),
   adaptation:'Verbatim public writer/SettingsFileGuard; only named mutex mapped to owned namespace; existing InputHost terminal remains isolated.'},null,2)+'\n',{flag:'wx'});
  return {guard,ioPorts:'',touchHeaders:'',touchHandler:'\n#define ymSettingsWriteDocumentUnlocked ymSettingsWriteDocument\n',touchDispatch:'',includeRoot:repo,extraInputs:[]};
 }});
 return {...native,sourcePins:before,sourceIntake:()=>before.map(row=>({...row,current:pin(row.path),unchanged:pin(row.path).sha256===row.sha256}))};
}
