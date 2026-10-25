import assert from 'node:assert/strict';import {createHash} from 'node:crypto';
const sha=text=>createHash('sha256').update(text).digest('hex');
// The real child hashes GetRawText(), so semantic JSON equality is insufficient.
export function assertManagedManifestBinding(text){
 assert(typeof text==='string','MANAGED_MANIFEST_TEXT_REQUIRED');text=text.replace(/^\uFEFF/,'');
 const parsed=JSON.parse(text);assert(parsed&&typeof parsed==='object'&&/^[a-f0-9]{64}$/.test(parsed.inputManifestSha256),'MANAGED_BINDING_HASH_REQUIRED');
 const matches=[...text.matchAll(/"inputBindings"\s*:\s*/g)];assert.equal(matches.length,1,'EXACT_ONE_MANAGED_INPUT_BINDINGS_REQUIRED');
 const start=matches[0].index+matches[0][0].length;assert.equal(text[start],'{','MANAGED_INPUT_BINDINGS_OBJECT_REQUIRED');
 let depth=0,quoted=false,escaped=false,end=-1;for(let i=start;i<text.length;i++){const c=text[i];if(quoted){if(escaped)escaped=false;else if(c==='\\')escaped=true;else if(c==='"')quoted=false;continue;}if(c==='"'){quoted=true;continue;}if(c==='{'||c==='[')depth++;if(c==='}'||c===']'){depth--;if(depth===0){end=i+1;break;}}}
 assert(end>start,'MANAGED_BINDING_VALUE_INCOMPLETE');const raw=text.slice(start,end),actual=sha(raw);assert.equal(actual,parsed.inputManifestSha256,'MANAGED_INPUT_BINDING_RAW_HASH_MISMATCH');assert.deepEqual(JSON.parse(raw),parsed.inputBindings,'MANAGED_BINDING_VALUE_AMBIGUOUS');return {inputManifestSha256:actual,rawBytes:Buffer.byteLength(raw)};
}
export function serializeManagedManifest(value){assert.equal(sha(JSON.stringify(value.inputBindings)),value.inputManifestSha256,'MANAGED_COMPACT_BINDING_HASH_MISMATCH');const text=JSON.stringify(value)+'\n';assertManagedManifestBinding(text);return text;}
