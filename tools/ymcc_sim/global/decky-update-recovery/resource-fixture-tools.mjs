import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
export const RESOURCE_FIXTURE_AUTHORITY_SOURCES=['tools/ymcc_sim/global/decky-update-recovery/resource-fixture-tools.mjs','tools/ymcc_sim/global/decky-update-recovery/resource-fixture-tools.json'];
const digest=b=>createHash('sha256').update(b).digest('hex');
const manifestSha256='5c644a9e95179f89ae64a67e9e2eaa39e32d7d8f1a48939f575b6430d4ccd35b';
export function verifyResourceFixtureTools(manifest=new URL('./resource-fixture-tools.json',import.meta.url)){
 const bytes=fs.readFileSync(manifest);
 if(digest(bytes)!==manifestSha256)throw Error('RESOURCE_FIXTURE_MANIFEST_CHANGED');
 const value=JSON.parse(bytes);
 if(value.schemaVersion!==1||value.files.length!==2)throw Error('RESOURCE_FIXTURE_MANIFEST_SCHEMA');
 for(const row of value.files){
  if(fs.realpathSync(row.path).toLowerCase()!==path.resolve(row.path).toLowerCase()||digest(fs.readFileSync(row.path))!==row.sha256)throw Error('RESOURCE_FIXTURE_TOOL_CHANGED:'+row.path);
 }
 return {...value,manifestSha256};
}
