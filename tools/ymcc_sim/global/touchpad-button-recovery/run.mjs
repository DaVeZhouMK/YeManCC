import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const out=path.resolve(process.argv[2]??'');
if(process.argv.length!==3)throw Error('Expected explicit evidence directory');
fs.mkdirSync(out,{recursive:true});
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const jobs=[
 ['offline', 'python',['tools/screen_touchpad_settings_reveal_selftest.py'],[]],
 ['negative-abort','python',['tools/screen_touchpad_settings_reveal_selftest.py'],['--negative-abort']],
 ['negative-missing','python',['tools/screen_touchpad_settings_reveal_selftest.py'],['--negative-missing']],
 ['negative-reveal','python',['tools/screen_touchpad_settings_reveal_selftest.py'],['--negative']],
 ['native','pwsh',['-NoProfile','-File','tools/screen_touchpad_button_abort_native_selftest.ps1','-OutputDirectory'],[]],
 ['exit','pwsh',['-NoProfile','-File','tools/screen_touchpad_exit_release_selftest.ps1','-OutputDirectory'],[]],
];
const report={ok:false,scope:'Scoped button recovery, settings reveal and exit consumers',featureClassification:'persistent',fullLifecycleAcceptance:false,physicalHardwareAcceptance:false,productDeployed:false,inputInjected:false,results:[],startedAt:new Date().toISOString()};
try{
 for(const [name,command,prefix,suffix]of jobs){
  const destination=path.join(out,name);const result=spawnSync(command,[...prefix,destination,...suffix],{cwd:root,windowsHide:true,encoding:'utf8',timeout:45000,maxBuffer:2*1024*1024});
  fs.writeFileSync(path.join(out,name+'-stdout.log'),result.stdout??'');fs.writeFileSync(path.join(out,name+'-stderr.log'),result.stderr??'');
  const receipt=read(path.join(destination,'receipt.json'));
  // Negative tools retain the original failing executable's exitCode internally,
  // while their receipt records the exact expected failure-set match.
  if(!receipt.ok||result.error)throw Error(name+': consumer failed');
  if(result.status!==0)throw Error(name+': runner returned '+result.status);
  report.results.push({name,ok:receipt.ok,checks:receipt.checks,receipt:path.join(destination,'receipt.json'),negativeDetected:receipt.negativeDetected??false});
 }
 report.ok=true;
}catch(error){report.error=String(error.stack??error);process.exitCode=1;}
finally{report.finishedAt=new Date().toISOString();fs.writeFileSync(path.join(out,'button-recovery-result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({ok:report.ok,results:report.results,error:report.error}));}
