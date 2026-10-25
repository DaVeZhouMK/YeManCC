import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {need,owned,hashFile,readJson,writeNew,executeStage,naturalTerminal} from './helper.mjs';
import {runtimeMembers,mainReturnMembers} from './adapter.mjs';
const leaf=path.dirname(fileURLToPath(import.meta.url));
const output=owned(leaf,path.join(leaf,'maintenance-evidence',process.argv[2]??'checks-01'));
need(!fs.existsSync(output),'RADIAL_CHECK_EVIDENCE_MUST_BE_FRESH');fs.mkdirSync(output,{recursive:true});
const tools=readJson(path.join(leaf,'toolchain-inputs.json'));
const env={...process.env,PSModulePath:path.join(path.dirname(tools.executables.powershell.path),'Modules')};delete env.NODE_OPTIONS;delete env.NODE_PATH;
const records=[];
const checkedScripts=[...fs.readdirSync(leaf).filter(n=>n.endsWith('.mjs')),...fs.readdirSync(path.join(leaf,'main-return')).filter(n=>n.endsWith('.mjs')).map(n=>'main-return/'+n)].sort();
for(const name of checkedScripts){
 const record=await executeStage({root:output,exe:tools.executables.node.path,args:['--check',path.join(leaf,name)],label:'syntax-'+name.replaceAll('.','-').replaceAll('/','-'),env,timeoutMs:10000});
 records.push({kind:'node-syntax',source:name,...record});
}
const contract=await executeStage({root:output,exe:tools.executables.node.path,args:['--test',path.join(leaf,'contract.test.mjs'),path.join(leaf,'main-return/contract.test.mjs')],label:'node-contract',env,timeoutMs:15000});records.push({kind:'contract-tests',...contract});
const quote=value=>"'"+value.replaceAll("'","''")+"'";
const psCommand="$radialTokens=$null;$radialParseErrors=$null;[void][System.Management.Automation.Language.Parser]::ParseFile("+quote(path.join(leaf,'Run-Radial.ps1'))+",[ref]$radialTokens,[ref]$radialParseErrors);[pscustomobject]@{status=$(if($radialParseErrors.Count -eq 0){'PASS'}else{'FAIL'});errors=@($radialParseErrors|ForEach-Object{$_.Message});scriptExecuted=$false;nativeCompiled=$false}|ConvertTo-Json -Depth 4;if($radialParseErrors.Count -gt 0){exit 2}";
const parser=await executeStage({root:output,exe:tools.executables.powershell.path,args:['-NoLogo','-NoProfile','-NonInteractive','-Command',psCommand],label:'powershell-parser',env,timeoutMs:15000});records.push({kind:'powershell-parser-only',...parser});
const passed=records.every(record=>naturalTerminal(record,0));
const report={schemaVersion:1,status:passed?'PASS':'FAIL',checks:records.map(({kind,source,pid,naturalExit,signal,timedOut,outputComplete,outputTruncated,launchError,exe,exeSha256,stdout,stderr})=>({kind,source:source??null,pid,naturalExit,signal,timedOut,outputComplete,outputTruncated,launchError,exe,exeSha256,stdout,stderr})),sourceIdentities:[...new Set([...runtimeMembers,...mainReturnMembers,'contract.test.mjs','main-return/contract.test.mjs','check.mjs'])].sort().map(name=>({path:name,sha256:hashFile(path.join(leaf,name))})),contractTestsAreValidatorChecksNotNativeBehaviorEvidence:true,nativeCompiled:false,nativeExecuted:false,globalProducerOrAudit:false};
writeNew(path.join(output,'report.json'),report);console.log(JSON.stringify({status:report.status,checks:records.length,output,nativeCompiled:false,nativeExecuted:false}));if(!passed)process.exitCode=2;
