'use strict';
// GPU-only runtime observer; exact static pins precede all package execution.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),Module=require('node:module'),cp=require('node:child_process');
const write=fs.writeFileSync.bind(fs),read=fs.readFileSync.bind(fs),real=fs.realpathSync.bind(fs),load=Module._load,spawn=cp.spawn,spawnSync=cp.spawnSync;
const config=process.env.YMCC_GPU_OBSERVER_CONFIG;if(!config||!path.isAbsolute(config))throw Error('GPU_OBSERVER_CONFIG_REQUIRED');
const cfg=JSON.parse(read(config,'utf8')),key=p=>path.resolve(p).toLowerCase(),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pins=new Map(cfg.reads.map(row=>[key(row.path),row])),generated=new Map();
const trace={schemaVersion:1,status:'PASS',pid:process.pid,reads:[],verificationReads:[],loads:[],processes:[],outputs:[],failure:null,node:process.execPath};
const inside=p=>{const r=path.relative(cfg.out,p);return r!==''&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
function deny(code,file){trace.status='FAIL';trace.failure={code,path:String(file)};throw Error('GPU_OBSERVER_'+code+':'+file);}
function check(file,kind){
 if(typeof file==='number')return deny('UNDECLARED_FD',file);
 const lexical=file instanceof URL?require('node:url').fileURLToPath(file):path.resolve(String(file)),actual=real(lexical),pin=pins.get(key(actual));
 if(key(lexical)!==key(actual)&&!(cfg.aliases??[]).some(row=>key(row.path)===key(lexical)&&key(row.target)===key(actual)&&row.sha256===pin?.sha256))return deny('UNDECLARED_ALIAS',lexical);
 if(!pin){if(inside(actual)&&key(actual)===key(lexical)&&!/[.](?:[cm]?js|ts|vue|exe|dll|cmd|bat)$/i.test(actual))return actual;return deny('UNDECLARED_READ',actual);}
 if(hash(read(actual))!==pin.sha256)return deny('CHANGED_INPUT',actual);
 trace[kind].push({path:actual,sha256:pin.sha256});return actual;
}
fs.writeFileSync=function(file,...args){
 const actual=path.resolve(String(file));if(!inside(actual)||key(real(path.dirname(actual)))!==key(path.dirname(actual)))return deny('OUTPUT_ESCAPE',actual);
 if(fs.existsSync(actual)&&key(real(actual))!==key(actual))return deny('OUTPUT_ALIAS',actual);
 write(file,...args);if(key(real(actual))!==key(actual))return deny('OUTPUT_ALIAS',actual);const sha256=hash(read(actual)),old=pins.get(key(actual));if(old&&!generated.has(key(actual))&&old.sha256!==sha256)return deny('STATIC_INPUT_OVERWRITE',actual);
 const row={path:actual,sha256,role:'actual-owned-generated-output'};generated.set(key(actual),row);pins.set(key(actual),row);
};
fs.readFileSync=function(file,...args){check(file,globalThis[Symbol.for('ymcc.gpu.verification')]?'verificationReads':'reads');return read(file,...args);};
const asyncRead=fs.promises.readFile.bind(fs.promises);fs.promises.readFile=async function(file,...args){check(file,globalThis[Symbol.for('ymcc.gpu.verification')]?'verificationReads':'reads');return asyncRead(file,...args);};
const callbackRead=fs.readFile.bind(fs);fs.readFile=function(file,...args){check(file,globalThis[Symbol.for('ymcc.gpu.verification')]?'verificationReads':'reads');return callbackRead(file,...args);};
Module._load=function(name,parent,isMain){const resolved=Module._resolveFilename(name,parent,isMain);if(path.isAbsolute(resolved))check(resolved,'loads');return load.apply(this,arguments);};
function child(file,args){const actual=real(file),pin=pins.get(key(actual));if(!pin||hash(read(actual))!==pin.sha256)return deny('UNDECLARED_PROCESS',file);if(actual!==cfg.compiler&&actual!==cfg.browser?.executable&&!(inside(actual)&&actual.endsWith('.exe')))return deny('PROCESS_BOUNDARY',actual);trace.processes.push({path:actual,sha256:pin.sha256,args});}
cp.spawn=function(file,args,...rest){child(file,args);return spawn.call(this,file,args,...rest);};cp.spawnSync=function(file,args,...rest){child(file,args);return spawnSync.call(this,file,args,...rest);};
for(const name of ['exec','execSync','execFile','execFileSync','fork'])cp[name]=()=>deny('UNREVIEWED_PROCESS_API',name);
require('node:module').syncBuiltinESMExports();
process.on('exit',()=>{const dest=process.env.YMCC_GPU_OBSERVER_TRACE;if(!dest||!inside(dest))return;trace.outputs=[...generated.values()];write(dest,JSON.stringify(trace,null,2));});
