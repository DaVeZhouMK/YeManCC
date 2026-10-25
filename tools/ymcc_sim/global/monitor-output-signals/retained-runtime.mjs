import fs from'node:fs';import{root,createAuditPlugin}from'./common.mjs';
import{lockMonitorToolScope,verifyMonitorToolScope,loadMonitorCompiler,observeMonitorToolRuntime}from'./tool-authority.mjs';
const loaded=new Map(),toolAuthority=lockMonitorToolScope(root),{build}=loadMonitorCompiler(toolAuthority);
export async function auditedBuild(options){
 verifyMonitorToolScope(toolAuthority);
 const result=await build({...options,metafile:true,plugins:[createAuditPlugin(loaded),...(options.plugins??[])]});
 verifyMonitorToolScope(toolAuthority);
 const toolRuntime=observeMonitorToolRuntime(toolAuthority,{bundleInputs:Object.keys(result.metafile.inputs)});
 fs.writeFileSync(process.env.YMCC_OUTPUT_LOAD_RECEIPT,JSON.stringify({sourceApproval:false,loadedSourceHashes:Object.fromEntries(loaded),metafile:result.metafile,toolScopeVerified:true,toolAuthority,toolRuntime},null,2));return result;
}

// The original producer can load Vue/TypeScript after compilation. Capture those actual loads too.
export function finalizeRetainedToolRuntime(){
 const file=process.env.YMCC_OUTPUT_LOAD_RECEIPT,receipt=JSON.parse(fs.readFileSync(file,'utf8'));
 receipt.toolRuntime=observeMonitorToolRuntime(toolAuthority,{bundleInputs:Object.keys(receipt.metafile.inputs)});
 receipt.actualLoadsCapturedAfterOriginalProducer=true;
 fs.writeFileSync(file,JSON.stringify(receipt,null,2));
}
