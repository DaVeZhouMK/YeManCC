// Exact R123 baseline binding; no product code substitutions.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {extract,sha} from '../owned_return_settlement_r120/source-binding.mjs';
export {sha};
export const load=file=>{const bytes=fs.readFileSync(file);return {file,bytes,text:bytes.toString('utf8'),sha256:sha(bytes)};};
export function bind(source){
 const t=source.text,ranges=[];
 const owner=extract(t,'struct','MsiDinputState',source.file,ranges);
 const ownerBinding=ranges[0];
 const ownerStart=t.indexOf(owner);assert(ownerStart>=0);
 const record=(name,a,b)=>{assert(a>=ownerStart&&b<=ownerStart+owner.length&&b>a,name+' outside MsiDinputState');const code=t.slice(a,b);ranges.push({file:source.file,kind:'exact-state-declarations',symbol:name,startLine:t.slice(0,a).split('\n').length,endLine:t.slice(0,b).split('\n').length,startUtf8Byte:Buffer.byteLength(t.slice(0,a)),endUtf8ByteExclusive:Buffer.byteLength(t.slice(0,b)),sha256:sha(code)});return code;};
 const bindMatches=[...owner.matchAll(/^    unsigned bindCount = 0;[^\r\n]*/gm)];assert.equal(bindMatches.length,1);
 const bindAt=ownerStart+bindMatches[0].index;
 const bindField=record('MsiDinputState.bindCount',bindAt,bindAt+bindMatches[0][0].length);
 const a=t.indexOf('    std::atomic<uint32_t> canonicalSeq{0};',ownerStart),end='    std::atomic<bool> canonicalValid{false};',b=t.indexOf(end,a)+end.length;
 assert(a>=ownerStart&&b>a);
 const fields=record('MsiDinputState canonical field block (including original ifdef)',a,b);
 const allCanonical=[...owner.matchAll(/\bcanonical[A-Za-z0-9_]+\b/g)].map(m=>m[0]);
 const selectedCanonical=[...fields.matchAll(/\bcanonical[A-Za-z0-9_]+\b/g)].map(m=>m[0]);
 assert.deepEqual([...new Set(allCanonical)].sort(),[...new Set(selectedCanonical)].sort(),'A canonical field exists outside the frozen field block');
 const functions=['msiDinputPublishCanonicalFrame','msiDinputInvalidateCanonicalFrame','msiDinputCanonicalSnapshot'].map(name=>extract(t,'function',name,source.file,ranges));
 return {owner,ownerBinding,fields:bindField+'\n'+fields,functions:functions.join('\n\n'),ranges};
}
export function freezeBaseline(repo,evidenceRoot){
 const source=load(path.join(repo,'native/main.cpp')),bound=bind(source);
 assert(!fs.existsSync(path.join(evidenceRoot,'baseline')),'Never replace a frozen baseline');
 const dir=path.join(evidenceRoot,'baseline');fs.mkdirSync(dir,{recursive:true});
 fs.writeFileSync(path.join(dir,'actual-fields.inc'),bound.fields);
 fs.writeFileSync(path.join(dir,'actual-functions.inc'),bound.functions);
 fs.writeFileSync(path.join(dir,'MsiDinputState.actual.txt'),bound.owner);
 const dependencyNames=['native/msi_virtual_report_diagnostic.h','deps/json/json.hpp'];
 const dependencies=dependencyNames.map(name=>load(path.join(repo,name)));
 for(const d of dependencies)fs.writeFileSync(path.join(dir,path.basename(d.file)),d.bytes);
 const header=dependencies[0],headerRanges=[];extract(header.text,'struct','SourceStamp',header.file,headerRanges);
 const metadata={schema:1,frozenAt:new Date().toISOString(),baselineSourceSHA:source.sha256,sourceFile:source.file,ranges:bound.ranges,sourceStamp:headerRanges[0],dependencies:dependencies.map(d=>({file:d.file,sha256:d.sha256})),frozenInputs:['actual-fields.inc','actual-functions.inc','MsiDinputState.actual.txt',...dependencies.map(d=>path.basename(d.file))].map(name=>({name,sha256:sha(fs.readFileSync(path.join(dir,name)))})),sourceSubstitutions:[],ownerLayoutCompiled:false,scope:'Exact publish, invalidate, snapshot and required original state declarations; full owner text retained for hash attribution only. No pump/permit/fallback/BUS/Host path.'};
 fs.writeFileSync(path.join(evidenceRoot,'source-binding.json'),JSON.stringify(metadata,null,2)+'\n');return metadata;
}