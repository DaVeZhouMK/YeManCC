import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
// Small lexical shape contract, not a C# compiler or allocation measurement.
// Mask comments/string/char literals before matching the two unique production methods.
function maskCsharp(text){
 const out=text.split('');let i=0;
 const blank=(start,end)=>{for(let j=start;j<end;j++)if(out[j]!=='\r'&&out[j]!=='\n')out[j]=' ';};
 while(i<text.length){
  const start=i;
  if(text.startsWith('//',i)){i=text.indexOf('\n',i+2);if(i<0)i=text.length;blank(start,i);continue;}
  if(text.startsWith('/*',i)){const end=text.indexOf('*/',i+2);if(end<0)throw Error('SHAPE_UNCLOSED_COMMENT');i=end+2;blank(start,i);continue;}
  const raw=text.slice(i).match(/^(?:\$*)"{3,}/);
  if(raw){const quote=raw[0].match(/"+$/)[0],end=text.indexOf(quote,i+raw[0].length);if(end<0)throw Error('SHAPE_UNCLOSED_RAW_STRING');i=end+quote.length;blank(start,i);continue;}
  const literal=text.slice(i).match(/^(?:\$@|@\$|@|\$)?"|^'/);
  if(literal){const marker=literal[0],verbatim=marker.includes('@'),quote=marker.endsWith("'")?"'":'"';i+=marker.length;let closed=false;
   while(i<text.length){if(!verbatim&&text[i]==='\\'){i+=2;continue;}if(text[i]===quote){if(verbatim&&text[i+1]===quote){i+=2;continue;}i++;closed=true;break;}i++;}
   if(!closed)throw Error('SHAPE_UNCLOSED_LITERAL');blank(start,i);continue;
  }
  i++;
 }
 return out.join('');
}
function method(masked,name,signature){
 const matches=[...masked.matchAll(signature)];if(matches.length!==1)throw Error('SHAPE_METHOD_NOT_UNIQUE:'+name+':'+matches.length);
 const start=matches[0].index,open=masked.indexOf('{',start);if(open<0)throw Error('SHAPE_METHOD_BODY_MISSING:'+name);
 let depth=0;for(let i=open;i<masked.length;i++){if(masked[i]==='{')depth++;else if(masked[i]==='}'&&--depth===0)return {name,start,end:i+1,header:masked.slice(start,open),body:masked.slice(open+1,i)};}
 throw Error('SHAPE_METHOD_UNCLOSED:'+name);
}
export function inspectMaintenanceBufferShape(source){
 const rows=[];const check=(name,ok,observed)=>rows.push({name,ok:!!ok,observed});
 try{
  const masked=maskCsharp(source);
  const stop=method(masked,'StopHcControllerCpuLoops',/^    public bool StopHcControllerCpuLoops\s*\(/gm);
  const evaluate=method(masked,'EvaluateLoopStop',/^    private static bool EvaluateLoopStop\s*\(/gm);
  for(const name of ['stopped','failures']){
   const declarations=[...stop.body.matchAll(new RegExp('\\bList\\s*<\\s*string\\s*>\\s*\\?\\s*'+name+'\\s*=\\s*null\\s*;','g'))];
   check('null-local:'+name,declarations.length===1,declarations.length);
   const nullableRef=new RegExp('\\bref\\s+List\\s*<\\s*string\\s*>\\s*\\?\\s+'+name+'\\b').test(evaluate.header);check('nullable-ref:'+name,nullableRef,nullableRef);
   const empty=new RegExp('\\b'+name+'\\s*=\\s*\\(\\s*IReadOnlyList\\s*<\\s*string\\s*>\\s*\\?\\s*\\)\\s*'+name+'\\s*\\?\\?\\s*Array\\s*\\.\\s*Empty\\s*<\\s*string\\s*>\\s*\\(\\s*\\)','g');
   const empties=[...stop.body.matchAll(empty)];check('empty-diagnostic-array:'+name,empties.length===1,empties.length);
  }
  for(const m of [stop,evaluate]){
   const expected=m===stop?{stopped:3,failures:2}:{stopped:1,failures:2};
   const lazy=/\(\s*(stopped|failures)\s*\?\?=\s*new\s+List\s*<\s*string\s*>\s*\(\s*\)\s*\)\s*\.\s*Add\s*\(/g;
   const adds=[...m.body.matchAll(lazy)];
   for(const name of ['stopped','failures'])check('lazy-add:'+m.name+':'+name,adds.filter(a=>a[1]===name).length===expected[name],adds.filter(a=>a[1]===name).length);
   const constructors=[...m.body.matchAll(/\bnew\s+List\s*<\s*string\s*>\s*\(/g)];
   check('no-eager-result-list:'+m.name,constructors.length===adds.length,{constructors:constructors.length,lazyAdds:adds.length});
   const direct=[...m.body.matchAll(/\b(?:stopped|failures)\s*\??\s*\.\s*Add\s*\(/g)];
   check('no-direct-result-add:'+m.name,direct.length===0,direct.length);
  }
 }catch(error){check('unique-production-method-boundaries',false,error.message);}
 return {schemaVersion:1,kind:'fan-maintenance-buffer-source-contract',scope:'source-contract',ok:rows.length===14&&rows.every(r=>r.ok),checks:rows.length,cases:rows,
  sourceSha256:hash(source),hardwareCalls:0,hostMainExecuted:false,HCAssemblyLoaded:false,allocationBehaviorProven:false,cpuBenchmarkPerformed:false,
  limitations:['Lexical source-shape regression guard for two exact production methods; not a C# AST, alias analysis, IL proof or measured allocation/CPU benchmark.',
   'Does not prove pending/idle/lifecycle semantics; the separately executed original8+20 production-consumer checks retain that responsibility.']};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const args=process.argv.slice(2);if(args.length!==2||args[0]!=='--result')throw Error('Use fixed production source and --result <absolute-case-receipt>');
 const target=path.resolve(args[1]);if(!path.isAbsolute(args[1])||target===REPO||target.startsWith(REPO+path.sep)||fs.existsSync(target))throw Error('Fresh outside-repository result required');
 const file=path.join(REPO,'FanLab/real-host/Program.cs'),sourceBytes=fs.readFileSync(file),source=sourceBytes.toString('utf8'),result=inspectMaintenanceBufferShape(source);
 result.sourceHashes=[{path:'FanLab/real-host/Program.cs',sha256:hash(sourceBytes)},{path:'tools/ymcc_sim/global/fan-runtime-maintenance/buffer-shape-contract.mjs',sha256:hash(fs.readFileSync(fileURLToPath(import.meta.url)))}];
 fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({ok:result.ok,checks:result.checks,scope:result.scope,allocationBehaviorProven:false}));if(!result.ok)process.exitCode=1;
}
