import {B51_RETURN_PROFILE} from './input_coordinator_b51_return_profile.mjs';
// Static source projection only. Never import/execute the producer or C++ code.
import {createHash} from 'node:crypto';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
export const hashSource=value=>createHash('sha256').update(value).digest('hex');
const need=(v,m)=>{if(!v)throw Error('B49_SOURCE_'+m);};
export function uniqueRange(text,needle){const start=text.indexOf(needle);need(start>=0&&text.indexOf(needle,start+1)<0,'UNIQUE_ANCHOR:'+needle);return start;}
function skip(text,i){for(;;){while(/\s/.test(text[i]??'')&&i<text.length)i++;if(text.startsWith('//',i)){const n=text.indexOf('\n',i);i=n<0?text.length:n+1;}else if(text.startsWith('/*',i)){const n=text.indexOf('*/',i+2);need(n>=0,'COMMENT');i=n+2;}else return i;}}
function quoted(text,i){const quote=text[i];need(quote==='"'||quote==="'",'STRING');let value='';const start=i++;while(i<text.length){const ch=text[i++];if(ch===quote)return {value,end:i,start};if(ch!=='\\'){value+=ch;continue;}const escaped=text[i++];const basic={n:'\n',r:'\r',t:'\t',b:'\b',f:'\f',v:'\v','0':'\0'};if(escaped==='x'||escaped==='u'){const n=escaped==='x'?2:4,digits=text.slice(i,i+n);need(new RegExp('^[0-9a-fA-F]{'+n+'}$').test(digits),'STRING_ESCAPE');value+=String.fromCharCode(parseInt(digits,16));i+=n;}else if(escaped in basic)value+=basic[escaped];else if(escaped==='\\'||escaped==='"'||escaped==="'")value+=escaped;else throw Error('B49_SOURCE_UNSUPPORTED_ESCAPE:'+escaped);}throw Error('B49_SOURCE_UNTERMINATED_STRING');}
export function matching(text,start){const open=text[start],close=({'(':')','[':']','{':'}'})[open];need(close,'DELIMITER');let depth=0;for(let i=start;i<text.length;){const j=skip(text,i);if(j!==i){i=j;continue;}const c=text[i];if(c==='"'||c==="'"){i=quoted(text,i).end;continue;}if(c===open)depth++;if(c===close&&--depth===0)return i;i++;}throw Error('B49_SOURCE_UNBALANCED');}
export function parseOriginalB49Recipe(producer){const start=uniqueRange(producer,'for(const[name,body]of [');const arrayStart=producer.indexOf('[',start+'for(const[name,body]of '.length),arrayEnd=matching(producer,arrayStart);let i=arrayStart+1;const rows=[];while((i=skip(producer,i))<arrayEnd){need(producer[i++]==='[','RECIPE_ROW');i=skip(producer,i);const name=quoted(producer,i);i=skip(producer,name.end);need(producer[i++]===',','RECIPE_COMMA');i=skip(producer,i);const body=quoted(producer,i);i=skip(producer,body.end);need(producer[i++]===']','RECIPE_ROW_END');rows.push([name.value,body.value]);i=skip(producer,i);if(producer[i]===',')i++;else need(i===arrayEnd,'RECIPE_SEPARATOR');}
 const loopBody=producer.indexOf('{',arrayEnd),end=matching(producer,loopBody)+1;
 need(rows.length===21&&new Set(rows.map(r=>r[0])).size===21,'RECIPE_21_UNIQUE');
 return {rows,start,end,loop:producer.slice(start,end),producerSha256:hashSource(producer)};
}
function tokens(text){const out=[];let i=0;while((i=skip(text,i))<text.length){if(text[i]==='"'||text[i]==="'"){const q=quoted(text,i);out.push({kind:'literal',value:q.value});i=q.end;continue;}const m=/^(?:[A-Za-z_][\w]*(?:::[A-Za-z_][\w]*)*|\d+|==|!=|&&|\|\||[+?:(),.{}])/.exec(text.slice(i));need(m,'TOKEN:'+text.slice(i,i+25));out.push({kind:/^\d+$/.test(m[0])?'number':'syntax',value:m[0]});i+=m[0].length;}return out;}
// Evaluate only naming expressions. The supported operators cannot call source code.
export function projectNameExpression(text,env={}){const t=tokens(text);let i=0;const at=v=>t[i]?.value===v,consume=v=>{need(at(v),'EXPRESSION_EXPECTED:'+v);i++;};function primary(){const token=t[i++];need(token,'EXPRESSION_END');let value;if(token.kind==='literal')value=token.value;else if(token.kind==='number')value=Number(token.value);else if(token.value==='('){value=conditional();consume(')');}else if(['std::string','std::to_string','DWORD'].includes(token.value)){consume('(');value=conditional();consume(')');value=token.value==='DWORD'?Number(value):String(value);}else if(['true','false'].includes(token.value))value=token.value==='true';else {need(Object.hasOwn(env,token.value),'UNKNOWN_NAME:'+token.value);value=env[token.value];}
 while(at('.')){i++;consume('rfind');consume('(');const prefix=conditional();consume(',');const pos=conditional();consume(')');need(typeof value==='string'&&pos===0,'RFIND_SCOPE');value=value.startsWith(prefix)?0:-1;}return value;}
 function add(){let v=primary();while(at('+')){i++;v+=primary();}return v;}
 function equality(){let v=add();while(at('==')||at('!=')){const op=t[i++].value,b=add();v=op==='=='?v===b:v!==b;}return v;}
 function conditional(){const condition=equality();if(!at('?'))return condition;i++;const yes=conditional();consume(':');const no=conditional();return condition?yes:no;}
 const result=conditional();need(i===t.length,'EXPRESSION_TRAILING');return result;}
