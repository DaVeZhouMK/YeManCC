import fs from 'node:fs';
import path from 'node:path';
import {need,sha256,hashFile,writeNew,owned} from './helper.mjs';

export function extractBlock(source,anchor,{semicolon=false}={}){
 const start=source.indexOf(anchor);need(start>=0&&source.indexOf(anchor,start+anchor.length)<0,'RADIAL_EXTRACTION_ANCHOR_NOT_UNIQUE:'+anchor);
 const brace=source.indexOf('{',start);need(brace>=0,'RADIAL_EXTRACTION_OPEN_BRACE');let depth=0,state='code',end=-1;
 for(let i=brace;i<source.length;i++){
  const c=source[i],next=source[i+1];
  if(state==='line'){if(c==='\n')state='code';continue;}
  if(state==='comment'){if(c==='*'&&next==='/'){state='code';i++;}continue;}
  if(state==='string'||state==='char'){if(c==='\\'){i++;continue;}if(c===(state==='string'?'"':"'"))state='code';continue;}
  if(c==='/'&&next==='/'){state='line';i++;continue;}if(c==='/'&&next==='*'){state='comment';i++;continue;}
  if(c==='"'){state='string';continue;}if(c==="'"){state='char';continue;}
  if(c==='{')depth++;if(c==='}'&&--depth===0){end=i+1;break;}
 }
 need(end>start,'RADIAL_EXTRACTION_UNBALANCED:'+anchor);
 if(semicolon){need(source[end]===';','RADIAL_EXTRACTION_TYPE_TERMINATOR');end++;}
 const exact=source.slice(start,end);return {exact,sourceByteStart:Buffer.byteLength(source.slice(0,start)),sourceByteEnd:Buffer.byteLength(source.slice(0,end)),sourceStartLine:source.slice(0,start).split('\n').length,exactSha256:sha256(exact)};
}
export function verifyTemplates(templates,contract){
 need(contract.templates?.length===2,'RADIAL_TEMPLATE_CONTRACT');
 for(const row of contract.templates)need(templates[row.path]&&sha256(templates[row.path])===row.sha256,'RADIAL_PORT_OR_CASE_TEMPLATE_NEEDS_EXPLICIT_MAINTENANCE:'+row.path);return true;
}
export function assembleSources(sources,contract){
 need(contract.schemaVersion===1&&contract.caseId==='controller.radial-native-consumer'&&contract.definitions?.length===13,'RADIAL_EXTRACTION_CONTRACT');
 need(sha256(sources['deps/json/json.hpp'])===contract.json.sha256,'RADIAL_JSON_SOURCE_NEEDS_EXPLICIT_MAINTENANCE');
 const text=sources['native/radial_overlay.h'].toString('utf8'),definitions=contract.definitions.map(spec=>{
  need(sources[spec.source],'RADIAL_UNDECLARED_DEFINITION_SOURCE');const definition=extractBlock(sources[spec.source].toString('utf8'),spec.anchor,spec);
  need(definition.exactSha256===spec.exactSha256,'RADIAL_DEFINITION_NEEDS_EXPLICIT_MAINTENANCE:'+spec.name);return {...spec,...definition,sourceSha256:sha256(sources[spec.source])};
 });
 need(new Set(definitions.map(d=>d.name)).size===13,'RADIAL_DUPLICATE_DEFINITION');
 const namespace=text.indexOf('namespace ymcc::radial {'),aliasStart=text.indexOf('using Json=',namespace),aliasEnd=text.indexOf('struct Config',aliasStart),constantStart=text.indexOf(' static constexpr UINT expireTimer='),fieldStart=text.indexOf(' Config config;');
 need(namespace>=0&&aliasStart>=0&&aliasEnd>aliasStart&&constantStart>=0&&fieldStart>=0,'RADIAL_DECLARATION_ANCHOR_CHANGED');
 const aliases=text.slice(aliasStart,aliasEnd),constants=text.slice(constantStart,text.indexOf('\n',constantStart)),fields=text.slice(fieldStart,text.indexOf('\n',fieldStart));
 for(const [name,value]of Object.entries({aliases,constants,fields}))need(sha256(value)===contract.declarations[name].sha256,'RADIAL_DECLARATION_NEEDS_EXPLICIT_MAINTENANCE:'+name);
 const get=name=>{const row=definitions.find(d=>d.name===name);need(row,'RADIAL_DEFINITION_MISSING:'+name);return row.exact;};
 const privateMethods=['owns','send','region','receive','proc'].map(name=>get('Overlay::'+name));
 const publicMethods=['initialize','active','close','shutdown','destructor'].map(name=>get('Overlay::'+name));
 const original='#pragma once\n#include "MemoryPorts.hpp"\nnamespace ymcc::radial {\n'+aliases+'\n'+get('makeInputRegion')+'\n'+get('Config')+'\nclass Overlay {\n friend struct OverlayTestAccess;\n'+constants+'\n'+get('Overlay::State')+'\n'+fields+'\n'+privateMethods.join('\n')+'\npublic:\n'+publicMethods.join('\n')+'\n};\n}\n';
 need(sha256(original)===contract.reviewedAssembledOriginalSha256,'RADIAL_ASSEMBLY_NEEDS_EXPLICIT_MAINTENANCE');
 need(contract.faults?.length===2,'RADIAL_EXACTLY_TWO_SOURCE_FAULTS');const variants={original};
 for(const fault of contract.faults){need(original.split(fault.before).length===2&&fault.before!==fault.after,'RADIAL_FAULT_ANCHOR_NOT_UNIQUE:'+fault.id);variants[fault.id]=original.replace(fault.before,fault.after);}
 need(Object.keys(variants).join(',')==='original,owner-or-to-and,generation-gate-removed','RADIAL_FIXED_VARIANTS_CHANGED');
 return {definitions,declarations:{aliases,constants,fields},variants,assemblyScaffolding:contract.assemblyScaffolding,exclusions:contract.exclusions};
}
export function prepareFixture({evidenceRoot,sourceRows,contract}){
 const row=relative=>{const result=sourceRows.find(r=>r.path===relative);need(result,'RADIAL_CAPTURED_SOURCE_MISSING:'+relative);need(hashFile(result.saved)===result.sha256,'RADIAL_CAPTURED_SOURCE_CHANGED');return result;};
 const sources=Object.fromEntries(contract.productSources.map(relative=>[relative,fs.readFileSync(row(relative).saved)]));
 const templates=Object.fromEntries(contract.templates.map(spec=>[spec.path,fs.readFileSync(row('tools/ymcc_sim/global/radial-native-consumer/'+spec.path).saved)]));
 verifyTemplates(templates,contract);const extraction=assembleSources(sources,contract),fixture=owned(evidenceRoot,path.join(evidenceRoot,'fixture'));fs.mkdirSync(fixture);
 const files=[];const put=(name,bytes)=>{const file=owned(evidenceRoot,path.join(fixture,name));fs.writeFileSync(file,bytes,{flag:'wx'});files.push({path:file,sha256:hashFile(file)});return file;};
 for(const [name,bytes]of Object.entries(templates))put(name,bytes);
 for(const [variant,source]of Object.entries(extraction.variants)){put('selected-'+variant+'.hpp',source);put('entry-'+variant+'.cpp','#include "selected-'+variant+'.hpp"\n#include "ConsumerCases.hpp"\n');}
 const manifest={schemaVersion:1,...extraction,definitions:extraction.definitions.map(d=>({...d,snapshot:row(d.source).saved})),productSources:contract.productSources.map(relative=>row(relative)),files,postCreateSeedOnly:true,fullOverlayClassCompiled:false,wholeMainOrDiscoveryContextDeclared:false};
 const manifestFile=owned(evidenceRoot,path.join(evidenceRoot,'extraction-manifest.json'));writeNew(manifestFile,manifest);
 return {fixture,files,manifestFile,manifestSha256:hashFile(manifestFile),jsonInclude:path.dirname(row('deps/json/json.hpp').saved)};
}
