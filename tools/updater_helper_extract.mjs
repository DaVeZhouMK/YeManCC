import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(import.meta.dirname,'..');
// Extract the actual C++ emission statements, preserving all PowerShell control
// flow. Every nonliteral must be supplied explicitly; unknown expressions fail.
export function extractUpdaterHelper(source,bindings){
 const end=source.indexOf('helperScriptWritten = f.good()');
 const start=source.lastIndexOf('std::ofstream f(script',end);
 if(start<0||end<start)throw Error('helper emission boundaries missing');
 const block=source.slice(start,end);let output='\ufeff',statements=0;
 const re=/\bf\s*<<((?:"(?:[^"\\]|\\.)*"|[^";])*);/g;
 for(const match of block.matchAll(re)){
  const parts=match[1].match(/"(?:[^"\\]|\\.)*"|[^"<>]+/g)||[];
  for(let part of parts){part=part.trim();if(!part)continue;
   if(part.startsWith('"'))output+=JSON.parse(part);
   else if(Object.hasOwn(bindings,part))output+=bindings[part];
   else throw Error('Unbound production emitter expression: '+part);
  }++statements;
 }
 if(statements<300)throw Error('incomplete production script');
 return {script:output,statements};
}
export function updaterFixtureBindings(config){
 const ps=x=>"'"+String(x).replaceAll("'","''")+"'";
 const bindings={};
 const values={zip:config.zip??'',staging:config.staging,exePath:path.win32.join(config.install,'YeManCC','YeManCC.exe'),exedir:path.win32.join(config.install,'YeManCC'),packagedYeManCC:path.win32.join(config.staging,'YeManCC'),packagedExe:path.win32.join(config.staging,'YeManCC','YeManCC.exe'),pcDir:path.win32.join(config.install,'PowerControl'),installRoot:config.install,packagedNestedCustomSteamLibrary:path.win32.join(config.staging,'YeManCC','CustomSteamLibrary'),packagedLegacyCustomSteamLibrary:path.win32.join(config.staging,'CustomSteamLibrary'),customSteamLibraryDir:path.win32.join(config.install,'YeManCC','CustomSteamLibrary'),supportPath:path.win32.join(config.install,'YeMan-Support.html'),script:config.script,recoveryJournal:config.recoveryJournal??path.win32.join(config.install,'fixture-install-journal.json')};
 for(const[k,v]of Object.entries(values))bindings['psLiteral('+k+')']=ps(v);
 Object.assign(bindings,{'GetCurrentProcessId()':String(config.parentPid),'std::to_string(updaterCreatedTicks)':String(config.parentCreationFileTime??'0'),'deckyResumeTicket':'','psLiteral(updateProgressPath())':ps(config.progress),'psLiteral(U2W(operationId))':ps(config.operationId??'fixture-operation'),'psLiteral(U2W(version))':ps(config.version),'psLiteral(U2W(ascii_lower(sha)))':ps(config.sha??'0'.repeat(64)),'ymcc::deckypackage::maintenanceScript()':fs.readFileSync(path.join(repo,'native/decky_package_maintenance.ps1'),'utf8').replace(/^\ufeff/,''),'ymcc::modulepackage::maintenanceScript()':fs.readFileSync(path.join(repo,'native/module_package_maintenance.ps1'),'utf8').replace(/^\ufeff/,''),'ymcc::updaterecovery::script()':fs.readFileSync(path.join(repo,'native/update_install_recovery.ps1'),'utf8').replace(/^\ufeff/,''),'ymcc::hcruntime::script()':fs.readFileSync(path.join(repo,'native/updater_hc_runtime.ps1'),'utf8').replace(/^\ufeff/,''),'ymcc::updatehooks::script()':fs.readFileSync(path.join(repo,'native/updater_hook_tools.ps1'),'utf8').replace(/^\ufeff/,''),'updateHookPathsLiteral':'@('+((config.hookToolPaths??[]).map(ps).join(','))+')'});return bindings;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const config=JSON.parse(fs.readFileSync(process.argv[3],'utf8').replace(/^\ufeff/,''));
 const result=extractUpdaterHelper(fs.readFileSync(process.argv[2],'utf8'),updaterFixtureBindings(config));
 fs.writeFileSync(config.script,result.script);console.log(JSON.stringify({statements:result.statements,output:config.script}));
}
