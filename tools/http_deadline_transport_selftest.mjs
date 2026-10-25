import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const root=process.argv[2],requests=[];
const sockets=new Set();
const server=createServer((req,res)=>{
 requests.push(req.url);
 if(req.url==='/normal'){res.end('{"ok":true}');return;}
 if(req.url==='/denied'){res.writeHead(401);res.end('no');return;}
 if(req.url==='/slowheaders'){setTimeout(()=>{if(!res.destroyed)res.end('late');},1300).unref();return;}
 if(req.url==='/slowbody'){res.writeHead(200);res.write('x');const timer=setInterval(()=>{if(res.destroyed){clearInterval(timer);return;}res.write('x');},40);res.on('close',()=>clearInterval(timer));return;}
 if(req.url==='/large'){res.end(Buffer.alloc((8<<20)+1));return;}
});server.on('connection',s=>{sockets.add(s);s.on('close',()=>sockets.delete(s));});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const port=server.address().port,run=promisify(execFile),cases=[];
async function test(endpoint,budget,mode,expected){let output='',code=0;try{output=(await run(path.join(root,'http-transport-fixture.exe'),[String(port),endpoint,String(budget),mode],{windowsHide:true,timeout:5000})).stdout;}catch(e){output=e.stdout;code=e.code;}
 assert.match(output,expected);const elapsed=Number(output.match(/elapsed=(\d+)/)?.[1]);
 if(mode==='queue')assert(elapsed<100);
 if(endpoint==='/slowheaders'||endpoint==='/slowbody')assert(elapsed<650,'absolute deadline cannot refresh per packet');
 cases.push({endpoint,budget,mode,code,elapsed,output:output.trim()});}
try{await test('/normal',1000,'normal',/OK status=200/);await test('/denied',1000,'normal',/OK status=401/);
 await test('/slowheaders',250,'normal',/deadline exceeded/);await test('/slowbody',250,'normal',/deadline exceeded/);
 await test('/slowheaders',1000,'cancel',/cancelled during shutdown/);const before=requests.length;await test('/normal',250,'queue',/phase=queue/);assert.equal(requests.length,before,'expired queued POST must never reach server');
 await test('/large',3000,'normal',/exceeded 8 MiB/);
 const result={ok:true,cases,requests,scope:'production native async transport, real loopback HTTP, no HC or hardware'};fs.writeFileSync(path.join(root,'http-transport-results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
}finally{for(const s of sockets)s.destroy();await new Promise(r=>server.close(r));}