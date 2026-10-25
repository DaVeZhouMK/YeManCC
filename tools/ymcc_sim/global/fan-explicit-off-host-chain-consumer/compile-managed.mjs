import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function compileManaged({inputRoot,moduleRelative,root}){
 const leaf=path.join(inputRoot,moduleRelative),contract=JSON.parse(fs.readFileSync(path.join(leaf,'managed-compiler-contract.json'),'utf8')),tool=JSON.parse(fs.readFileSync(path.join(leaf,'toolchain-inputs.json'),'utf8'));
 const out=path.join(root,'managed-direct-payload');if(fs.existsSync(out))throw Error('FRESH_DIRECT_MANAGED_PAYLOAD_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const ledger=[],programs=[];
 const pinned=new Map(tool.files.map(row=>[path.resolve(row.path).toLowerCase(),row.sha256]));
 const quote=value=>'"'+value+'"';
 for(const project of contract.projects){
  const name=project.properties.AssemblyName,target=project.properties.OutputType==='Exe'?'exe':'library',dll=path.join(out,name+'.dll'),pdb=path.join(out,name+'.pdb');
  const args=['/noconfig','/nostdlib+','/nologo','/utf8output','/target:'+target,'/out:'+dll,'/pdb:'+pdb,'/debug:portable','/optimize+','/deterministic+','/langversion:'+project.properties.LangVersion,'/nullable:'+project.properties.Nullable,'/define:'+project.properties.DefineConstants,'/warn:10','/warnaserror+','/nowarn:1701,1702','/errorendlocation','/highentropyva+','/filealign:512'];
  if(project.properties.PlatformTarget)args.push('/platform:'+project.properties.PlatformTarget);
  const sources=[];
  for(const entry of project.compile){let source=path.join(inputRoot,entry.source);if(hash(source)!==entry.sha256)throw Error('MANAGED_SOURCE_IDENTITY_DIFFERS:'+entry.source);
   if(entry.guardedOriginalHarness){
    const text=fs.readFileSync(source,'utf8'),anchor='    var engine = Activator.CreateInstance(T("FanHostEngine"), flags, null, new[] { options, diagnostics }, null)!;',eol=text.includes('\r\n')?'\r\n':'\n';if(text.split(anchor).length!==2)throw Error('ORIGINAL_ENGINE_FACTORY_ANCHOR');
    const transformed=text.replace(anchor,'    ExplicitChainBoundary.RequireMockOptions(options);'+eol+anchor+eol+'    ExplicitChainBoundary.ObserveMockEngine(engine);');
    const checks=value=>value.split(/\r?\n/).filter(line=>line.trim().startsWith('Check('));if(JSON.stringify(checks(text))!==JSON.stringify(checks(transformed)))throw Error('ORIGINAL_MANAGED_CHECKS_CHANGED');
    source=path.join(out,'guarded-original-harness.cs');fs.writeFileSync(source,transformed);
   }
   sources.push(source);ledger.push({kind:'compile',project:project.key,path:source,sourceIdentity:entry.source,sha256:hash(source),originalSourceSha256:entry.sha256,guardedOriginalHarness:entry.guardedOriginalHarness===true});
  }
  for(const ref of project.references){const file=ref.projectAssembly?path.join(out,ref.projectAssembly):ref.path;if(!fs.existsSync(file))throw Error('MANAGED_REFERENCE_MISSING:'+file);const expected=ref.sha256??programs.find(row=>path.basename(row.dll)===ref.projectAssembly)?.dllSha256;if(hash(file)!==expected||(!ref.projectAssembly&&pinned.get(path.resolve(file).toLowerCase())!==expected))throw Error('MANAGED_REFERENCE_IDENTITY_DIFFERS:'+file);args.push('/reference:'+file);ledger.push({kind:'reference',project:project.key,path:file,sha256:hash(file),privateProjectAssembly:!!ref.projectAssembly});}
  for(const analyzer of project.analyzers){if(hash(analyzer.path)!==analyzer.sha256||pinned.get(path.resolve(analyzer.path).toLowerCase())!==analyzer.sha256)throw Error('MANAGED_ANALYZER_IDENTITY_DIFFERS');args.push('/analyzer:'+analyzer.path);ledger.push({kind:'analyzer',project:project.key,path:analyzer.path,sha256:analyzer.sha256});}
  args.push(...sources);const responseFile=path.join(out,project.key+'.csc.rsp');fs.writeFileSync(responseFile,args.map(quote).join('\n')+'\n');
  const env={...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1'};delete env.NODE_OPTIONS;delete env.NODE_PATH;
  const child=spawnSync(contract.dotnet,[contract.csc,'@'+responseFile],{cwd:out,env,windowsHide:true,encoding:'utf8',timeout:45000,maxBuffer:4*1024*1024});
  fs.writeFileSync(path.join(out,project.key+'.compile.stdout.log'),child.stdout??'');fs.writeFileSync(path.join(out,project.key+'.compile.stderr.log'),child.stderr??'');
  const row={project:project.key,exitCode:child.status,error:child.error?.code??null,signal:child.signal,responseFile,responseSha256:hash(responseFile),dll,dllSha256:fs.existsSync(dll)?hash(dll):null,pdb,pdbSha256:fs.existsSync(pdb)?hash(pdb):null};programs.push(row);fs.writeFileSync(path.join(out,'compile-result.json'),JSON.stringify({status:'COMPILATION_IN_PROGRESS',programs,ledger},null,2)+'\n');
  if(child.status!==0||child.error||child.signal)throw Error('DIRECT_ORIGINAL_CSHARP_COMPILE_FAILED:'+project.key+':'+child.stdout+':'+child.stderr);
 }
 for(const item of contract.runtimeSupport){if(hash(item.path)!==item.sha256)throw Error('MANAGED_RUNTIME_SUPPORT_CHANGED');const destination=path.join(out,item.name);fs.copyFileSync(item.path,destination);if(hash(destination)!==item.sha256)throw Error('MANAGED_RUNTIME_SUPPORT_COPY_CHANGED');ledger.push({kind:'runtime-support',path:item.path,sha256:item.sha256,copy:destination});}
 const result={status:'DIRECT_ORIGINAL_MANAGED_SOURCE_COMPILED',programs,ledger,compiler:contract.csc,compilerSha256:hash(contract.csc),actualManagedProgramExecuted:false,originalMainInvoked:false,originalManagedChecksPreserved:true};fs.writeFileSync(path.join(out,'compile-result.json'),JSON.stringify(result,null,2)+'\n');return result;
}
