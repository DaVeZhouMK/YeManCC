// Exact original28 environment leaves for an explicitly admitted current source.
// A reversible source transformation never grants compile or runtime authority.
import fs from 'node:fs';import assert from 'node:assert/strict';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {parseOriginalB51Recipe,hashSource} from './input_coordinator_b49_source_structure.mjs';
import {B51_RETURN_PROFILE} from './input_coordinator_b51_return_profile.mjs';
import {requireCurrentCompileBatch} from './ymcc_sim/global/controller-coordinator-pipe/r3-current-compile-batch.mjs';
const expectations=JSON.parse(fs.readFileSync(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-current-expectations.json',import.meta.url),'utf8'));
export function transformCurrentEnvironmentRecipe(source,{producerSource,expectedExpandedSha256}={}){
 if(typeof source!=='string'||typeof expectedExpandedSha256!=='string'||!/^[a-f0-9]{64}$/.test(expectedExpandedSha256)||hashSource(source)!==expectedExpandedSha256)throw Error('CURRENT_EXPLICIT_EXPANDED_SOURCE_REQUIRED');
 if(typeof producerSource!=='string'||hashSource(producerSource)!==expectations.originalSourceRecipeSha256)throw Error('CURRENT_ORIGINAL_PRODUCER_IDENTITY_REQUIRED');
 const recipe=parseOriginalB51Recipe(producerSource);assert.equal(recipe.rows.length,28);assert.equal(hashSource(JSON.stringify(recipe.rows)),B51_RETURN_PROFILE.recipeSHA,'Original28 recipe bodies changed');
 let inert=source;const substitutions=[],pairs=[];
 for(const [symbol,body]of recipe.rows){const original=extract(source,symbol).code,brace=original.indexOf('{');assert(brace>=0);const replacement=original.slice(0,brace+1)+'\n'+body+'\n}';if(inert.split(original).length!==2)throw Error('CURRENT_NONUNIQUE_RECIPE:'+symbol);inert=inert.replace(original,()=>replacement);pairs.push({original,replacement});substitutions.push({symbol,originalSha256:hashSource(original),compiledSha256:hashSource(replacement),literalBodySha256:hashSource(body),environmentLeaf:true,expectedNativeExecution:false});}
 const core=expectations.nativeCore.filter(row=>row.source==='expanded-native-main').map(row=>{const original=extract(source,row.symbol).code,current=extract(inert,row.symbol).code;assert.equal(hashSource(original),row.sha256,'Reviewed original core changed:'+row.symbol);assert.equal(current,original,'Environment adapter changed core:'+row.symbol);return {symbol:row.symbol,sourceSha256:row.sha256,transformedSha256:hashSource(current),unchanged:true};});assert.equal(core.length,60);
 const cold=expectations.nativeStructCore.find(row=>row.symbol==='ColdEnvironment'),coldOriginal=extract(source,'ColdEnvironment','struct').code,coldCurrent=extract(inert,'ColdEnvironment','struct').code;assert.equal(hashSource(coldOriginal),cold.sha256);assert.equal(coldCurrent,coldOriginal);
 function restore(value){for(const pair of [...pairs].reverse()){if(value.split(pair.replacement).length!==2)throw Error('CURRENT_NONUNIQUE_RESTORE');value=value.replace(pair.replacement,()=>pair.original);}if(value!==source)throw Error('CURRENT_RESTORE_BYTES');return value;}
 assert.equal(restore(inert),source);return {inert,substitutions,core,restore,coldEnvironment:{sourceSha256:hashSource(coldOriginal),transformedSha256:hashSource(coldCurrent),unchanged:true},scope:{kind:'STATIC_CURRENT_FIRST_ENVIRONMENT_RECIPE_ONLY',recipeProducerSha256:expectations.originalSourceRecipeSha256,exactExpandedSourceSha256:expectedExpandedSha256,compilerExecuted:false,nativeExecuted:false,fullOriginalProducerTransformStillRequired:true,fullOriginalConsumerAcceptance:false,publicSingle60Accepted:false}};
}
export function applyCurrentEnvironmentRecipe(source,{producerSource,batchAuthority}={}){const layout=requireCurrentCompileBatch(batchAuthority);return transformCurrentEnvironmentRecipe(source,{producerSource,expectedExpandedSha256:layout.expandedSourceSha256});}
