import assert from 'node:assert/strict';
import test from 'node:test';
import path from 'node:path';
import {getExtraAdapters} from '../extra-adapters.mjs';
import {assertReviewedRegistryRow,reviewedRegistryLists} from './assert-lists.mjs';

const registry=getExtraAdapters();
for(const id of Object.keys(reviewedRegistryLists.consumers)){
 const matches=registry.filter(row=>row.id===id);
 test(`reviewed registry maintenance: ${id} accepts the real complete registration`,()=>{
  assert.equal(matches.length,1);
  assertReviewedRegistryRow(matches[0]);
 });
 const mutations=[
  ['source missing one',row=>row.evidenceInputs.pop(),/MAINTENANCE_SOURCES_COUNT/],
  ['source extra one',row=>row.evidenceInputs.push('tools/unreviewed-source.mjs'),/MAINTENANCE_SOURCES_COUNT/],
  ['source duplicate at same count',row=>row.evidenceInputs[0]=row.evidenceInputs[1],/MAINTENANCE_SOURCES_UNIQUE/],
  ['source from another root at same count',row=>row.evidenceInputs[0]=path.resolve('foreign-source-root',row.evidenceInputs[0]),/MAINTENANCE_SOURCES_EXACT_LIST/],
  ['tool missing one',row=>row.artifactInputs.pop(),/MAINTENANCE_TOOLS_COUNT/],
  ['tool extra one',row=>row.artifactInputs.push(path.resolve('foreign-tool-extra.dll')),/MAINTENANCE_TOOLS_COUNT/],
  ['tool duplicate at same count',row=>row.artifactInputs[0]=row.artifactInputs[1],/MAINTENANCE_TOOLS_UNIQUE/],
  ['tool from another root at same count',row=>row.artifactInputs[0]=path.resolve('foreign-tool-root',path.basename(row.artifactInputs[0])),/MAINTENANCE_TOOLS_EXACT_LIST/],
 ];
 for(const [name,change,expected]of mutations)test(`reviewed registry maintenance: ${id} rejects ${name}`,()=>{
  assert.equal(matches.length,1);const row=structuredClone(matches[0]);change(row);
  assert.throws(()=>assertReviewedRegistryRow(row),expected);
 });
}
