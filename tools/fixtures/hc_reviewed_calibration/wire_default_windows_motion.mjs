import fs from 'node:fs';import path from 'node:path';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
// Replace only the current candidate namespace's real selector/capability bodies.
// Archived before/historical namespaces stay untouched and retain their hashes.
export function wireDefaultWindowsMotion(base,source){
 if(!source.includes('static bool inputCaptureDefaultHcMotionIdentity('))return{code:base,bindings:[]};
 const start=base.indexOf('namespace candidate {');if(start<0)throw Error('Candidate namespace missing');
 let prefix=base.slice(0,start),tail=base.slice(start);const bindings=[];
 const receipt=extract(source,'HcMotionSelectorReceipt','struct'),oldReceipt=extract(tail,'HcMotionSelectorReceipt','struct');
 tail=tail.replace(oldReceipt.code,()=>receipt.code);bindings.push(receipt);
 const names=['inputCaptureGenericHcMotionRecipe','inputCaptureResolveGenericHcMotionSelector','inputCaptureGenericHcMotionSelectorBound','inputCaptureMotionDeviceClass',
  'inputCaptureDefaultHcMotionIdentity','inputCaptureResolveDefaultHcMotionSelector','inputCaptureDefaultHcMotionSelectorBound',
  'inputCaptureResolveNewHcMotionSelector','inputCaptureNewHcMotionSelectorBound','inputCaptureResolveLegacyHcMotionSelector','inputCaptureLegacyHcMotionSelectorBound',
  'inputCaptureResolveMsiMotionSelector','inputCapturePrivateHcMotionSelectorRecipe','inputCaptureMotionSelectorBound','inputCaptureResolveHcMotionSelector',
  'inputCaptureApplyHcMatrixCandidate','inputCaptureMotionCapabilityReceipt'];
 const missing=[];
 for(const name of names){const next=extract(source,name);let old;try{old=extract(tail,name);}catch(error){if(!String(error.message).includes('Expected one function definition'))throw error;}
  if(old)tail=tail.replace(old.code,()=>next.code);else missing.push(next);bindings.push(next);
 }
 if(missing.length){const first=extract(tail,'inputCaptureResolveNewHcMotionSelector').code;missing.sort((a,b)=>a.startUtf8Byte-b.startUtf8Byte);tail=tail.replace(first,()=>missing.map(b=>b.code).join('\n\n')+'\n\n'+first);}
 for(const name of ['HcMotionCapabilityCatalog.h','hc_default_motion_catalog.h','hc_generic_motion_catalog.h','hc_oem_windows_motion_catalog.h'])if(!prefix.includes('#include "'+name+'"'))prefix+='\n#include "'+name+'"\n';
 return {code:prefix+tail,bindings:bindings.map(({code,...b})=>b)};
}

// Keep older calibration regressions bound to the actual processing source
// helpers/getters. Their raw/SDK boundary stays controlled; no device is opened.
export function wireWindowsCalibrationContext(base,source){
 if(!source.includes('static bool inputCaptureWindowsCalibrationFrameCurrent('))return {code:base,bindings:[]};
 const start=base.indexOf('namespace candidate {');if(start<0)throw Error('Candidate namespace missing for calibration context');
 let prefix=base.slice(0,start),tail=base.slice(start);
 const marker='static InputCaptureSensorMeta g_sensorMeta;';
 if(tail.split(marker).length!==2)throw Error('Candidate metadata declaration missing');
 const contextNames=['t_windowsProcessingMeta','t_windowsProcessingStamp'];if(source.includes('static thread_local const WindowsSensorView* t_windowsProcessingView='))contextNames.push('t_windowsProcessingView');
 const globals=contextNames.map(name=>{const row=source.split(/\r?\n/).find(l=>l.startsWith('static thread_local ')&&l.includes(name+'='));if(!row)throw Error('Missing actual processing global '+name);return row;});
 for(const name of ['t_windowsProcessingMeta','t_windowsProcessingStamp'])if(tail.includes('static thread_local ')&&new RegExp('static thread_local [^;]*\\b'+name+'\\b').test(tail))throw Error('Calibration fixture context already declared');
 tail=tail.replace(marker,marker+'\n'+(contextNames.includes('t_windowsProcessingView')?'using WindowsSensorView=ymcc::sensors::view::Snapshot<InputCaptureSensorMeta>;\n':'')+globals.join('\n'));
 const bindings=[];
 if(source.includes('static bool inputCapturePrivateStandardApiBound(')){
  const helper=extract(source,'inputCapturePrivateStandardApiBound'),last=globals.at(-1);
  tail=tail.replace(last,()=>last+'\n'+helper.code);bindings.push((({code,...r})=>r)(helper));
 }
 for(const name of ['inputCaptureSensorMetaSnapshot','inputCaptureLegacySensorFieldsSnapshot','inputCaptureCandidateMatrixReceipt']){const old=extract(tail,name).code,next=extract(source,name);tail=tail.replace(old,()=>next.code);bindings.push((({code,...r})=>r)(next));}
 if(source.includes('static const ymcc::hc_model_motion::Recipe* inputCapturePrivateOemCalibrationRecipe('))prefix+='\n#include "hc_oem_windows_motion_catalog.h"\n';
 return{code:prefix+tail,bindings};
}

