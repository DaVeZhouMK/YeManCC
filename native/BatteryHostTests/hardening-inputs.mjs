import fs from 'node:fs';import path from 'node:path';import{fileURLToPath}from 'node:url';
export function generateHardeningInputs(output){
 const here=path.dirname(fileURLToPath(import.meta.url)),native=path.resolve(here,'../BatteryHost');
 const rep=(s,a,b)=>{if(s.split(a).length!==2)throw Error('EXACT_NEGATIVE_SPAN '+a);return s.replace(a,b);};
 fs.mkdirSync(output,{recursive:true});
 let runtime=fs.readFileSync(path.join(native,'InProcessEntry.cs'),'utf8').replaceAll('\r\n','\n').replaceAll('InProcessEntry','InProcessEntryMissingDrain').replaceAll('InProcessRuntime','InProcessRuntimeMissingDrain').replaceAll('InProcessOwnerLease','InProcessOwnerLeaseMissingDrain');
 runtime=rep(runtime,'bool workDrained=WaitSystemPowerWork(left);','bool workDrained=true;_ = left; // exact missing-drain semantic negative');
 const original=fs.readFileSync(path.join(native,'Controller.cs'),'utf8').replaceAll('\r\n','\n'),body=original.slice(original.indexOf('internal sealed class Controller'));
 const prefix='#nullable enable\nusing System.Security.Cryptography;using System.Text;using System.Text.Json;\nnamespace YeManBatteryHost;\n';
 const refresh=rep(body,'                    RequireAvailable(); // Acknowledgement cannot discard ownership after an old slow read.\n','').replace(/\bController\b/g,'MissingRefreshController');
 const healthy=rep(body,'resumeCleanupPending=providerCleanupDebt.Count!=0||(!active&&recovery is {Software:true});','resumeCleanupPending=providerCleanupDebt.Count!=0||recovery is {Software:true};').replace(/\bController\b/g,'HealthyJournalController');
 const outputs={'MissingDrain.g.cs':'#nullable enable\n'+runtime,'MissingRefresh.g.cs':prefix+refresh,'HealthyJournal.g.cs':prefix+healthy};
 for(const [name,text]of Object.entries(outputs))fs.writeFileSync(path.join(output,name),text,{flag:'wx'});
 return Object.keys(outputs).map(name=>path.join(output,name));
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){if(process.argv.length!==3)throw Error('Usage: hardening-inputs.mjs <new private output directory>');console.log(JSON.stringify(generateHardeningInputs(path.resolve(process.argv[2]))));}
