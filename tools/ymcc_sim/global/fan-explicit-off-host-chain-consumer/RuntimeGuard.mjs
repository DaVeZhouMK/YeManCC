import fs from 'node:fs';import net from 'node:net';import tls from 'node:tls';import dns from 'node:dns';import dgram from 'node:dgram';import cp from 'node:child_process';import path from 'node:path';import crypto from 'node:crypto';import {syncBuiltinESMExports} from 'node:module';
const root=path.resolve(process.env.B0_CHAIN_NODE_EVIDENCE||'');
if(!process.env.B0_CHAIN_NODE_EVIDENCE||!fs.statSync(root).isDirectory())throw Error('EXPLICIT_OWNED_NODE_EVIDENCE_REQUIRED');
const origin=new URL(process.argv[2]);
if(origin.protocol!=='http:'||origin.hostname!=='127.0.0.1'||!origin.port||origin.username||origin.password)throw Error('EXACT_PRIVATE_LOOPBACK_ORIGIN_REQUIRED');
const originalWrite=fs.writeFileSync.bind(fs),originalFetch=globalThis.fetch,denied=[],requests=[],connections=[];
const initial={windowAbsent:typeof globalThis.window==='undefined',chromeAbsent:typeof globalThis.chrome==='undefined'};
let beforeExit=false,explicitExit=false;
const originalStdoutWrite=process.stdout.write,originalStderrWrite=process.stderr.write,stdoutChunks=[],stderrChunks=[];
process.stdout.write=function(chunk,encoding,...rest){stdoutChunks.push(Buffer.isBuffer(chunk)?Buffer.from(chunk):Buffer.from(String(chunk),typeof encoding==='string'?encoding:'utf8'));return originalStdoutWrite.call(this,chunk,encoding,...rest);};
process.stderr.write=function(chunk,encoding,...rest){stderrChunks.push(Buffer.isBuffer(chunk)?Buffer.from(chunk):Buffer.from(String(chunk),typeof encoding==='string'?encoding:'utf8'));return originalStderrWrite.call(this,chunk,encoding,...rest);};
function reject(kind,detail){denied.push({kind,detail:String(detail)});throw Error('EXPLICIT_CHAIN_BOUNDARY:'+kind);}
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const nativeDlopen=process.dlopen;process.dlopen=function(...args){return reject('NATIVE_ADDON_FORBIDDEN',args[1]);};
const nativeExit=process.exit;process.exit=function(code){explicitExit=true;return reject('EXPLICIT_EXIT_FORBIDDEN',code);};
for(const name of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork'])cp[name]=function(){return reject('NODE_CHILD_PROCESS_FORBIDDEN',name);};
for(const name of ['writeFile','writeFileSync','appendFile','appendFileSync','truncate','truncateSync','rename','renameSync','unlink','unlinkSync','rm','rmSync','rmdir','rmdirSync','mkdir','mkdirSync','copyFile','copyFileSync','cp','cpSync','symlink','symlinkSync','link','linkSync','createWriteStream'])if(typeof fs[name]==='function')fs[name]=function(){return reject('NODE_FILESYSTEM_MUTATION_FORBIDDEN',name);};
const connect=net.Socket.prototype.connect;
net.Socket.prototype.connect=function(...args){
 let candidate=args[0];if(Array.isArray(candidate))candidate=candidate[0];
 let host,port;if(candidate&&typeof candidate==='object'){host=candidate.host??candidate.hostname;port=candidate.port;}else{port=candidate;host=typeof args[1]==='string'?args[1]:undefined;}
 if(host!=='127.0.0.1'||Number(port)!==Number(origin.port))return reject('NETWORK_DESTINATION_FORBIDDEN',JSON.stringify({host,port}));
 connections.push({host,port:Number(port)});return connect.apply(this,args);
};
net.Server.prototype.listen=function(){return reject('NODE_LISTENER_FORBIDDEN','listen');};
tls.connect=function(){return reject('TLS_FORBIDDEN','connect');};
dgram.createSocket=function(){return reject('UDP_FORBIDDEN','createSocket');};
const lookup=dns.lookup;dns.lookup=function(host,...args){if(host!=='127.0.0.1')return reject('DNS_FORBIDDEN',host);return lookup.call(this,host,...args);};
for(const name of Object.keys(dns))if(name.startsWith('resolve')&&typeof dns[name]==='function')dns[name]=function(host){return reject('DNS_FORBIDDEN',host);};
syncBuiltinESMExports();
globalThis.fetch=async function(input,init={}){
 const target=new URL(typeof input==='string'||input instanceof URL?input:input.url);
 if(target.origin!==origin.origin||target.username||target.password)return reject('FETCH_ORIGIN_FORBIDDEN',target.origin);
 const method=(init.method||'GET').toUpperCase();if(!['GET','POST'].includes(method))return reject('HTTP_METHOD_FORBIDDEN',method);
 const headers=new Headers(init.headers),token=headers.get('X-YeMan-Fan-Session');
 const row={sequence:requests.length+1,path:target.pathname,method,sessionTokenPresent:!!token,sessionTokenSha256:token?sha(token):null,requestBody:typeof init.body==='string'?JSON.parse(init.body):null,startedAt:new Date().toISOString()};requests.push(row);
 try{const response=await originalFetch.call(this,input,init),body=await response.clone().text();row.status=response.status;row.responseSha256=sha(body);row.responseBytes=Buffer.byteLength(body);try{row.response=JSON.parse(body);}catch{row.responseText=body;}row.endedAt=new Date().toISOString();return response;}
 catch(error){row.transportError=String(error);throw error;}
};
function receipt(exitCode){return {schemaVersion:1,kind:'original-ts-actual-loopback-consumer-boundary',processId:process.pid,mode:process.argv[3]||'allowed',allowedOrigin:origin.origin,exitCode,initial,windowAbsentAtExit:typeof globalThis.window==='undefined',chromeAbsentAtExit:typeof globalThis.chrome==='undefined',beforeExitObserved:beforeExit,explicitExitCalled:explicitExit,denied,connections,requests,originalProgramOutput:{stdoutBase64:Buffer.concat(stdoutChunks).toString('base64'),stdoutSha256:sha(Buffer.concat(stdoutChunks)),stderrBase64:Buffer.concat(stderrChunks).toString('base64'),stderrSha256:sha(Buffer.concat(stderrChunks))},activeResources:process.getActiveResourcesInfo(),realHostMain:false,physicalAcceptance:false};}
process.on('beforeExit',code=>{beforeExit=true;originalWrite(path.join(root,'node-runtime-boundary.json'),JSON.stringify(receipt(code),null,2)+'\n');});
process.on('exit',code=>{originalWrite(path.join(root,'node-runtime-boundary.json'),JSON.stringify(receipt(code),null,2)+'\n');});