export function copyWindowsCalibrationContextHeaders(repo,out,freeze){
 for(const file of ['capabilities/HcMotionCapabilityCatalog.h','capabilities/HcMotionCapabilityCatalog.generated.h','native/hc_generic_motion_catalog.h'])fs.writeFileSync(path.join(out,path.basename(file)),freeze(path.join(repo,file)));
 const pending=['windows_source_view.h','windows_source_eligibility.h','windows_motion_processing.h','hc_oem_windows_motion_catalog.h'],copied=new Set();
 for(const name of ['private_profile_scope.inc','private_profile_cases.inc','windows_processing_cases.inc'])fs.writeFileSync(path.join(out,name),freeze(path.join(repo,'tools/fixtures/hc_reviewed_calibration',name)));
 while(pending.length){const name=pending.pop();if(copied.has(name))continue;copied.add(name);const data=freeze(path.join(repo,'native',name));fs.writeFileSync(path.join(out,name),data);for(const m of data.toString('utf8').matchAll(/#include\s+"([^"/]+\.h)"/g))if(fs.existsSync(path.join(repo,'native',m[1])))pending.push(m[1]);}
}

// Real worker ownership bodies; this only wires isolated fixtures and has no hardware effects.
export function windowsProcessorFixture(source,control=''){
 const names=['inputCaptureWindowsProcessingRecipeMatches','inputCaptureWindowsProcessingMetadataMatches','inputCaptureWindowsProcessingInputMatches','inputCaptureWindowsProcessingFrameCurrent','inputCaptureWindowsProcessingAbortCalibration','inputCaptureWindowsProcessingAdopt','inputCaptureWindowsProcessingProcess','inputCaptureWindowsProcessingNoteFrame','inputCaptureWindowsProcessingReceiptSnapshot'];
 const functions=names.map(n=>extract(source,n)),retire=extract(source,'inputCaptureWindowsProcessingRetire'),overlay=extract(source,'ymccMadgwickResetOrientation');
 const mutations={
  '--drop-publication-proof':['inputCaptureWindowsProcessingProcess','accel.timestamp100ns,view.submission.publication','accel.timestamp100ns,{}'],
  '--skip-native-process':['inputCaptureWindowsProcessingProcess','g_gmProcess(g_gm,gx,gy,gz,ax,ay,az,delta);','/* controlled omission of actual native processing */'],
  '--skip-handoff-reset':['inputCaptureWindowsProcessingAdopt','g_gmResetMotionApi(g_gm);','/* controlled omission of native reset */'],
  '--drop-frame-proof':['inputCaptureWindowsProcessingProcess','owner.hasPending=true;return true;','return true;'],
  '--skip-cpu-retirement':['inputCaptureWindowsProcessingRetire','owner.lease->store(false,std::memory_order_release);','/* controlled omission of lease revocation */']
 };
 if(mutations[control]){const[name,from,to]=mutations[control],binding=name===retire.symbol?retire:functions.find(b=>b.symbol===name);if(!binding||!binding.code.includes(from))throw Error('Actual processor control '+control);binding.code=binding.code.replace(from,to);}
 const declarations='static ymcc::sensors::processing::Owner g_windowsProcessingOwner;\nstatic std::uint64_t g_windowsProcessingEpoch=0;\nstatic std::shared_ptr<const ymcc::sensors::processing::ReceiptCache> g_windowsProcessingReceipt;\nstatic std::chrono::steady_clock::time_point g_gmLastTick{};\nstatic bool g_gmLastTickSet=false;\n'+retire.code;
 const q=source.split(/\r?\n/).find(l=>l.startsWith('static float g_ymMadgQ['));if(!q)throw Error('Actual overlay quaternion');
 return{declarations,functions:functions.map(b=>b.code).join('\n\n'),overlay:q+'\n'+overlay.code,bindings:[retire,overlay,...functions].map(({code,...b})=>b)};
}