function splitArguments(text){let start=0;const result=[];for(let i=0;i<text.length;i++){if(text[i]==='"'||text[i]==="'"){i=quoted(text,i).end-1;continue;}if('({['.includes(text[i])){i=matching(text,i);continue;}if(text[i]===','){result.push(text.slice(start,i).trim());start=i+1;}}if(text.slice(start).trim())result.push(text.slice(start).trim());return result;}
function withoutComments(s){return s.replace(/\/\*[\s\S]*?\*\/|\/\/[^\r\n]*/g,' ');}
function nameExpression(code){const start=uniqueRange(code,'cases.push_back('),begin=code.indexOf('{"name",',start)+8;need(begin>=8,'NAME_FIELD');let i=begin;for(;i<code.length;i++){if(code[i]==='"'||code[i]==="'"){i=quoted(code,i).end-1;continue;}if('(['.includes(code[i])){i=matching(code,i);continue;}if(code[i]==='}')return code.slice(begin,i);}throw Error('B49_SOURCE_NAME_FIELD_END');}
// Deliberately supports only range-for and calls in the actual driver suffix.
// The original business method bodies are never interpreted or approximated.
function projectDriver(text,env,call){let i=0;function statement(scope){i=skip(text,i);if(text[i]==='}')return;if(text.startsWith('for',i)&&/\W/.test(text[i+3])){const head=text.indexOf('(',i),end=matching(text,head),decl=text.slice(head+1,end);const m=/^\s*const\s+(?:std::string|DWORD|auto)\s+(\w+)\s*:\s*\{([\s\S]*)\}\s*$/.exec(decl);need(m,'RANGE_FOR:'+decl);const values=splitArguments(m[2]).map(v=>{const d=/^DWORD\{(\d+)\}$/.exec(v);return d?Number(d[1]):projectNameExpression(v,scope);});i=skip(text,end+1);const bodyStart=i;let bodyEnd;if(text[i]==='{')bodyEnd=matching(text,i)+1;else{let cursor=i;while(text[cursor]!==';'){if(text[cursor]==='(')cursor=matching(text,cursor);cursor++;need(cursor<text.length,'LOOP_BODY');}bodyEnd=cursor+1;}
 const body=text.slice(bodyStart,bodyEnd);for(const value of values)projectDriver(body,{...scope,[m[1]]:value},call);i=bodyEnd;return;}
 if(text[i]==='{'){const end=matching(text,i);projectDriver(text.slice(i+1,end),scope,call);i=end+1;return;}
 const m=/^(\w+)\s*\(/.exec(text.slice(i));need(m,'DRIVER_STATEMENT:'+text.slice(i,i+60));const start=i,open=i+m[0].length-1,end=matching(text,open),args=splitArguments(text.slice(open+1,end)).map(a=>projectNameExpression(a,scope));i=skip(text,end+1);need(text[i++]===';','CALL_END');call(m[1],args,{driverText:text.slice(start,i),bindings:{...scope}});}
 while((i=skip(text,i))<text.length)statement(env);
}
function deriveSourceOrder(files,b51=false){const required=['fixture.cpp','pipe_cases.inc','target_return_cases.inc','prepare_ack_cases.inc','off_lifecycle_cases.inc','ps_return_cases.inc','ps_wake_cases.inc'];for(const f of required)need(typeof files[f]==='string','MISSING:'+f);
 const definitions=new Map(),sourceInputs=required.map(path=>({path,sha256:hashSource(files[path])}));
 function fn(file,name){const key=file+':'+name;if(!definitions.has(key))definitions.set(key,extract(files[file],name).code);return definitions.get(key);}
 const rows=[];let region='original95';
 const emit=(file,symbol,env={},site={})=>{const code=fn(file,symbol),expression=nameExpression(code),name=projectNameExpression(expression,env);need(typeof name==='string'&&name.length>0,'NAME_NOT_STRING');rows.push({name,region,sourcePath:'tools/fixtures/input_coordinator_lifecycle/'+file,sourceLine:files[file].slice(0,files[file].indexOf(code)+code.indexOf('cases.push_back(')).split('\n').length,symbol,namingExpression:expression,bindings:env,callSite:site});};
 const basic={};for(const kind of ['Present','Unknown','Absent','Foreign'])basic['PsWakeProbe::'+kind]='PsWakeProbe::'+kind;
 function call(name,args,site){switch(name){case 'fullPipeVerifiedSourceLineageCase':emit('pipe_cases.inc',name,{},site);break;case 'fullPipePsProtectedCycleCase':emit('pipe_cases.inc',name,{variant:args[0]??'central',family:args[1]??'ROG'},site);break;case 'fullPipeInitialBudgetCase':emit('pipe_cases.inc',name,{family:args[0],expires:args[1],expiredBeforeEntry:args[2]??false},site);break;case 'fullPipeQueuedInitialAuthorityCase':emit('pipe_cases.inc',name,{reason:args[0],replaceRequest:args[1]},site);break;case 'psEndpointProbeCases':emit('ps_wake_cases.inc',name,{},site);break;case 'fullPipePrepareAckLostCase':emit('prepare_ack_cases.inc',name,{family:args[0],offWhileHeld:args[1]??false,unknownReturn:args[2]??false,exitSweep:args[3]??false},site);break;case 'fullPipePsReturnEvidenceCases':{const code=fn('ps_return_cases.inc',name);projectDriver(code.slice(code.indexOf('{')+1,-1),basic,call);break;}case 'fullPipePsReturnEvidenceCase':{const expected=args[1],verdict=expected.endsWith('Present')?'present':expected.endsWith('Unknown')?'unknown':expected.endsWith('Foreign')?'foreign-peer':'absent';if(b51)need(args.length===2||args.length===3,'B51_PS_CASE_ARITY');emit('ps_return_cases.inc',name,b51?{connected:args[0],verdict,blockedStore:args[2]??false}:{connected:args[0],verdict},site);break;}case 'fullPipeOffLifecycleCases':{const code=fn('off_lifecycle_cases.inc',name);projectDriver(code.slice(code.indexOf('{')+1,-1),basic,call);break;}case 'fullPipeOffLifecycleCase':emit('off_lifecycle_cases.inc',name,{variant:args[0]},site);break;default:throw Error('B49_SOURCE_UNSUPPORTED_DRIVER_CALL:'+name);}}
 const driver=fn('pipe_cases.inc','fullPipeCases');
 const tableStart=uniqueRange(driver,'scenarios[]='),open=driver.indexOf('{',tableStart),close=matching(driver,open);
 const table=splitArguments(driver.slice(open+1,close));
 const initialName=nameExpression(driver);
 for(const row of table){need(row[0]==='{'&&row.at(-1)==='}','SCENARIO_ROW');const [fault,family,persona='steamdeck']=splitArguments(row.slice(1,-1)).map(v=>projectNameExpression(v));const name=projectNameExpression(initialName,{fault,family,edge:persona==='dualsense-edge'});rows.push({name,region,sourcePath:'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc',sourceLine:files['pipe_cases.inc'].slice(0,files['pipe_cases.inc'].indexOf(driver)+driver.indexOf('scenarios[]=')).split('\n').length,symbol:'fullPipeCases/scenarios',namingExpression:initialName,bindings:{fault,family,persona},callSite:{driverText:row}});}
 const suffix=uniqueRange(driver,'fullPipeVerifiedSourceLineageCase();');projectDriver(driver.slice(suffix,-1),basic,call);
 const legacy95Boundary=rows.length;need(rows.at(-1).name==='OFF-LIFECYCLE/MSI-return-unproven','LEGACY_WITNESS_LAST');
 const entry=files['fixture.cpp'];need(entry.includes('fullPipeCases();fixtureTargetReturnFullPipeCases();'),'FULL_PIPE_ENTRY_ORDER');
 region='target-return-tail';const tail=fn('target_return_cases.inc','fixtureTargetReturnFullPipeCases');
 const history=/for\(const bool retainHistory:\{([^}]+)\}\)/.exec(tail);need(history,'TAIL_HISTORY_LOOP');const choose=/fixtureTargetReturn::preparePcCase\((retainHistory\?[^;]+)\);/.exec(tail);need(choose,'TAIL_HISTORY_NAME');
 const pcCode=fn('target_return_cases.inc','preparePcCase');const pcExpr=nameExpression(pcCode);
 for(const value of splitArguments(history[1]).map(x=>projectNameExpression(x))){const name=projectNameExpression(choose[1],{retainHistory:value});emit('target_return_cases.inc','preparePcCase',{name},{driverText:choose[0],bindings:{retainHistory:value}});}
 const variants=/for\(const std::string variant:\{([^}]+)\}\)/.exec(tail);need(variants,'TAIL_VARIANTS');for(const name of splitArguments(variants[1]).map(x=>projectNameExpression(x)))emit('target_return_cases.inc','preparePcCase',{name},{driverText:'fixtureTargetReturn::preparePcCase(variant)',bindings:{variant:name}});
 const ack=tail.match(/fullPipePrepareAckLostCase\("PC"\);/g);need(ack?.length===1,'TAIL_ACK_CALL');call('fullPipePrepareAckLostCase',['PC'],{driverText:ack[0],origin:'fixtureTargetReturnFullPipeCases'});
 const counts=new Map();const occurrences=rows.map((row,index)=>{const nameOccurrence=(counts.get(row.name)??0)+1;counts.set(row.name,nameOccurrence);return {sourceOrdinal:index+1,nameOccurrence,occurrenceId:`${index+1}:${nameOccurrence}:${row.name}`,...row};});
 return {schemaVersion:2,kind:'STATIC_SOURCE_PROJECTION_NOT_NATIVE_EXECUTION',original95WitnessBoundary:legacy95Boundary,plannedOccurrences:occurrences.length,plannedUniqueNames:counts.size,expectedRuntimeChecks:null,expectedOwnedChildren:null,sourceInputs,functionInputs:[...definitions].map(([definition,code])=>({definition,sha256:hashSource(code)})),occurrences,partitionClosureVerified:false};
}

