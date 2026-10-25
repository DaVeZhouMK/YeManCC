// Protocol-only fixture; never connects to Steam or executes the received expression.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
const root=process.cwd(),out=path.resolve(root,'../../Temp/steam-appearance-validation/cef-robust');fs.mkdirSync(out,{recursive:true});
const sockets=new Set();
function frame(text,opcode=1,fin=true){const bytes=Buffer.from(text);assert(bytes.length<126);return Buffer.concat([Buffer.from([(fin?128:0)|opcode,bytes.length]),bytes])}
const server=http.createServer();
server.on('upgrade',(req,socket)=>{
 sockets.add(socket);socket.on('error',()=>{});socket.on('close',()=>sockets.delete(socket));
 const accept=createHash('sha1').update(req.headers['sec-websocket-key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
 socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\n\r\n');
 socket.once('data',()=>{
  const event=JSON.stringify({method:'Runtime.consoleAPICalled',params:{}});
  if(req.url==='/unmatched'){socket.write(frame(event));return}
  const reply=req.url==='/exception'?{id:1,result:{exceptionDetails:{text:'fixture evaluation failed'}}}:req.url==='/error'?{id:1,error:{message:'fixture CDP error'}}:{id:1,result:{result:{type:'number',value:3}}};
  const text=JSON.stringify(reply);
  if(req.url==='/fragment'){socket.write(frame(event));socket.write(frame(text.slice(0,19),1,false));socket.write(frame(text.slice(19),0,true));}
  else socket.write(frame(text));
 });
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const url='ws://127.0.0.1:'+server.address().port;
function run(mode){return new Promise((resolve,reject)=>{
 const source=path.join(root,'PowerControl/steam-appearance.ps1').replaceAll("'","''");
 const command=`. '${source}' -Action Library; try { Invoke-CefStyleCleanup ([pscustomobject]@{webSocketDebuggerUrl='${url}/${mode}'})|ConvertTo-Json -Compress;exit 0 }catch{Write-Output $_.Exception.Message;exit 7}`;
 const encoded=Buffer.from(command,'utf16le').toString('base64');const child=spawn('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',encoded],{windowsHide:true});let stdout='',stderr='';child.stdout.on('data',d=>stdout+=d);child.stderr.on('data',d=>stderr+=d);child.on('error',reject);const timer=setTimeout(()=>{child.kill();reject(Error('CEF fixture worker timeout'))},20000);child.on('close',code=>{clearTimeout(timer);resolve({code,stdout:stdout.trim(),stderr})});
})}
const cases=[];
try{
 const fragmented=await run('fragment');assert.equal(fragmented.code,0,JSON.stringify(fragmented));assert.equal(fragmented.stdout,'true');cases.push('Unrelated CDP event followed by a fragmented matching acknowledgement is correctly assembled and confirmed');
 for(const mode of ['exception','error','unmatched']){const result=await run(mode);assert.equal(result.code,7,JSON.stringify(result));cases.push('Reject '+mode+' instead of reporting cleanup success')}
 fs.writeFileSync(path.join(out,'cef-result.json'),JSON.stringify({ok:true,scope:'Local inert WebSocket fixture and actual PowerShell cleanup client; no Steam connection or style mutation',cases},null,2));console.log('PASS: '+cases.length+' CDP framing/acknowledgement/failure checks');
}finally{for(const socket of sockets)socket.destroy();await new Promise(resolve=>server.close(resolve))}
