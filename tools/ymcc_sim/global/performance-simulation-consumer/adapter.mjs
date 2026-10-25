import path from 'node:path';
import {ID,MODULE,lockToolsSync,sourceDeclaration} from './contract.mjs';

/** Synchronous public descriptor factory. Full capture/source before-after stays in async run.mjs. */
export function getPerformanceSimulationConsumerAdapters(repoRoot=path.resolve(import.meta.dirname,'../../../..'),options={}){
 const root=path.resolve(repoRoot),declaration=sourceDeclaration();let context,blockedReason;
 try{context=lockToolsSync(root,options)}catch(error){blockedReason='PERFORMANCE_SIMULATION_AUTHORITY_UNAVAILABLE:'+error.message}
 const effectiveEnv=options.env??process.env;const supplied=options.binding??{bindingManifestFile:effectiveEnv.YMCC_BATTERY_UI_NODE_TOOLS_BINDING,expectedBindingSha256:effectiveEnv.YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256};
 const bindingArgs=context?.bindingFile?[context.bindingFile,context.bindingSha256]:supplied.bindingManifestFile&&supplied.expectedBindingSha256?[supplied.bindingManifestFile,supplied.expectedBindingSha256]:[];
 const descriptor={id:ID,domain:'performance',title:'Actual performance simulation bridge/page draft, explicit Save and close lifecycle',kind:'node',path:MODULE+'/run.mjs',args:['--repo-root',root,'--evidence-dir','{outDir}/consumer',...(bindingArgs.length?['--binding',bindingArgs[0],'--binding-sha256',bindingArgs[1]]:[])],timeoutMs:300000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['consumer/result.json'],evidenceInputs:[...new Set([...declaration.sources,...declaration.moduleFiles.map(name=>MODULE+'/'+name)])],artifactInputs:context?.artifacts.map(row=>row.path)??[],...(blockedReason?{blockedReason}:{}),completionContract:{schemaVersion:1,rules:[{line:'PASS '+ID}]},boundary:'Actual caller-selected captured or primary source; complete standard Node/UI tools, memory configuration/system ports and owned headless Edge only; global active, joint=false, CPU power benefit=null. No floating-source or physical approval.'};
 return [descriptor];
}
export default getPerformanceSimulationConsumerAdapters;

