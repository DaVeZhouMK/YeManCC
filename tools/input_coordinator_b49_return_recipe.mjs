import {B51_RETURN_PROFILE} from './input_coordinator_b51_return_profile.mjs';
// All 21 original B49 environment ports, read from the original producer's
// literal first recipe. This module never imports or executes that producer.
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {parseOriginalB49Recipe,parseOriginalB51Recipe,deriveB51SourceOrder,hashSource} from './input_coordinator_b49_source_structure.mjs';
export const B49_ORIGINAL_PRODUCER_SHA256='4bcb657946c1b03c91243b5491f5eebec7ad461f26eee7f24d081caab19534e6';
export const B49_ORIGINAL_EXPANDED_SHA256='565c0bf86705a0df7a47e0ffce51082a3e2ba0925098530d64d3b32616d5ecd4';
export const B49_DECISION_HOOKS=Object.freeze(['inputHostTargetReturnBeforeCreateLocked','inputHostTargetReturnAcceptPsLocked','inputHostTargetReturnTargetExitedLocked','inputHostTargetReturnBeforeHideLocked','inputHostTargetReturnAfterPhysicalRestoreLocked']);
export function applyB49ReturnRecipe(source,{producerSource}={}){
 if(typeof producerSource!=='string'||hashSource(producerSource)!==B49_ORIGINAL_PRODUCER_SHA256)throw Error('B49_ORIGINAL_PRODUCER_IDENTITY_REQUIRED');
 if(hashSource(source)!==B49_ORIGINAL_EXPANDED_SHA256)throw Error('B49_EXACT_EXPANDED_SOURCE_REQUIRED');
 const recipe=parseOriginalB49Recipe(producerSource),pairs=[],substitutions=[];let inert=source;
 for(const [name,body] of recipe.rows){
  const original=extract(source,name).code,brace=original.indexOf('{');
  if(brace<0)throw Error('B49_RECIPE_BODY_MISSING:'+name);
  const replacement=original.slice(0,brace+1)+'\n'+body+'\n}';
  if(inert.split(original).length!==2)throw Error('B49_NONUNIQUE_RECIPE:'+name);
  inert=inert.replace(original,replacement);pairs.push({original,replacement});
  substitutions.push({symbol:name,beforeSha256:hashSource(original),afterSha256:hashSource(replacement),literalBodySha256:hashSource(body),scope:'Original B49 explicit environment port; original file/pipe and five decision owners retained'});
 }
 for(const hook of B49_DECISION_HOOKS)if(extract(inert,hook).code!==extract(source,hook).code)throw Error('B49_DECISION_HOOK_CHANGED:'+hook);
 function restore(value){for(const {original,replacement} of [...pairs].reverse()){if(value.split(replacement).length!==2)throw Error('B49_NONUNIQUE_RESTORE');value=value.replace(replacement,original);}return value;}
 if(restore(inert)!==source)throw Error('B49_RECIPE_NOT_BYTE_REVERSIBLE');
 return {inert,substitutions,restore,recipeEvidence:{kind:'STATIC_TRANSFORM_NOT_EXECUTION',producerSha256:recipe.producerSha256,originalExpandedSha256:hashSource(source),transformedSha256:hashSource(inert),restoredSha256:hashSource(restore(inert)),count:substitutions.length,decisionHooks:B49_DECISION_HOOKS.map(symbol=>({symbol,sha256:hashSource(extract(source,symbol).code),unchanged:true}))}};
}

// Explicit B51 source-only contract. Never silently widens the B49 profile.
export function applyB51ReturnRecipe(source,{producerSource,fixtureFiles}={}){
 if(typeof source!=='string'||hashSource(source)!==B51_RETURN_PROFILE.expandedSHA)throw Error('B51_EXACT_EXPANDED_SOURCE_REQUIRED');
 const recipe=parseOriginalB51Recipe(producerSource),order=deriveB51SourceOrder(fixtureFiles);
 if(hashSource(JSON.stringify(recipe.rows))!==B51_RETURN_PROFILE.recipeSHA)throw Error('B51_EXACT_RECIPE_REQUIRED');
 const legacy=recipe.rows.filter(row=>B51_RETURN_PROFILE.legacyPortNames.includes(row[0]));
 if(legacy.length!==21||hashSource(JSON.stringify(legacy))!==B51_RETURN_PROFILE.legacyRecipeSHA)throw Error('B51_ORIGINAL21_ALTERED');
 const pairs=[],substitutions=[];let inert=source;
 for(const [symbol,body]of recipe.rows){
  const original=extract(source,symbol).code,brace=original.indexOf('{');if(brace<0)throw Error('B51_RECIPE_BODY_MISSING:'+symbol);
  const replacement=original.slice(0,brace+1)+'\n'+body+'\n}';if(inert.split(original).length!==2)throw Error('B51_NONUNIQUE_RECIPE:'+symbol);
  inert=inert.replace(original,replacement);pairs.push({original,replacement});substitutions.push({symbol,beforeSha256:hashSource(original),afterSha256:hashSource(replacement),literalBodySha256:hashSource(body),originalB49Port:B51_RETURN_PROFILE.legacyPortNames.includes(symbol),scope:'Exact B51 original producer environment port; current five business hooks unchanged'});
 }
 const hooks=B49_DECISION_HOOKS.map(symbol=>{const before=extract(source,symbol).code,after=extract(inert,symbol).code;const expected=B51_RETURN_PROFILE.hooks.find(r=>r.symbol===symbol);if(before!==after||hashSource(before)!==expected?.sha256)throw Error('B51_DECISION_HOOK_CHANGED:'+symbol);return {symbol,sha256:hashSource(before),unchanged:true};});
 function restore(value){for(const {original,replacement}of [...pairs].reverse()){if(value.split(replacement).length!==2)throw Error('B51_NONUNIQUE_RESTORE');value=value.replace(replacement,original);}if(value!==source)throw Error('B51_RESTORE_BYTES');return value;}
 if(restore(inert)!==source)throw Error('B51_RECIPE_NOT_BYTE_REVERSIBLE');
 return {inert,substitutions,restore,sourceOrder:order,recipeEvidence:{kind:'STATIC_B51_TRANSFORM_NOT_EXECUTION',sourceProfile:'B51_FIXED_EXACT',producerSha256:recipe.producerSha256,originalExpandedSha256:hashSource(source),transformedSha256:hashSource(inert),restoredSha256:hashSource(restore(inert)),count:28,originalB49PortCount:21,additionalB51PortCount:7,additionalB51Ports:[...B51_RETURN_PROFILE.additionalPortNames],decisionHooks:hooks,profileBinding:B51_RETURN_PROFILE.fixedBinding,compileApproved:false,executionApproved:false}};
}
