import fs from 'node:fs';
import path from 'node:path';
import {need,sha256,hashFile,owned,writeNew} from '../helper.mjs';

// Finds the complete body after the signature. Default arguments such as {}
// remain inside parentheses and cannot be mistaken for a function body.
export function extractBlock(source,anchor,{semicolon=false}={}){
 const start=source.indexOf(anchor);need(start>=0&&source.indexOf(anchor,start+anchor.length)<0,'MAIN_RETURN_ANCHOR_NOT_UNIQUE:'+anchor);
 let parens=0,state='code',brace=-1;
 function quoted(c,next){
  if(state==='line'){if(c==='\n')state='code';return 1;}
  if(state==='comment'){if(c==='*'&&next==='/'){state='code';return 2;}return 1;}
  if(state==='string'||state==='char'){if(c==='\\')return 2;if(c===(state==='string'?'"':"'"))state='code';return 1;}
  if(c==='/'&&next==='/'){state='line';return 2;}if(c==='/'&&next==='*'){state='comment';return 2;}
  if(c==='"'){state='string';return 1;}if(c==="'"){state='char';return 1;}return 0;
 }
 for(let i=start;i<source.length;i++){const c=source[i],skip=quoted(c,source[i+1]);if(skip){i+=skip-1;continue;}if(c==='(')parens++;if(c===')')parens--;if(c==='{'&&parens===0){brace=i;break;}}
 need(brace>=0,'MAIN_RETURN_BODY_MISSING:'+anchor);state='code';let depth=0,end=-1;
 for(let i=brace;i<source.length;i++){const c=source[i],skip=quoted(c,source[i+1]);if(skip){i+=skip-1;continue;}if(c==='{')depth++;if(c==='}'&&--depth===0){end=i+1;break;}}
 need(end>start,'MAIN_RETURN_UNBALANCED:'+anchor);if(semicolon){need(source[end]===';','MAIN_RETURN_TERMINATOR:'+anchor);end++;}
 const exact=source.slice(start,end);return {exact,sha256:sha256(exact),sourceByteStart:Buffer.byteLength(source.slice(0,start)),sourceByteEnd:Buffer.byteLength(source.slice(0,end)),sourceStartLine:source.slice(0,start).split('\n').length};
}
export function assembleMainReturn(sources,templates,contract){
 need(contract?.schemaVersion===1&&contract.scopeVersion===2&&contract.stage==='main-return'&&contract.definitions?.length===30&&contract.declarations?.length===54,'MAIN_RETURN_CONTRACT_SCOPE');
 for(const row of contract.productSources)need(sources[row.path]&&sha256(sources[row.path])===row.sha256,'MAIN_RETURN_WHOLE_SOURCE_NEEDS_EXPLICIT_MAINTENANCE:'+row.path);
 for(const row of [...contract.templates,contract.memoryPorts])need(templates[row.path]&&sha256(templates[row.path])===row.sha256,'MAIN_RETURN_PORT_SIGNATURE_OR_CASE_NEEDS_EXPLICIT_MAINTENANCE:'+row.path);
 const definitions=contract.definitions.map(spec=>{const text=sources[spec.source]?.toString('utf8');need(typeof text==='string','MAIN_RETURN_UNDECLARED_DEFINITION_SOURCE');const d=extractBlock(text,spec.anchor,spec);need(d.sha256===spec.sha256,'MAIN_RETURN_DEFINITION_NEEDS_EXPLICIT_MAINTENANCE:'+spec.name);return {...spec,...d,sourceSha256:sha256(sources[spec.source])};});
 need(new Set(definitions.map(d=>d.name)).size===30,'MAIN_RETURN_DUPLICATE_DEFINITION');
 const declarations=contract.declarations.map(spec=>{const text=sources[spec.source].toString('utf8'),start=text.indexOf(spec.anchor);need(start>=0&&text.indexOf(spec.anchor,start+spec.anchor.length)<0&&sha256(spec.anchor)===spec.sha256,'MAIN_RETURN_DECLARATION_NEEDS_EXPLICIT_MAINTENANCE:'+spec.name);return {...spec,exact:spec.anchor,sourceSha256:sha256(sources[spec.source]),sourceByteStart:Buffer.byteLength(text.slice(0,start)),sourceByteEnd:Buffer.byteLength(text.slice(0,start+spec.anchor.length)),sourceStartLine:text.slice(0,start).split('\n').length};});
 const get=name=>{const row=definitions.find(d=>d.name===name);need(row,'MAIN_RETURN_DEFINITION_MISSING:'+name);return row.exact;};
 const declaration=name=>{const row=declarations.find(d=>d.name===name);need(row,'MAIN_RETURN_DECLARATION_MISSING:'+name);return row.exact;};
 const header=sources['native/radial_overlay.h'].toString('utf8');const exactLine=anchor=>{const start=header.indexOf(anchor);need(start>=0&&header.indexOf(anchor,start+anchor.length)<0,'MAIN_RETURN_OVERLAY_DECLARATION');return header.slice(start,header.indexOf('\n',start));};
 const overlayDeclarations={aliases:header.slice(header.indexOf('using Json='),header.indexOf('struct Config')),constants:exactLine(' static constexpr UINT expireTimer='),fields:exactLine(' Config config;')};
 for(const[name,text]of Object.entries(overlayDeclarations))need(sha256(text)===contract.overlayDeclarations[name].sha256,'MAIN_RETURN_OVERLAY_DECLARATION_CHANGED:'+name);
 const privateDefs=['owns','send','region','receive','proc'].map(name=>get('radial::Overlay::'+name));
 const publicDefs=['initialize','active','pad','close','shutdown','destructor'].map(name=>get('radial::Overlay::'+name));
 const radial='#pragma once\n#include "MainPlatform.hpp"\nnamespace ymcc::radial {\n'+overlayDeclarations.aliases+'\n'+get('radial::makeInputRegion')+'\n'+get('radial::Config')+'\nclass Overlay {\n friend struct OverlayTestAccess;\n'+overlayDeclarations.constants+'\n'+get('radial::Overlay::State')+'\n'+overlayDeclarations.fields+'\n'+privateDefs.join('\n')+'\npublic:\n'+publicDefs.join('\n')+'\n};\n}\n';
 let types='';for(const name of ['CustomSteamLibraryInputPhase','SerialJobKind','FocusTargetSnapshot','OemProducerId','OemProducerTokens']){if(name==='OemProducerTokens')types+=declaration('kOemProducerCount')+'\n';types+=get(name)+'\n';}
 const globals=declarations.filter(d=>!['kOemProducerCount','g_radialOverlay'].includes(d.name)).map(d=>d.exact).join('\n');
 const mainFns=['radialHasInputOwner','gpAnyShortcutHeld','gpAnyButtonOrTriggerHeld','gpAnyThumbAxisHeld','gamepadUiInputEligible','gamepadEmitUiAction','nativeYmccShortcutSubmit','nativeYmccShortcutEmit','radialProcessUiInput','gamepadEval'].map(get);
 const original='#pragma once\n#include "selected-radial.hpp"\nusing json=nlohmann::json;using Microsoft::WRL::ComPtr;\n'+types+'\n'+globals+'\n'+declaration('g_radialOverlay')+'\n#include "MainEndpoints.hpp"\n'+mainFns.join('\n')+'\n'+get('radialInitialize-lambda-declaration')+'\n';
 need(sha256(original)===contract.reviewedSelectedMainSha256&&sha256(radial)===contract.reviewedSelectedRadialSha256,'MAIN_RETURN_ASSEMBLY_NEEDS_EXPLICIT_MAINTENANCE');
 need(contract.faults?.length===2,'MAIN_RETURN_EXACTLY_TWO_FAULTS');const variants={original};for(const fault of contract.faults){need(original.split(fault.before).length===2&&fault.before!==fault.after,'MAIN_RETURN_FAULT_ANCHOR_NOT_UNIQUE:'+fault.id);variants[fault.id]=original.replace(fault.before,fault.after);}
 need(Object.keys(variants).join(',')==='original,neutral-and-to-or,queued-radial-gate-removed','MAIN_RETURN_FIXED_VARIANTS');
 return {definitions,declarations,overlayDeclarations,radial,variants};
}
export function prepareMainReturn({evidenceRoot,sourceRows,contract}){
 const row=relative=>{const found=sourceRows.find(r=>r.path===relative);need(found&&hashFile(found.saved)===found.sha256,'MAIN_RETURN_CAPTURED_INPUT_MISSING_OR_CHANGED:'+relative);return found;};
 const sources=Object.fromEntries(contract.productSources.map(spec=>[spec.path,fs.readFileSync(row(spec.path).saved)]));
 const prefix='tools/ymcc_sim/global/radial-native-consumer/';
 const templates=Object.fromEntries(contract.templates.map(spec=>[spec.path,fs.readFileSync(row(prefix+'main-return/'+spec.path).saved)]));templates['MemoryPorts.hpp']=fs.readFileSync(row(prefix+'MemoryPorts.hpp').saved);
 const assembly=assembleMainReturn(sources,templates,contract),fixture=owned(evidenceRoot,path.join(evidenceRoot,'fixture'));fs.mkdirSync(fixture);
 const files=[];const put=(name,bytes)=>{const file=owned(evidenceRoot,path.join(fixture,name));fs.writeFileSync(file,bytes,{flag:'wx'});files.push({path:file,sha256:hashFile(file)});};
 for(const[name,bytes]of Object.entries(templates))put(name,bytes);put('selected-radial.hpp',assembly.radial);
 for(const[variant,source]of Object.entries(assembly.variants)){put('selected-main-'+variant+'.hpp',source);put('entry-'+variant+'.cpp','#include "selected-main-'+variant+'.hpp"\n#include "MainCases.hpp"\n');}
 const manifestFile=owned(evidenceRoot,path.join(evidenceRoot,'extraction-manifest.json'));writeNew(manifestFile,{schemaVersion:1,scopeVersion:2,stage:'main-return',productSources:contract.productSources.map(spec=>row(spec.path)),definitions:assembly.definitions,declarations:assembly.declarations,overlayDeclarations:assembly.overlayDeclarations,files,fullMainTranslationUnitCompiled:false,postCreateSeedOnly:true,hostIntentExecuted:false,openCoverage:contract.openCoverage});
 return {fixture,files,manifestFile,manifestSha256:hashFile(manifestFile),jsonInclude:path.dirname(row('deps/json/json.hpp').saved)};
}
