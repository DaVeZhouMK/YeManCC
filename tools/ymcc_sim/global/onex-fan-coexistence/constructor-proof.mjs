import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {stageFaultPayload} from './fault-payload-staging.mjs';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tree=root=>fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>e.isDirectory()?tree(path.join(root,e.name)):[path.join(root,e.name)]);
const sorted=list=>JSON.stringify([...list].sort());
function nativeInterface(file,inventory){
 const bytes=fs.readFileSync(file),pe=bytes.readUInt32LE(0x3c);if(bytes.toString('ascii',pe,pe+4)!=='PE\0\0'||bytes.readUInt16LE(pe+4)!==0x8664)throw Error('Pinned motion library must be an x64 PE');
 const optional=pe+24,sections=bytes.readUInt16LE(pe+6),size=bytes.readUInt16LE(pe+20);if(bytes.readUInt16LE(optional)!==0x20b)throw Error('PE32+ required');
 const spans=Array.from({length:sections},(_,i)=>{const at=optional+size+i*40;return{size:Math.max(bytes.readUInt32LE(at+8),bytes.readUInt32LE(at+16)),rva:bytes.readUInt32LE(at+12),raw:bytes.readUInt32LE(at+20)};});
 const offset=rva=>{const span=spans.find(s=>rva>=s.rva&&rva<s.rva+s.size);if(!span)throw Error('PE RVA outside a section');return span.raw+rva-span.rva;};
 const string=rva=>{const at=offset(rva),end=bytes.indexOf(0,at);if(end<0||end-at>4096)throw Error('Invalid PE string');return bytes.toString('ascii',at,end);};
 const rva=bytes.readUInt32LE(optional+112),length=bytes.readUInt32LE(optional+116),at=offset(rva),count=bytes.readUInt32LE(at+24),functions=offset(bytes.readUInt32LE(at+28)),names=offset(bytes.readUInt32LE(at+32)),ordinals=offset(bytes.readUInt32LE(at+36));
 const exports=Array.from({length:count},(_,i)=>{const name=string(bytes.readUInt32LE(names+i*4)),entry=bytes.readUInt32LE(functions+bytes.readUInt16LE(ordinals+i*2)*4);if(entry>=rva&&entry<rva+length)throw Error('Native math exports cannot forward outside the pinned file');return name;});
 const declared=inventory.nativeGamepadMotionDeclarations.map(m=>m.entryPoint);if(declared.length!==21||declared.some(n=>!exports.includes(n)))throw Error('Original managed motion ABI is missing a native export');
 // The original library is a software math dependency. Refuse any newly
 // introduced native module in its import table before executing it.
 const imports=[];const importRva=bytes.readUInt32LE(optional+120);if(importRva)for(let p=offset(importRva);bytes.readUInt32LE(p+12);p+=20){imports.push(string(bytes.readUInt32LE(p+12)));}
 const allowed=['VCRUNTIME140.dll','api-ms-win-crt-math-l1-1-0.dll','api-ms-win-crt-heap-l1-1-0.dll','api-ms-win-crt-runtime-l1-1-0.dll','KERNEL32.dll'];
 if(sorted(imports.map(n=>n.toLowerCase()))!==sorted(allowed.map(n=>n.toLowerCase())))throw Error('Pinned native math import set changed');
 return{status:'PASS',nativeSha256:hash(file),machine:'x64',declaredCount:declared.length,exports,imports,forwardedExports:false,declaredMethods:inventory.nativeGamepadMotionDeclarations,hardwareIo:false};
}
export function runConstructorProof({repo,out,harness,candidate,hc,run}){
 const root=path.join(out,'original-constructors');fs.mkdirSync(root);const relative=file=>path.relative(out,file);
 const runtime=path.resolve(path.dirname(hc)),shared=path.join(repo,'PowerControl/ymcc-shared-runtime'),catalog=path.join(repo,'capabilities/hc-device-catalog.json');
 const inputs=[];const pins={};
 for(const [base,name]of [[runtime,'HandheldCompanion.runtime.json'],[shared,'manifest.json']]){
  const manifest=path.join(base,name);pins[manifest]=hash(manifest);inputs.push({path:manifest,sha256:pins[manifest]});
  for(const row of read(manifest).files){const file=path.resolve(base,row.packagedPath||row.path);if(!file.startsWith(base+path.sep)||hash(file)!==row.sha256)throw Error('Constructor runtime manifest member differs: '+file);pins[file]=row.sha256;inputs.push({path:file,sha256:row.sha256});}
 }
 const inventoryPath=path.join(root,'inventory.json');run('dotnet',[harness,'constructor-inventory',hc,inventoryPath],relative(path.join(root,'inventory.log')),30000);const inventory=read(inventoryPath);
 if(inventory.hcSha256!==hash(hc)||inventory.factoryConstructors!==80||inventory.rows.filter(r=>r.inactive).length!==1||inventory.rows.filter(r=>!r.inactive).length!==79)throw Error('Original constructor selection changed');
 const native=nativeInterface(path.join(runtime,'GamepadMotion.dll'),inventory);fs.writeFileSync(path.join(root,'native-interface.json'),JSON.stringify(native,null,2)+'\n');
 const fixture=path.join(root,'leaf-hc/HandheldCompanion.dll'),ports=path.join(root,'ports-proof.json');
 run('dotnet',[harness,'constructor-leaf-ports',hc,fixture,ports,shared],relative(path.join(root,'ports.log')),30000);const portsProof=read(ports);
 if(portsProof.status!=='PASS'||portsProof.sourceSha256!==hash(hc)||portsProof.instrumentedSha256!==hash(fixture)||portsProof.changedMethods.length!==7||!portsProof.allDeviceInstanceConstructorBodiesUnchanged||!portsProof.gamepadMotionManagedAndNativeAbiUnchanged)throw Error('Original constructor IO changes escaped their exact leaves');
 const executions=[];let names;
 for(const mode of ['positive','capabilities','hotkeys','dispose','native']){
  const dir=path.join(root,mode);fs.mkdirSync(dir);let image=fixture,host=candidate,mutation=null;
  if(mode!=='positive'){
   const faultRoot=mode==='native'?path.join(dir,'host-payload'):dir;if(mode==='native')stageFaultPayload(path.dirname(candidate),faultRoot);
   const fault=path.join(faultRoot,mode==='native'?'YeManFanHost.dll':'HandheldCompanion.dll'),proof=path.join(dir,'fault-proof.json'),source=mode==='native'?candidate:fixture;
   run('dotnet',[harness,'fault-original-constructor-'+mode,source,fault,proof,runtime,shared],relative(path.join(dir,'fault-generation.log')),30000);mutation=read(proof);
   if(mutation.status!=='PASS'||mutation.sourceSha256!==hash(source)||mutation.faultSha256!==hash(fault)||mutation.changedMethods.length!==1)throw Error('Original constructor fault was not exactly one method');
   if(mode==='native')host=fault;else image=fault;
  }
  const currentPins={...pins,[image]:hash(image)},pinFile=path.join(dir,'runtime-pins.json');fs.writeFileSync(pinFile,JSON.stringify(currentPins,null,2)+'\n');
  const resultPath=path.join(dir,'result.json');run('dotnet',[harness,'original-constructors',image,runtime,shared,inventoryPath,pinFile,resultPath,'all',catalog,host],relative(path.join(dir,'run.log')),60000,mode==='positive'?0:2);
  const result=read(resultPath),failed=result.cases.filter(c=>c.status!=='PASS');
  if(result.cases.length!==89||result.rejectedNativeRequests!==0||result.hcSha256!==hash(image)||result.candidateHostSha256!==hash(host)||result.originalDeviceConstructorBodiesExecuted!==true||result.actualHostNativeAdmissionExecuted!==true||result.nativeMathCodeExecuted!==true||result.managerStartCalled!==false||result.actualDeviceOpenBodyExecuted!==false||result.hostMainStarted!==false||result.hardwareIo!==false)throw Error('Original constructor/native execution scope or case set differs');
  if(mode==='positive'){if(result.status!=='PASS'||failed.length||result.nativeMathFaultCleanup!==0)throw Error('Original constructor positive consumers failed');names=result.cases.map(r=>r.id);}
  else{
   const expected=mode==='native'?['native-admission:missing','native-admission:invalid']:mode==='capabilities'?['ROGAlly','ROGAllyX','XboxROGAlly','XboxROGAllyX'].map(n=>'original-constructor:HandheldCompanion.Devices.'+n):names.filter(n=>n.startsWith('original-constructor:'));
   if(result.status!=='FAIL'||sorted(failed.map(r=>r.id))!==sorted(expected))throw Error('Original constructor fault did not fail its exact consumer set: '+mode);
   const fragment=mode==='capabilities'?'capability bits must satisfy':mode==='dispose'?'Dispose releases its owner handle':mode==='native'?'Expected GamepadMotion.dll':'four reserved default hotkey flags';
   if(failed.some(r=>!r.error.includes(fragment)))throw Error('Fault was not detected at its intended business assertion: '+mode);
   if(mode==='dispose'&&result.nativeMathFaultCleanup!==86)throw Error('Every deliberately retained native math owner must be explicitly cleaned up');
  }
  if(sorted(result.cases.map(r=>r.id))!==sorted(names))throw Error('Original constructor private-fault case set differs');
  executions.push({mode,cases:result.cases.length,checks:result.checks,resultPath,hostSha256:hash(host),hcSha256:hash(image),failedIds:failed.map(r=>r.id),nativeMathFaultCleanup:result.nativeMathFaultCleanup,mutation});
 }
 for(const row of inputs)if(hash(row.path)!==row.sha256)throw Error('Original constructor runtime drifted: '+row.path);
 const sourceHashes={'capabilities/hc-device-catalog.json':hash(catalog)};
 const proof={status:'PASS',originalHcSha256:hash(hc),candidateHostSha256:hash(candidate),portsProof,positiveCases:89,originalConstructorsExecuted:79,constructorExecutions:86,inactiveConstructorsExecuted:0,originalNativeAbiDeclarations:21,positiveChecks:executions[0].checks,privateFaults:executions.slice(1).map(r=>({mode:r.mode,count:r.failedIds.length,ids:r.failedIds})),executions,allOriginalDeviceInstanceConstructorBodiesUnchanged:true,originalManagedMotionConstructorAndDisposeExecuted:true,pinnedNativeMathExecuted:true,actualHostNativeDependencyAdmissionExecuted:true,completeHardwareNativeBootstrapProven:false,hardwareIo:false,managerStartCalled:false,hostMainStarted:false,scope:'Original79 admitted constructor bodies and real pure-native motion allocation/disposal, current catalog bounds and actual Host NativeLibrary admission; seven explicit metadata/composition/hardware IO leaves. No manager Start/device Open, inactive SuiPlay or real hardware.'};
 fs.writeFileSync(path.join(root,'proof.json'),JSON.stringify(proof,null,2)+'\n');return{proof,sourceHashes,inputs,evidence:tree(root).map(file=>({path:file,sha256:hash(file)}))};
}
