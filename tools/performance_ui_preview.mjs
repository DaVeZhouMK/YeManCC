import { createServer } from 'node:http';
import { createPerformancePreviewServer } from './lib/continuousPerformancePreview.mjs';
const port=Number(process.argv[2]||13132);
if(!Number.isInteger(port)||port<1024||port>65535)throw Error('Invalid preview port');
const server=await createPerformancePreviewServer(port);
console.log('YMCC production Vue components: http://127.0.0.1:'+server.address().port);
console.log('Preview uses memory settings; no hardware writes. Desktop template: ?template=desktop&uniform=1');
let redirect;const legacyPort=Number(process.argv[3]);if(legacyPort){redirect=createServer((req,res)=>{res.writeHead(302,{Location:'http://127.0.0.1:'+port+'/?simulation=1','Cache-Control':'no-store'});res.end()});await new Promise((resolve,reject)=>{redirect.once('error',reject);redirect.listen(legacyPort,'127.0.0.1',resolve)})}
for(const signal of ['SIGINT','SIGTERM'])process.once(signal,()=>{redirect?.close();server.close(()=>process.exit(0))});
