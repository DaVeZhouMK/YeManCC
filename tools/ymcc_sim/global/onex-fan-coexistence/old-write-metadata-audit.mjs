// This module reads selected raw metadata reports. It never loads target code.
const fail=(code,detail)=>{throw Object.assign(new Error(code),{code,detail});};
const need=(ok,code,detail)=>{if(!ok)fail(code,detail);};
export function auditOldWriteMetadata(reports){
  need(Array.isArray(reports)&&reports.length>0&&reports.length<=10,'OLD_WRITE_BOUNDED_REPORT_SET');
  const methods=new Map(),types=new Map(),images=new Map();
  for(const report of reports){
    need(report.status==='PASS_RAW_SELECTED_METHOD_READ'&&report.failure===null&&report.targetAssembliesObserved===false&&report.targetCodeExecuted===false&&report.targetConstructorsInvoked===false&&report.externalResolutionAllowed===false,'OLD_WRITE_RAW_REPORT_NOT_METADATA_ONLY');
    for(const image of report.rows){
      need(image.moduleInitializers.length===0,'OLD_WRITE_MODULE_INITIALIZER_PRESENT',{path:image.path});
      const existing=images.get(image.identity);need(!existing||existing.sha256===image.sha256,'OLD_WRITE_IMAGE_IDENTITY_COLLISION',{identity:image.identity});images.set(image.identity,{path:image.path,sha256:image.sha256,identity:image.identity,mvid:image.mvid,moduleInitializers:[]});
      for(const selection of image.selections){
        types.set(image.sha256+'|'+selection.type,selection);
        for(const method of selection.methods){
          const id=image.sha256+'|'+method.token,old=methods.get(id);
          need(!old||old.method.instructionListingSha256===method.instructionListingSha256,'OLD_WRITE_METHOD_BODY_CONFLICT',{method:method.method});
          methods.set(id,{image:{path:image.path,sha256:image.sha256,identity:image.identity},type:selection.type,method});
        }
      }
    }
  }
  need(methods.size<=512,'OLD_WRITE_METHOD_QUERY_BUDGET_EXCEEDED',{count:methods.size});
  const syntheticSha='6d7bff05991ee407538b4ef35e12092b21a155d15ab5ca60fe0eae21846391f6',hostSha='a6788164463da89f7e7df6a6ab1edaf7c2d5eff69c27d127b0d3ce5c47d36539';
  const chain=[],seenTypes=new Set();let current='HandheldCompanion.Devices.OneXPlayerX1AMD';
  while(current&&current!=='System.Object'&&!seenTypes.has(current)&&chain.length<8){seenTypes.add(current);chain.push(current);current=types.get(syntheticSha+'|'+current)?.baseType;}
  const expectedChain=['HandheldCompanion.Devices.OneXPlayerX1AMD','HandheldCompanion.Devices.OneXPlayerX1','HandheldCompanion.Devices.OneXAOKZOE','HandheldCompanion.Devices.IDevice'];
  const predicate=[...methods.values()].find(r=>r.image.sha256===hostSha&&r.type==='YeManFanHost.MsiFanDeviceLifecycle'&&r.method.method.includes('::IsMsiFamily('));
  const open=[...methods.values()].find(r=>r.image.sha256===hostSha&&r.method.method.includes('RealHcBackend::OpenHcDevice('));
  const fixedOneXNoMsi=JSON.stringify(chain)===JSON.stringify(expectedChain)&&current==='System.Object'&&predicate?.method.instructionListingSha256==='82e5e05be00ab70155e356b220d2f463f732b91a7e9a61506398ec3e4f50b4ac'&&open?.method.instructionListingSha256==='b8030f7b67c565fb601bdeea6f45879135db7159ae812baf1961f599be3b4866';
  const fixedBranchProof={proven:fixedOneXNoMsi,actualType:expectedChain[0],baseChain:chain,predicate:predicate?{token:predicate.method.token,bodySha256:predicate.method.instructionListingSha256,matchName:'HandheldCompanion.Devices.ClawA1M',result:false}:null,
    openBranch:open?{token:open.method.token,bodySha256:open.method.instructionListingSha256,predicateCallOffset:61,branchOffset:66,falseTargetOffset:161,excludedMsiCtorOffset:83,excludedMsiOpenOffset:111}:null,
    meaning:'The pinned predicate loops FullName/BaseType for ClawA1M; the exact synthetic chain has no match. Exact Host OpenHcDevice branch skips MSI ctor/Open. No simple-name device inference.'};
  const excludedMsiMethods=[...methods.values()].filter(r=>fixedOneXNoMsi&&r.type==='YeManFanHost.MsiFanDeviceLifecycle'&&!r.method.isStatic);
  const excludedSet=new Set(excludedMsiMethods.map(r=>r.image.sha256+'|'+r.method.token));
  const edges=new Map(),reflectionSites=[],externalLeaves=[];
  for(const row of methods.values())for(const call of row.method.calls??[]){
    if(excludedSet.has(row.image.sha256+'|'+row.method.token))continue;
    if(fixedOneXNoMsi&&call.target.includes('YeManFanHost.MsiFanDeviceLifecycle::')&&!call.target.includes('::IsMsiFamily(')&&!call.target.includes('::.cctor('))continue;
    if(call.token>>>24===6&&!methods.has(row.image.sha256+'|'+call.token)){
      const id=row.image.sha256+'|'+call.token;
      if(!edges.has(id))edges.set(id,{image:row.image,token:call.token,target:call.target,callers:[]});
      edges.get(id).callers.push({method:row.method.method,token:row.method.token,offset:call.offset});
    }
    if(/System\.(Reflection|Activator)|CreateDelegate|GetMethod|GetField|GetProperty/.test(call.target))reflectionSites.push({image:row.image,method:row.method.method,token:row.method.token,...call});
    if(/PInvoke|MemoryMapped|UnmanagedMemory|System\.IO\.File|System\.IO\.Ports|Registry|Process::|GetCurrentThreadId|Timer::|AsusACPI|WMI|ManagementObject/.test(call.target)||row.method.isPInvoke)externalLeaves.push({image:row.image,method:row.method.method,token:row.method.token,...call});
  }
  const find=(imageName,type,name)=>[...methods.values()].filter(row=>row.image.identity.split(',')[0]===imageName&&row.type===type&&row.method.method.includes('::'+name+'('));
  const binding=(image,type,name,purpose)=>{const found=find(image,type,name);return {image,type,name,purpose,reviewed:found.length>0,bodies:found.map(r=>({token:r.method.token,method:r.method.method,instructionListingSha256:r.method.instructionListingSha256,image:r.image}))};};
  const bindings=[
    ...['.ctor','BeginOpenAttemptEvidence','OpenHcDevice','WriteFanProfileDuty','ApplyPowerProfile','CaptureSessionRouteMarker','OnHcCpuTemperatureChanged','MonitorTargetForRead','DetachCpuTemperatureLoop','CloseHcDevice'].map(n=>binding('YeManFanHost','YeManFanHost.RealHcBackend',n,'OldWriteProbe Invoke/Activator fixed target')),
    ...['.ctor','Dispose','InvalidateCachedMapping','InvalidateLabelCache'].map(n=>binding('YeManFanHost','YeManFanHost.HWiNFOTemperatureMonitor',n,'Fresh monitor / actual Dispose chain')),
    ...['.ctor','Open','OpenCore','EnsureEventsReadyCore','Close','CloseCore','CleanupTransport'].map(n=>binding('YeManFanHost','YeManFanHost.OneXFanDeviceLifecycle',n,'Host fixed OneX lifecycle and cleanup')),
    ...['OpenEvents','Close','PowerProfileManager_Applied','SetFanControl','SetFanDuty','ECRamDirectWriteByte'].map(n=>binding('HandheldCompanion','HandheldCompanion.Devices.IDevice',n,'Exact synthetic declaring owner; no generic X1 Close invocation')),
    ...['.ctor','InitializeOls','WriteIoPortByte','EnterSuperIoConfig','ExitSuperIoConfig','Dispose'].map(n=>binding('HandheldCompanion','HandheldCompanion.OpenLibSys',n,'Explicit inert OLS terminal')),
    ...['SetFanControl','SetFanDuty'].map(n=>binding('HandheldCompanion','HandheldCompanion.Devices.OneXAOKZOE',n,'Inherited OneX fan dispatch')),
    ...['.ctor','SetTemperature','GetFanSpeed'].map(n=>binding('HandheldCompanion','HandheldCompanion.Misc.FanProfile',n,'Actual reflected curve leaf is the existing scripted pure fixture')),
  ];
  const missingBindings=bindings.filter(row=>!row.reviewed);
  const oneXMissing=[...edges.values()].filter(edge=>edge.callers.some(c=>c.method.includes('YeManFanHost.OneXFanDeviceLifecycle::')));
  const syntheticMissing=[...edges.values()].filter(edge=>edge.image.identity.startsWith('HandheldCompanion,'));
  const monitorCtor=find('YeManFanHost','YeManFanHost.HWiNFOTemperatureMonitor','.ctor')[0];
  const monitorDispose=find('YeManFanHost','YeManFanHost.HWiNFOTemperatureMonitor','Dispose')[0];
  const mapping=find('YeManFanHost','YeManFanHost.HWiNFOTemperatureMonitor','InvalidateCachedMapping')[0];
  const reportFields=row=>(row?.method.instructions??[]).filter(i=>/fld/.test(i.opcode)).map(i=>({offset:i.offset,opcode:i.opcode,operand:i.operand}));
  const terminals=[
    {id:'ols-and-ec',state:'EXACT_SYNTHETIC_BODIES_REVIEWED',effect:'Trace collections/flags and injected exceptions; no physical port',basis:bindings.filter(b=>b.type==='HandheldCompanion.OpenLibSys'),substitution:'Existing production-derived synthetic fixture 6d7...; raw original HC remains Cecil metadata only',originalAssertionsChanged:false},
    {id:'OneX-Close',state:oneXMissing.length?'OPEN_DIRECT_METHODS':'DIRECT_LOCAL_METHODDEF_CLOSED',effect:'Actual Host CloseCore invokes exact inherited IDevice.ECRamDirectWriteByte then owned synthetic OLS.Dispose; generic X1/OneXAOKZOE Close bodies intentionally throw',basis:bindings.filter(b=>b.type==='YeManFanHost.OneXFanDeviceLifecycle'),openEdges:oneXMissing,originalAssertionsChanged:false},
    {id:'temperature-Dispose',state:'NULL_RESOURCE_PRECONDITION_REQUIRES_FINAL_PARENT_REVIEW',effect:'Actual Dispose contains Timer.Dispose and InvalidateCachedMapping contains UnmanagedMemoryAccessor.Dispose / MemoryMappedFile.Dispose',constructorFields:reportFields(monitorCtor),disposeFields:reportFields(monitorDispose),mappingFields:reportFields(mapping),precondition:'OldWriteProbe constructs a fresh monitor, sets sampleReadTick only, never starts it; constructor does not assign timer/mapping/view. Parent must retain that invariant. Non-null external resource needs an accounted replacement before business run.',replacementImplementedThisSlice:false,originalAssertionsChanged:false},
    {id:'reflection-callbacks',state:missingBindings.length?'MISSING_EXACT_BODIES':'EXACT_FIXED_BINDING_BODIES_PRESENT',effect:'Host callback PowerProfileManager_Applied -> inherited synthetic SetFanControl; duty cold/cached -> exact SetFanDuty; temperature delegates -> FanProfile.SetTemperature/GetFanSpeed',missingBindings,originalAssertionsChanged:false},
    {id:'other-branches',state:'OPEN_CONSERVATIVE_FRONTIER',effect:'MSI/ASUS/self-test/failure/manager descendants in selected root methods remain explicit, not transitively accepted',remainingDirectEdges:[...edges.values()],replacementImplementedThisSlice:false},
  ];
  return {schemaVersion:1,status:'FINITE_METADATA_ANALYSIS_NOT_EXECUTION_APPROVAL',uniqueMethods:methods.size,images:[...images.values()],moduleInitializersObserved:0,
    fixedBranchProof,excludedMsiMethods:excludedMsiMethods.map(r=>({method:r.method.method,token:r.method.token,image:r.image,reason:'Pinned fixed OneX hierarchy and IsMsiFamily=false; no MSI ctor/Open instance created'})),
    directMethodDefFrontier:[...edges.values()],frontierCount:edges.size,oneXLifecycleDirectFrontier:oneXMissing,syntheticDirectFrontier:syntheticMissing,
    reflectionBindings:bindings,missingReflectionBindings:missingBindings,reflectionSites,externalTerminalOperands:externalLeaves,terminals,
    completeReachableGraphProven:false,fullClrIsolationProven:false,producerExecuted:false,targetRuntimeLoaded:false,sourceApprovalGranted:false,executionApprovalGranted:false,fullHostApproved:false};
}