export function parseOriginalB51Recipe(producer){
 need(typeof producer==='string'&&hashSource(producer)===B51_RETURN_PROFILE.producerSHA,'B51_EXACT_PRODUCER_REQUIRED');const start=uniqueRange(producer,'for(const[name,body]of [');const arrayStart=producer.indexOf('[',start+'for(const[name,body]of '.length),arrayEnd=matching(producer,arrayStart);let i=arrayStart+1;const rows=[];while((i=skip(producer,i))<arrayEnd){need(producer[i++]==='[','RECIPE_ROW');i=skip(producer,i);const name=quoted(producer,i);i=skip(producer,name.end);need(producer[i++]===',','RECIPE_COMMA');i=skip(producer,i);const body=quoted(producer,i);i=skip(producer,body.end);need(producer[i++]===']','RECIPE_ROW_END');rows.push([name.value,body.value]);i=skip(producer,i);if(producer[i]===',')i++;else need(i===arrayEnd,'RECIPE_SEPARATOR');}
 const loopBody=producer.indexOf('{',arrayEnd),end=matching(producer,loopBody)+1;
 need(rows.length===28&&new Set(rows.map(r=>r[0])).size===28,'B51_RECIPE_28_UNIQUE');
 return {rows,start,end,loop:producer.slice(start,end),producerSha256:hashSource(producer)};
}

