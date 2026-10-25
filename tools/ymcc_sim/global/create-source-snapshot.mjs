import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createSourceSnapshot,compareSourceSnapshot} from './source-snapshot.mjs';

export const SOURCE_SNAPSHOT_HELP=`Create a fixed source copy; never launch the simulator or compile anything.

node tools/ymcc_sim/global/create-source-snapshot.mjs --repo ABS_REPO --out ABS_FRESH_SIBLING
node tools/ymcc_sim/global/create-source-snapshot.mjs --compare ABS_MANIFEST --repo ABS_ORIGINAL_REPO

The destination must be a new directory immediately beside the source repository.
All included bytes are inventoried before copy, verified in the copy and inventoried
again in the original. Drift leaves the partial copy and FAIL evidence; no retries
or deletions occur. Explicit exclusions and their reasons are saved in the
manifest: root _scratch, work and
System.Management.Automation.Internal.Host.InternalHost; root build products;
non-deps .NET project bin/obj; scoped FanLab/runtime/project Logs; caches.
PowerControl is a mixed tree: code/scripts/projects, package.json and the exact
PowerControl/GameTitlePolicy.json imported by src/bridge/gamedetect.ts are preserved;
opaque runtime/settings/session/lease/log/binary bytes are not read or frozen.
Root InputHost is a production project: source and .csproj files stay included;
only its actual generated bin/obj and scoped runtime logs are excluded. deps originals
and controlled tools/fixtures log samples remain included. Excluded directories
are never descended into or read for content. This does not freeze the entire
installed payload or operational settings, sessions, leases or logs. Reparse traversal
and existing destinations are refused. Compare is read-only and checks every
included path/byte plus the entire native/main.cpp in both trees. A snapshot PASS
never approves changed mainline source. Final comparison is required after testing.
`;
export function parseSnapshotArguments(args){
 if(args.length===1&&['--help','-h'].includes(args[0]))return {mode:'help'};
 const values={};for(let i=0;i<args.length;i+=2){const flag=args[i],value=args[i+1];if(!['--repo','--out','--compare'].includes(flag)||typeof value!=='string'||!value||value.startsWith('--')||Object.hasOwn(values,flag))throw Error('SNAPSHOT_ARGUMENTS_INVALID');values[flag]=value;}
 if(!values['--repo'])throw Error('SNAPSHOT_REPO_REQUIRED');
 if(values['--out']&&!values['--compare']&&Object.keys(values).length===2)return {mode:'create',repoRoot:values['--repo'],outDir:values['--out']};
 if(values['--compare']&&!values['--out']&&Object.keys(values).length===2)return {mode:'compare',repoRoot:values['--repo'],manifestFile:values['--compare']};
 throw Error('SNAPSHOT_CREATE_OR_COMPARE_REQUIRED');
}
export function runSourceSnapshotCli(args){
 const options=parseSnapshotArguments(args);if(options.mode==='help')return {exitCode:0,text:SOURCE_SNAPSHOT_HELP};
 if(options.mode==='compare'){const result=compareSourceSnapshot(options);return {exitCode:result.isCurrent?0:2,result};}
 const value=createSourceSnapshot(options),result={schemaVersion:value.schemaVersion,kind:value.kind,status:value.status,sourceRoot:value.sourceRoot,snapshotRoot:value.snapshotRoot,manifestFile:value.manifestFile,isCurrent:value.isCurrent,currentAsOf:value.currentAsOf,fileCount:value.files.length,excludedRoots:value.excluded.length,copyVerifiedFiles:value.copyVerifiedFiles,main:value.main,sourceDiff:value.sourceDiff,snapshotDiff:value.snapshotDiff,failure:value.failure,requiresFinalCurrentComparison:true,snapshotPassApprovesChangedSource:false,executionPerformed:false,installationPayloadFrozen:false};
 return {exitCode:value.status==='PASS'?0:2,result};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const value=runSourceSnapshotCli(process.argv.slice(2));if(value.text)process.stdout.write(value.text);else console.log(JSON.stringify(value.result));process.exitCode=value.exitCode;}
 catch(error){console.error(JSON.stringify({status:'FAIL',isCurrent:false,code:error.code??error.message,message:String(error.message).slice(0,500),details:error.details??null,executionPerformed:false}));process.exitCode=3;}
}