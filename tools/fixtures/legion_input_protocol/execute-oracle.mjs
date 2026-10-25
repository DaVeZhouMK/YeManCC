import fs from 'node:fs';
import path from 'node:path';
import { sha, csharpOracle, makeVectors, oracleHeader } from './oracle.mjs';

export function executeCSharpOracle(primary, out, sourceDir, run, success) {
  // Installed SDK/compiler/reference packs only. Direct csc has no package restore.
  const oracleDir = path.join(out, 'csharp-oracle'); fs.mkdirSync(oracleDir);
  const dotnetRoot = path.join(process.env.ProgramFiles || 'C:/Program Files', 'dotnet');
  const dotnetExe = path.join(dotnetRoot, 'dotnet.exe');
  success(run(dotnetExe, ['--list-sdks'], 'csharp-oracle/installed-sdks.log'), 'installed SDK listing');
  success(run(dotnetExe, ['--list-runtimes'], 'csharp-oracle/installed-runtimes.log'), 'installed runtime listing');
  const latest = entries => entries.filter(name => /^\d+\.\d+\.\d+$/.test(name)).sort((a,b) => a.localeCompare(b, undefined, { numeric:true })).at(-1);
  const sdkVersion = latest(fs.readdirSync(path.join(dotnetRoot, 'sdk')));
  const packVersion = latest(fs.readdirSync(path.join(dotnetRoot, 'packs/Microsoft.NETCore.App.Ref')));
  if (!sdkVersion || !packVersion) throw Error('Installed .NET SDK/reference pack unavailable');
  const framework = 'net' + packVersion.split('.').slice(0,2).join('.');
  const referenceDir = path.join(dotnetRoot, 'packs/Microsoft.NETCore.App.Ref', packVersion, 'ref', framework);
  const runtimeDir = path.join(dotnetRoot, 'shared/Microsoft.NETCore.App', packVersion);
  if (!fs.existsSync(runtimeDir)) throw Error('Matching installed runtime required; download not permitted');
  const csc = path.join(dotnetRoot, 'sdk', sdkVersion, 'Roslyn/bincore/csc.dll');
  const csFile = path.join(oracleDir, 'source-body-oracle.cs'), oracleDll = path.join(oracleDir, 'source-body-oracle.dll');
  const csText = csharpOracle(primary); fs.writeFileSync(csFile, csText);
  fs.writeFileSync(path.join(oracleDir, 'extracted-tablet-switch.cs.txt'), primary.tabletSwitch);
  fs.writeFileSync(path.join(oracleDir, 'extracted-gos-assignments.cs.txt'), primary.single.join('\n')+'\n');
  fs.writeFileSync(path.join(oracleDir, 'tablet-attachdetails-fragment.cs.txt'), primary.attachment);
  const response = path.join(oracleDir, 'compile.rsp');
  const refs = fs.readdirSync(referenceDir).filter(name => name.endsWith('.dll')).sort().map(name => path.join(referenceDir,name));
  const cscArguments = ['/nologo','/utf8output','/nostdlib+','/optimize+','/checked-','/warnaserror+','/target:exe',
    '/out:"'+oracleDll+'"', ...refs.map(file => '/reference:"'+file+'"'), '"'+csFile+'"'];
  fs.writeFileSync(response, cscArguments.join('\n')+'\n');
  success(run(dotnetExe, [csc, '/noconfig', '@'+response], 'csharp-oracle/compile.log', oracleDir), 'actual extracted C# source compilation');
  fs.writeFileSync(path.join(oracleDir, 'source-body-oracle.runtimeconfig.json'), JSON.stringify({ runtimeOptions:{tfm:framework,framework:{name:'Microsoft.NETCore.App',version:packVersion},rollForward:'Disable'} },null,2)+'\n');
  const vectors=makeVectors();
  const vectorsPath=path.join(oracleDir,'vectors.json'), inputPath=path.join(oracleDir,'inputs.tsv'), resultsPath=path.join(oracleDir,'results.tsv');
  fs.writeFileSync(vectorsPath,JSON.stringify(vectors,null,2)+'\n');
  fs.writeFileSync(inputPath,vectors.map(v=>[v.id,v.protocol,v.sensorIndex,v.byte1Zero?1:0,v.hex].join('|')).join('\n')+'\n');
  success(run(dotnetExe,[oracleDll,inputPath,resultsPath],'csharp-oracle/run.log',oracleDir),'actual extracted C# source execution');
  const generated=oracleHeader(vectors,fs.readFileSync(resultsPath,'utf8'));
  fs.writeFileSync(path.join(sourceDir,'oracle.generated.h'),generated);
  return {status:'compiled-and-executed',sdkVersion,packVersion,framework,vectors:vectors.length,
    compiler:{path:csc,sha256:sha(fs.readFileSync(csc))},sourceSha256:sha(csText),compiledDllSha256:sha(fs.readFileSync(oracleDll)),
    vectorsSha256:sha(fs.readFileSync(vectorsPath)),resultsSha256:sha(fs.readFileSync(resultsPath)),generatedHeaderSha256:sha(generated),
    originalTabletSwitchSha256:sha(primary.tabletSwitch),originalGoSAssignmentsSha256:sha(primary.single.join('\n')),
    packageRestore:false,download:false,shippedHcNativeOrManagedDllExecuted:false,tabletShiftAdapter:false};
}