// The default historical path and its complete projection remain byte-equivalent.
export function deriveB49SourceOrder(files){return deriveSourceOrder(files);}
export function assertB51FixtureFiles(files){
 const expected=B51_RETURN_PROFILE.fixtureSHA;
 need(files&&Object.keys(files).length===Object.keys(expected).length,'B51_EXACT_FIXTURE_MEMBERS');
 for(const [name,sha]of Object.entries(expected))need(typeof files[name]==='string'&&hashSource(files[name])===sha,'B51_EXACT_FIXTURE:'+name);
}
export function deriveB51SourceOrder(files){
 assertB51FixtureFiles(files);
 const raw=deriveSourceOrder(files,true),extraName=B51_RETURN_PROFILE.additionalCaseName;
 const extra=raw.occurrences.filter(r=>r.name===extraName);need(extra.length===1&&extra[0].sourceOrdinal===93,'B51_EXACT_ADDED_CASE');
 const old=raw.occurrences.filter(r=>r.name!==extraName);
 need(old.length===100&&hashSource(JSON.stringify(old.map(r=>r.name)))===B51_RETURN_PROFILE.legacyNameSequenceSHA,'B51_ORIGINAL100_SUBSEQUENCE');
 const occurrenceCounts=new Map(),original=old.map((row,index)=>{const ordinal=index+1,count=(occurrenceCounts.get(row.name)??0)+1;occurrenceCounts.set(row.name,count);return {sourceOrdinal:ordinal,nameOccurrence:count,occurrenceId:ordinal+':'+count+':'+row.name,name:row.name,actualSourceOrdinal:row.sourceOrdinal,region:row.region};});
 const map=new Map(original.map(r=>[r.actualSourceOrdinal,r]));
 const occurrences=raw.occurrences.map(row=>({...row,originalSourceOrdinal:map.get(row.sourceOrdinal)?.sourceOrdinal??null,originalOccurrenceId:map.get(row.sourceOrdinal)?.occurrenceId??null,explicitB51Extension:row.name===extraName}));
 const witness=occurrences.find(r=>r.originalSourceOrdinal===95);
 need(witness?.name===B51_RETURN_PROFILE.legacyMsiWitnessName&&witness.sourceOrdinal===96,'B51_MSI_WITNESS_MAPPING');
 need(raw.plannedOccurrences===101&&raw.plannedUniqueNames===100,'B51_ACTUAL_STATIC_COUNT');
 const {original95WitnessBoundary:actualOriginalRegionBoundary,...projection}=raw;
 return {...projection,sourceProfile:'B51_FIXED_EXACT',kind:'STATIC_B51_SOURCE_PROJECTION_NOT_EXECUTION',occurrences,actualOriginalRegionBoundary,original95Witness:{name:witness.name,originalSourceOrdinal:95,actualSourceOrdinal:witness.sourceOrdinal},additionalOccurrences:occurrences.filter(r=>r.explicitB51Extension),originalContract:{kind:'UNCHANGED_B49_NAME_ORDER_SUBSEQUENCE_NOT_B51_RAW_COUNT',plannedOccurrences:100,plannedUniqueNames:99,original95WitnessBoundary:95,nameSequenceSHA:B51_RETURN_PROFILE.legacyNameSequenceSHA,occurrences:original},profileBinding:B51_RETURN_PROFILE.fixedBinding,sourceAuthorityAdmitted:false,compileApproved:false,executionApproved:false};
}
export function prepareB51OrderedFixture(fixture){
 need(typeof fixture==='string'&&hashSource(fixture)===B51_RETURN_PROFILE.fixtureSHA['fixture.cpp'],'B51_EXACT_ORDERED_FIXTURE_INPUT');let out=fixture;
 for(const row of B51_RETURN_PROFILE.observationPatches){need(out.split(row.before).length===2,'B51_ORDERED_UNIQUE_ANCHOR:'+row.label);out=out.replace(row.before,row.after);}
 function restore(value){for(const row of [...B51_RETURN_PROFILE.observationPatches].reverse()){need(value.split(row.after).length===2,'B51_ORDERED_INVERSE');value=value.replace(row.after,row.before);}need(value===fixture,'B51_ORDERED_INVERSE_BYTES');return value;}
 need(restore(out)===fixture,'B51_ORDERED_RESTORATION');
 return {fixture:out,restore,status:'STATIC_ORDERED_OBSERVATION_SOURCE_NOT_EXECUTED',beforeSHA:hashSource(fixture),afterSHA:hashSource(out),actualPrefixBoundary:96,originalWitnessOrdinal:95,postTailSeparate:true,observationPatches:B51_RETURN_PROFILE.observationPatches,compileApproved:false,executionApproved:false};
}
