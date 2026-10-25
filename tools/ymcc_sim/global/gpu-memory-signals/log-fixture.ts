import fs from 'node:fs';
const [inputFile,resultFile]=process.argv.slice(2);
if(!inputFile||!resultFile)throw Error('OWNED_INPUT_AND_RESULT_REQUIRED');
const input=JSON.parse(fs.readFileSync(inputFile,'utf8'));
const attempts={externalNetwork:0,userStorage:0,writes:0};let listener;let response;const requests=[];
globalThis.fetch=()=>{attempts.externalNetwork++;throw Error('EXTERNAL_NETWORK_DENIED');};
const storage={getItem(){attempts.userStorage++;throw Error('USER_STORAGE_DENIED');},setItem(){attempts.userStorage++;throw Error('USER_STORAGE_DENIED');}};
(globalThis as any).localStorage=storage;(globalThis as any).sessionStorage=storage;
(globalThis as any).window={chrome:{webview:{addEventListener(_name:string,fn:any){listener=fn;},postMessage(message:any){
 if(message.cmd!=='gpu.dedicatedMemory.get'||Object.keys(message.args??{}).length){attempts.writes++;throw Error('ONLY_MEMORY_READ_ALLOWED');}
 requests.push({cmd:message.cmd,args:message.args});queueMicrotask(()=>listener({data:{id:message.id,result:response}}));
}}}};
const product=await import('../../../../src/bridge/gpuDedicatedMemory');
const rows=[];
for(const event of input.events){
 const fields=['ok','available','provider','verification','capacityRead','dedicatedBytes','selected','pendingOption','options'];
 const observed=Object.fromEntries(fields.filter(k=>Object.hasOwn(event,k)).map(k=>[k,event[k]]));
 // Existing capability diagnostics deliberately omit response binding/fence.
 // These explicit inert labels only admit pure bridge presentation logic.
 // They never name an actual adapter or authorize a save.
 const supplemented={schema:1,settingRoute:'known-model',reason:'',adapterName:'SIMULATED-LOG-ADAPTER',driverVersion:'SIMULATED-LOG-DRIVER',pnpId:'SIMULATED-LOG-PNP',luid:'SIMULATED-LOG-LUID',fence:'SIMULATED-LOG-NONWRITABLE-FENCE'};
 response={...supplemented,...observed};const read=await product.gpuDedicatedMemory.read();const parsed=product.parseDedicatedMemorySnapshot(read);
 const missing=['ok','available','provider','verification','capacityRead','dedicatedBytes','selected','options'].filter(k=>!Object.hasOwn(event,k));
 const expectedParse=event.ok===true&&event.available===true&&['rog-apu','rog-vram'].includes(event.provider)&&event.capacityRead===true&&Number.isSafeInteger(event.dedicatedBytes)&&event.dedicatedBytes>0&&Array.isArray(event.options)&&event.options.length>=2&&event.options.some(o=>o.id===event.selected)&&!missing.length;
 if(Boolean(parsed)!==expectedParse)throw Error('PRODUCT_LOG_RESPONSE_ADMISSION_DIFFERS');
 const state=parsed?{parsed:true,capacity:product.formatDedicatedCapacity(parsed.dedicatedBytes),status:product.dedicatedMemoryStatus(parsed),configuredCapacityMismatch:product.dedicatedConfiguredCapacityMismatch(parsed)}:{parsed:false,capacity:null,status:null,configuredCapacityMismatch:null};
 if(parsed){const labels={pending_restart:'请重启',verified:'已生效',not_effective:'未生效',driver_changed:'未确认',unconfirmed:'未确认'};const expected=labels[parsed.verification]??(state.configuredCapacityMismatch?'未确认':'');if(state.status!==expected)throw Error('PRODUCT_LOG_STATUS_DIFFERS');}
 rows.push({eventId:event.id,status:'PASS',input:{event:'machine-capability',capability:'gpu.dedicatedMemory',observed},consumer:{modules:['src/bridge/gpuDedicatedMemory.ts','src/bridge/ipc.ts'],operation:'gpuDedicatedMemory.read → parseDedicatedMemorySnapshot → presentation',memoryReadOnly:true},state,feedback:{missingObservedFields:missing,supplementedFields:Object.keys(supplemented),supplementationProvenance:'source-derived-inert-binding-fixture',bindingVerified:false,saveInvoked:false,physicalObservation:false}});
}
if(attempts.externalNetwork||attempts.userStorage||attempts.writes||requests.length!==rows.length)throw Error('READ_ONLY_BOUNDARY_VIOLATED');
fs.writeFileSync(resultFile,JSON.stringify({status:'PASS',rows,requests,attempts,executedProductModules:['src/bridge/gpuDedicatedMemory.ts','src/bridge/ipc.ts']},null,2));
