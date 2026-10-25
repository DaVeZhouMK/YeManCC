import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
import {closeBrace} from '../hc_remaining_model_capabilities/expectations.mjs';
// Independent syntax reader for pinned HC C# declarations; never reads candidate native recipes.
export function oemPrivateRecipeOracle(repo,freeze){
 const sha=b=>createHash('sha256').update(b).digest('hex');
 const pins=new Map(freeze(path.join(repo,'deps/handheldcompanion-runtime/source-files.sha256')).toString('utf8').split(/\r?\n/).filter(Boolean).map(line=>{const m=/^([a-f0-9]{64})\s+(.+)$/i.exec(line);if(!m)throw Error('HC source pin');return [m[2].replaceAll('\\','/'),m[1].toLowerCase()];}));
 const files=new Map(),root=path.join(repo,'deps/handheldcompanion-runtime/source');
 const read=relative=>{const b=freeze(path.join(root,relative)),digest=sha(b);if(pins.get(relative)!==digest)throw Error('Pinned OEM C# changed: '+relative);files.set(relative,digest);return b.toString('utf8');};
 const clean=t=>t.replace(/@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,m=>m.startsWith('/')?m.replace(/[^\r\n]/g,' '):m);
 const axes=['X','Y','Z'];
 const base=clean(read('Devices/IDevice.cs'));const ctor=/public IMUMatrix\(\)\s*\{/.exec(base);if(!ctor)throw Error('Pinned IMUMatrix constructor');const block=base.slice(base.indexOf('{',ctor.index)+1,closeBrace(base,base.indexOf('{',ctor.index)));
 if(!/Axis\s*=\s*new Vector3\(1\.0f, 1\.0f, 1\.0f\)/.test(block)||!axes.every(c=>block.includes(`{ '${c}', '${c}' }`)))throw Error('Unreviewed default IMU axes');
 const paths={IDevice:'Devices/IDevice.cs',ClawA1M:'Devices/MSI/ClawA1M.cs',ClawA2VM:'Devices/MSI/ClawA2VM.cs',ClawBZ2EM:'Devices/MSI/ClawBZ2EM.cs',ClawCG3EM:'Devices/MSI/ClawCG3EM.cs',ASUS:'Devices/ASUS/ASUS.cs',ROGAlly:'Devices/ASUS/ROGAlly.cs',ROGAllyX:'Devices/ASUS/ROGAllyX.cs',XboxROGAlly:'Devices/ASUS/XboxROGAlly.cs',XboxROGAllyX:'Devices/ASUS/XboxROGAllyX.cs'};
 const cache=new Map();
 function resolve(name){if(cache.has(name))return cache.get(name);if(!paths[name])throw Error('Unreviewed OEM parent '+name);const s=clean(read(paths[name]));const def=new RegExp('\\bclass\\s+'+name+'\\b(?:\\s*:\\s*([\\w.]+))?').exec(s);if(!def)throw Error('Missing class '+name);let parent=def[1]?.split('.').at(-1);const alias=parent&&new RegExp('\\busing\\s+'+parent+'\\s*=\\s*([\\w.]+)\\s*;').exec(s);if(alias)parent=alias[1].split('.').at(-1);
 const inherited=parent?resolve(parent):{gyro:{axis:[1,1,1],swap:[0,1,2]},accel:{axis:[1,1,1],swap:[0,1,2]}};const result=JSON.parse(JSON.stringify(inherited));
 const ctor=new RegExp('\\b(?:public|protected)\\s+'+name+'\\s*\\([^)]*\\)[^{;]*\\{').exec(s);if(ctor){const open=s.indexOf('{',ctor.index),text=s.slice(open+1,closeBrace(s,open));for(const m of text.matchAll(/\b(GyroMatrix|AcceleroMatrix)\s*=\s*/g)){
 const start=m.index+m[0].length,head=/^new(?:\s+IMUMatrix)?\s*\(\s*\)/.exec(text.slice(start));if(!head)throw Error('Unreviewed matrix allocation');let end=start+head[0].length;while(/\s/.test(text[end]||'')&&end<text.length)end++;if(text[end]==='{')end=closeBrace(text,end)+1;if(text.slice(end).trimStart()[0]!==';')throw Error('Unreviewed matrix tail');const expr=text.slice(start,end),matrix={axis:[1,1,1],swap:[0,1,2]};
 const signs=/\bAxis\s*=\s*new\s+Vector3\s*\(([^)]+)\)/.exec(expr);if(signs){matrix.axis=signs[1].split(',').map(v=>Number(v.trim().replace(/[fF]$/,'')));if(matrix.axis.length!==3||matrix.axis.some(v=>v!==1&&v!==-1))throw Error('Unsupported axis sign');}
 if(/\bAxisSwap\s*=/.test(expr)){const pairs=[...expr.matchAll(/\{\s*'([XYZ])'\s*,\s*'([XYZ])'\s*\}/g)];if(pairs.length!==3||new Set(pairs.map(p=>p[1])).size!==3||new Set(pairs.map(p=>p[2])).size!==3)throw Error('Invalid axis permutation');matrix.swap=axes.map(c=>axes.indexOf(pairs.find(p=>p[1]===c)[2]));}
 result[m[1]==='GyroMatrix'?'gyro':'accel']=matrix;
 }}cache.set(name,result);return result;}
 const models={};for(const name of ['ClawA1M','ClawA2VM','ClawBZ2EM','ClawCG3EM','XboxROGAlly','XboxROGAllyX']){const matrices=resolve(name),raw=[[1,2,3],[0,0,1]],sample=[];for(const [kind,input] of [['gyro',raw[0]],['accel',raw[1]]]){const matrix=matrices[kind],v=[NaN,NaN,NaN];input.forEach((value,i)=>v[matrix.swap[i]]=value);sample.push(...v.map((value,i)=>value*matrix.axis[i]));}models['HandheldCompanion.Devices.'+name]={matrices,sample};}
 return {schema:1,source:'pinned-CSharp-constructor-and-inheritance-syntax',candidateNativeRead:false,physicalAxesVerified:false,files:[...files].map(([path,sha256])=>({path,sha256})),models};
}
