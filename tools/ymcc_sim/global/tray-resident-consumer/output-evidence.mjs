import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {need,save,outputFiles,unchanged} from './contract.mjs';
import {runSuite,parseOptions} from './run.mjs';
export async function createOutputEvidence(options){
 const result=await runSuite(options),root=path.resolve(options.outDir),outputs=outputFiles(root);
 need(unchanged(outputs),'OUTPUT_CHANGED_BEFORE_IDENTITY_CHANNEL');
 const evidence={schemaVersion:1,id:result.id,status:result.status,ok:result.ok,role:'owned-output-identities',sourceHashes:outputs.map(({path,sha256})=>({path,sha256})),boundary:'Absolute identities confined to this owned case; tools are artifactInputs in result.json, product sourceHashes remain repository-relative. This channel has no self hash; the unchanged parent runner fingerprints this result file.'};
 save(path.join(root,'output-evidence.json'),evidence);return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const r=await createOutputEvidence(parseOptions(process.argv.slice(2)));console.log(JSON.stringify({status:r.status,receipts:r.receipts.map(r=>({kind:r.kind,accepted:r.accepted,failures:r.controlFailures,passes:r.originalResult.passedNames.length})),durationMs:r.durationMs}));process.exitCode=r.ok?0:1;}catch(e){console.error(JSON.stringify({status:'TOOL_CONTROL_FAILURE',semanticDetected:false,error:e.stack}));process.exitCode=2;}
}
