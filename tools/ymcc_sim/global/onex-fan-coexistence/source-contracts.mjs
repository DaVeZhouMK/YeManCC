// Read-only source contracts and private source faults for existing fan audits.
import fs from 'node:fs';
import path from 'node:path';
import { block } from '../fan-open-signals/extract.mjs';
export const fixedSourceContractInputs=[
  'native/main.cpp','src/bridge/fanHost.ts','src/bridge/fanApi.ts',
  'src/bridge/fanHost/startupConfiguration.ts','src/bridge/fanHost/deployment.ts','src/bridge/fanHost/capabilityAdmission.ts',
  ...['App.xaml.cs','Views/Windows/MainWindow.xaml.cs','Managers/ManagerFactory.cs','Managers/DeviceManager.cs','Managers/PowerProfileManager.cs','Managers/SystemManager.cs','Misc/FanProfile.cs'].map(file=>'deps/handheldcompanion-runtime/source/'+file)
];
export function sourceContractInputs(repo) {
  const directory=path.join(repo,'deps/handheldcompanion-runtime/source/Devices');
  const walk=root=>fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(root,e.name)):e.isFile()&&e.name.endsWith('.cs')?[path.relative(repo,path.join(root,e.name)).replaceAll('\\','/')]:[]);
  return [...new Set([...fixedSourceContractInputs,...walk(directory)])].sort();
}
export function runSourceContracts({repo,out,run,powershell}) {
  fs.mkdirSync(out,{recursive:true});
  const shell=powershell??path.join(process.env.SystemRoot??'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe');
  if(!fs.existsSync(shell))throw Error('Windows PowerShell is required for the existing read-only lifecycle contracts');
  const original=fs.readFileSync(path.join(repo,'FanLab/real-host/Program.cs'),'utf8');
  const frontend=fs.readFileSync(path.join(repo,'src/bridge/fanHost.ts'),'utf8');
  const rows=[];
  function once(source,before,after){if(source.split(before).length!==2)throw Error('Source fault anchor missing/ambiguous: '+before);return source.replace(before,()=>after);}
  function hostMethod(signature,transform){const text=block(original,signature);return once(original,text,transform(text));}
  function check(id,audit,{host=original,front=frontend,error}={}) {
    const dir=path.join(out,id);fs.mkdirSync(dir);
    const hostFile=path.join(dir,'Program.cs'),frontFile=path.join(dir,'fanHost.ts');fs.writeFileSync(hostFile,host);fs.writeFileSync(frontFile,front);
    const script='tools/'+audit;
    const args=['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(repo,script),'-HcRoot',path.join(repo,'deps/handheldcompanion-runtime/source'),'-HostSource',hostFile];
    if(audit.includes('deep'))args.push('-NativeSource',path.join(repo,'native/main.cpp'),'-FrontendSource',frontFile,'-OutputRoot',path.join(dir,'deep-output'));
    const log=path.join(out,id+'.log');
    run(shell,args,path.relative(path.dirname(out),log),30000,error?1:0);
    const text=fs.readFileSync(log,'utf8');if(error&&!text.replace(/\s+/g,'').includes(error.replace(/\s+/g,'')))throw Error('Source fault did not reach the expected contract: '+id+' '+error);
    rows.push({id,audit,status:'PASS',expectedExitCode:error?1:0,expectedFailure:error??null,hostSourceChanged:host!==original,frontendSourceChanged:front!==frontend,log});
  }
  const enable='fan_hc_enable_alignment_selftest.ps1',order='fan_hc_lifecycle_order_audit.ps1',deep='fan_hc_lifecycle_deep_selftest.ps1';
  for(const audit of [enable,order,deep])check('current-'+audit,audit);
  check('fault-first-start',enable,{host:once(original,'() => Invoke(device!, "Initialize", false, false)','() => Invoke(device!, "Initialize", true, false)'),error:'Host handshake uses non-FirstStart HC initialization through the formal stage'});
  const noVirtual=hostMethod('private void CloseObservedInheritedEcFanDevice()',text=>once(text,'try { Invoke(device!, "Close"); }','try { /* private source fault: missing virtual Close */ }'));
  const noPostIdentity=hostMethod('private void CloseObservedInheritedEcFanDevice()',text=>{
    const eol=text.includes('\r\n')?'\r\n':'\n';return once(text,'            observation.RequireCurrent(device);'+eol+'            observation.CompleteClose();','            observation.CompleteClose();');
  });
  const noManagerCleanup=hostMethod('private void CloseHcDevice()',text=>once(text,'try { ConfirmManagerCleanupAfterClose(); }','try { /* private source fault: missing cleanup confirmation */ }'));
  for(const audit of [order,deep]) {
    check('fault-virtual-close-'+audit,audit,{host:noVirtual,error:'YeMan observed virtual Close missing token: try { Invoke(device!, "Close"); }'});
    check('fault-post-close-identity-'+audit,audit,{host:noPostIdentity,error:'YeMan observed Close retirement missing token: observation.RequireCurrent(device);'});
    check('fault-manager-confirmation-'+audit,audit,{host:noManagerCleanup,error:'missing token: try { ConfirmManagerCleanupAfterClose(); }'});
  }
  check('fault-inherited-bool-consumption',enable,{host:once(original,'if (!write(address, value))','if (false /* private source fault: ignore original bool */)'),error:'inherited EC bool consumer retains exact root ownership, metadata, readiness and failure gates'});
  check('fault-timeout-receipt-owner',enable,{host:once(original,'if (!backend.IsFanDispatchOwnerCurrent(receipt.ReturnOwner))','if (false /* private source fault: ignore return owner */)'),error:'a timed-out HC call is acknowledged only after its HC callback or complete terminal'});
  check('fault-temperature-signal-owner',enable,{host:once(original,'!realBackend.IsTemperatureStaleCurrent(signal)','false /* private source fault: ignore stale signal owner */'),error:'a stale isolated temperature sample restores OEM and remains resumable'});
  const replay=block(frontend,'private async replayResumeCurve(generation: number, intentRevision: number): Promise<void>');
  check('fault-resume-terminal-refusal',deep,{front:once(frontend,replay,once(replay,'if (isTerminalFanControlRefusal(error))','if (false /* private source fault: swallow terminal refusal */)')),error:'frontend resume read/refusal/currentness missing token: if (isTerminalFanControlRefusal(error))'});
  const result={status:'PASS',scope:'Read-only current source contracts and private source counterexamples; not runtime or hardware proof',positiveAudits:3,expectedRejectedSourceFaults:rows.filter(r=>r.expectedExitCode===1).length,cases:rows,hardwareIo:false,hostMainStarted:false};
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;
}
