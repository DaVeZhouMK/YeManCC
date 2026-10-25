import {extract} from '../legion_motion_consumer/extract_main.mjs';
// Update only the current candidate namespace of the retained real-DLL fixture.
// Historical comparison namespaces remain byte-for-byte retained.
export const changedNames=['inputCaptureResolveHcMotionSelector','inputCaptureMotionSelectorBound','inputCaptureCandidateMatrixReceipt','inputCaptureMotionCapabilityReceipt','inputCaptureHcWindowsLegacyNeeded','inputCaptureExtendedCalibrationRecipe','inputCaptureWindowsSelectionEvidence','inputCaptureWindowsProcessingRecipeMatches','inputCaptureWindowsProcessingAdopt','inputCaptureWindowsSelectionPublishContext'];
export const addedNames=['inputCaptureGenericHcMotionRecipe','inputCaptureResolveGenericHcMotionSelector','inputCaptureGenericHcMotionSelectorBound','inputCaptureMotionDeviceClass'];
export function rebindGenericFixture(base,source,bind){
 const at=base.indexOf('namespace candidate {');if(at<0)throw Error('Current candidate missing');let prior=base.slice(0,at),tail=base.slice(at);
 const supported=source.includes('static const ymcc::hc_model_motion::Recipe* inputCaptureGenericHcMotionRecipe(');
 if(!supported)return base;
 for(const name of changedNames){if(name==='inputCaptureWindowsSelectionPublishContext')continue;const from=extract(tail,name),to=extract(source,name);if(tail.split(from.code).length!==2)throw Error('Nonunique candidate '+name);tail=tail.replace(from.code,()=>to.code);bind({...to,code:undefined})}
 const helpers=addedNames.map(name=>{const code=extract(source,name);bind({...code,code:undefined});return code.code}).join('\n\n');const before=extract(tail,'inputCaptureDefaultHcMotionIdentity').code;tail=tail.replace(before,helpers+'\n\n'+before);
 return prior+'\n#include "hc_generic_motion_catalog.h"\n'+tail;
}
