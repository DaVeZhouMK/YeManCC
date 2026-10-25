import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
// Local extraction preserves C# verbatim strings such as @"\\?\".
// Keep the shared extractor unchanged: this bootstrap fixture owns its parser.
function block(text,signature){
 const at=text.indexOf(signature);if(at<0||text.indexOf(signature,at+signature.length)>=0)throw Error('Missing/ambiguous bootstrap signature: '+signature);
 const start=text.indexOf('{',at);if(start<0)throw Error('Missing bootstrap body: '+signature);
 let depth=0,state='code',verbatim=false;
 for(let i=start;i<text.length;i++){
  const c=text[i],n=text[i+1];
  if(state==='line'){if(c==='\n')state='code';continue;}
  if(state==='comment'){if(c==='*'&&n==='/'){state='code';i++;}continue;}
  if(state==='string'){
   if(verbatim&&c==='"'&&n==='"'){i++;continue;}
   if(!verbatim&&c==='\\'){i++;continue;}
   if(c==='"')state='code';continue;
  }
  if(state==='char'){if(c==='\\'){i++;continue;}if(c==="'")state='code';continue;}
  if(c==='/'&&n==='/'){state='line';i++;continue;}
  if(c==='/'&&n==='*'){state='comment';i++;continue;}
  if(c==='"'){state='string';verbatim=text[i-1]==='@'||text.slice(i-2,i)==='@$';continue;}
  if(c==="'"){state='char';continue;}
  if(c==='{')depth++;else if(c==='}'&&--depth===0)return text.slice(at,i+1);
 }
 throw Error('Unclosed bootstrap body: '+signature);
}
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export function generateBootstrapFixture({repo,out}){
 const here=path.join(repo,'tools/ymcc_sim/global/onex-fan-coexistence'),device='deps/handheldcompanion-runtime/source/Devices/IDevice.cs',sources={},methods=[];
 const source=relative=>{const bytes=fs.readFileSync(path.join(repo,relative));sources[relative]=hash(bytes);return bytes.toString('utf8');};
 const d=source(device),original=block(d,'public static IDevice GetCurrent()'),names=[...new Set([...original.matchAll(/new\s+(\w+)\s*\(/g)].map(m=>m[1]))];
 const deviceRoot=path.join(repo,'deps/handheldcompanion-runtime/source/Devices'),files=[];function walk(root){for(const e of fs.readdirSync(root,{withFileTypes:true})){const file=path.join(root,e.name);if(e.isDirectory())walk(file);else if(e.name.endsWith('.cs'))files.push(file);}}walk(deviceRoot);
 const types=new Map();for(const file of files){const relative=path.relative(repo,file).replaceAll('\\','/'),bytes=fs.readFileSync(file),text=bytes.toString('utf8');sources[relative]=hash(bytes);const ns=text.match(/namespace\s+([\w.]+)\s*[;{]/)?.[1];for(const m of text.matchAll(/\bclass\s+(\w+)\b/g)){if(names.includes(m[1])||m[1]==='ASUS'){if(types.has(m[1]))throw Error('Ambiguous original device type');types.set(m[1],{name:ns+'.'+m[1],relative,sha256:hash(fs.readFileSync(file))});}}}
 const mapping=names.map(name=>{const row=types.get(name==='AsusDevice'?'ASUS':name);if(!row)throw Error('Original constructor type source missing: '+name);sources[row.relative]=row.sha256;return{shortName:name,...row};});
 let get=original;for(const row of mapping)get=get.replaceAll('new '+row.shortName+'(','new global::'+row.name+'(');
 let text=source('tools/ymcc_sim/global/onex-fan-coexistence/BootstrapFixture.template.cs');
 function replace(marker,relative,signature,value){const originalContent=block(source(relative),signature),content=value??originalContent;if(text.split('/*'+marker+'*/').length!==2)throw Error('Bootstrap marker differs: '+marker);text=text.replace('/*'+marker+'*/',()=>content);methods.push({relative,signature,sourceSha256:hash(originalContent),generatedSha256:hash(content),sourceTransform:value?'Constructor type names qualified from original source namespace table':'none'});}
 replace('GET_CURRENT',device,'public static IDevice GetCurrent()',get);
 replace('PULL_SENSORS',device,'public void PullSensors()');replace('INITIALIZE',device,'public virtual void Initialize(bool FirstStart, bool NewUpdate)');replace('CAPABILITIES',device,'public enum DeviceCapabilities : ushort');
 replace('BETWEEN','deps/handheldcompanion-runtime/source/Utils/CommonUtils.cs','public static string? Between(');
 replace('MOTHERBOARD','deps/handheldcompanion-runtime/source/Misc/MotherboardInfo.cs','public static class MotherboardInfo');
 const constructors=mapping.map(row=>{const split=row.name.lastIndexOf('.'),ns=row.name.slice(0,split),name=row.name.slice(split+1);return`namespace ${ns} {public sealed class ${name}:global::HandheldCompanion.Devices.IDevice {public ${name}(){global::BootstrapFixture.Ports.Constructor(typeof(${name}).FullName!);}}}`;}).join('\n');
 text=text.replace('/*MODEL_CONSTRUCTORS*/',()=>constructors);fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'BootstrapFixture.g.cs'),text);
 const result={sourceHashes:sources,methods,constructorTypeMapping:mapping,generatedSha256:hash(text),scope:'Original complete GetCurrent/PullSensors/Between/Motherboard source algorithms; WMI, sensors, telemetry and all device constructors are explicit inert terminals. SuiPlay constructor refuses execution.',hardwareIo:false,constructorsOriginalBodiesExecuted:false};fs.writeFileSync(path.join(out,'bootstrap-provenance.json'),JSON.stringify(result,null,2)+'\n');return result;
}
