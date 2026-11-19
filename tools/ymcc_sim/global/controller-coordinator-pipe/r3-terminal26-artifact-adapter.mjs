// Registration for the existing four IDs. Large fixed-source graphs remain
// explicit nested manifests and are checked by the real leaf before/after I/O.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {terminal26ArtifactProfileActive,openTerminal26ArtifactContext,terminal26ArtifactInputs} from './r3-terminal26-artifact-inputs.mjs';
import {fixedArtifactConsumerSources,FIXED_CPP_SCOPE,FIXED_CPP_PIPE_IDS} from './r3-terminal26-artifact-run.mjs';

const MODULE='tools/ymcc_sim/global/controller-coordinator-pipe';
const union=rows=>[...new Set(rows)];

export function fixedArtifactOuterRegistration(view,repo,role){
 const {profile,native:n,preparation}=view.metadata;
 assert(profile.scope===FIXED_CPP_SCOPE&&profile.cacheAcceptance===false&&profile.toolClosureComplete===false,'FIXED_CPP_ADAPTER_SCOPE_REQUIRED');
 const q=profile.qualification[role];assert(q?.receipt?.path&&q?.raw?.path,'FIXED_CPP_ROLE_QUALIFICATION_ROOTS_REQUIRED');
 const sources=union([
  ...view.sources,...fixedArtifactConsumerSources(repo),MODULE+'/adapter.mjs',MODULE+'/r3-terminal26-artifact-adapter.mjs',
 ]);
 assert(sources.every(relative=>typeof relative==='string'&&!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..')),'FIXED_CPP_OUTER_SOURCES_MUST_BE_CONSUMER_RELATIVE');
 const artifactInputs=union([
  profile.descriptor.path,profile.expectations.path,q.receipt.path,q.raw.path,n.native.exe.path,n.native.sourceManifest.path,n.compilerLedger.path,n.preparation.path,
  preparation.layout.path,preparation.managed.manifestPath,profile.node.path,profile.powershell.path,
 ]);
 assert(artifactInputs.every(file=>path.isAbsolute(file)&&fs.statSync(file).isFile()),'FIXED_CPP_OUTER_ROOT_PIN_MISSING');
 assert(artifactInputs.length<=640&&sources.length<=640,'FIXED_CPP_EXISTING_REGISTRATION_LIMIT');
 return {evidenceInputs:sources,artifactInputs,cacheAcceptance:false,
  nestedInputContract:{kind:'FIXED_CPP_COMPLETE_ROLE_INPUT_MANIFEST_V1',descriptor:profile.descriptor,role,sourceRows:view.sourcePins.length,artifactRows:view.artifactPins.length,physicalInputs:view.inputPins.length,
   everyInputCheckedByLeaf:true,leafBeforeAndAfterOwnedProofRequired:true,wholeJavaScriptGeneratorClosureComplete:false,cacheAcceptance:false},
 };
}

export function getFixedArtifactPipeAdapters({repo,specs,selectedIds}={}){
 assert(typeof repo==='string'&&path.isAbsolute(repo));
 assert(specs?.length===4&&specs.every(row=>FIXED_CPP_PIPE_IDS[row.key]===row.id),'FIXED_CPP_ORIGINAL_FOUR_IDS_REQUIRED');
 if(!terminal26ArtifactProfileActive(repo))return null;
 const requested=selectedIds===undefined?null:new Set(selectedIds);
 return specs.map(spec=>{
  let registration,leafBudgetMs=360000;
  if(requested&&!requested.has(spec.id)){
   registration={evidenceInputs:[MODULE+'/r3-terminal26-artifact-adapter.mjs',MODULE+'/r3-terminal26-artifact-inputs.mjs',MODULE+'/r3-terminal26-artifact-prerequisites.json'],artifactInputs:[],
    blockedReason:'FIXED_CPP_INPUTS_DEFERRED_OUTSIDE_EXPLICIT_SELECTION',inputRegistrationDeferred:true,cacheAcceptance:false};
  }else{
   try{
    // One source-context inspection feeds both outer input lists.
    const context=openTerminal26ArtifactContext(spec.key,repo),view=terminal26ArtifactInputs(context);
    registration=fixedArtifactOuterRegistration(view,repo,spec.key);leafBudgetMs=view.metadata.profile.leafBudgetMs;
   }catch(error){
    registration={evidenceInputs:[MODULE+'/r3-terminal26-artifact-adapter.mjs',MODULE+'/r3-terminal26-artifact-inputs.mjs',MODULE+'/r3-terminal26-artifact-prerequisites.json'],artifactInputs:[],
     blockedReason:'FIXED_CPP_ARTIFACT_PREREQUISITE_UNAVAILABLE: '+String(error?.message??error).slice(0,2048),cacheAcceptance:false};
   }
  }
  return {id:spec.id,domain:'controller',title:'Fixed C++ artifact software pipe: '+spec.key+'; current live sources unqualified',lifecycleVersion:'VC-LIFECYCLE-20261020-R3',kind:'node',path:MODULE+'/r3-terminal26-artifact-run.mjs',
   args:['--case',spec.key,'--evidence-dir','{outDir}/'+spec.directory],timeoutMs:leafBudgetMs,profiles:['desktop','full'],scope:'behavioral-mock',...registration,resultFiles:[spec.directory+'/result.json'],
   boundary:'Exact fixed captured C++ coordinator and SDK-free managed Host over a real owned pipe. Original assertions, semantic negative sets and 124 child ownership checks preserved. Each leaf enforces native135s within finite parent budget and proves every nested input before/after. Historical whole-JavaScript generator closure and current-live-source acceptance remain unqualified; cache disabled. No product Main, real SDK/device/driver, physical return, installed application or OS sleep acceptance.'};
 });
}
