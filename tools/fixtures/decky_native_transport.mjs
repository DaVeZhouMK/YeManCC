// Development-only existing Windows websocket fixture framing, not delivered.
import assert from 'node:assert/strict';import net from 'node:net';import crypto from 'node:crypto';
export function deferred(){let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};}
export function bounded(promise,label){let timer;return Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('Timeout: '+label)),5000);})]).finally(()=>clearTimeout(timer));}
function masked(payload,opcode=1){const body=Buffer.from(payload);const mask=crypto.randomBytes(4);let header;
if(body.length<126)header=Buffer.from([0x80|opcode,0x80|body.length]);else header=Buffer.from([0x80|opcode,0xfe,body.length>>8,body.length&255]);const copy=Buffer.from(body);for(let i=0;i<copy.length;i++)copy[i]^=mask[i%4];return Buffer.concat([header,mask,copy]);}
class SocketAdapter {
  listeners=new Map();buffer=Buffer.alloc(0);upgraded=false;closed=false;sent=[];
  constructor(url,{origin='https://steamloopback.host'}={}){
    const parsed=new URL(url);assert.equal(parsed.hostname,'127.0.0.1');const nonce=crypto.randomBytes(16).toString('base64');this.accept=crypto.createHash('sha1').update(nonce+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    this.raw=net.connect({host:'127.0.0.1',port:Number(parsed.port)},()=>this.raw.write(`GET ${parsed.pathname}${parsed.search} HTTP/1.1\r\nHost: 127.0.0.1:${parsed.port}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${nonce}\r\nOrigin: ${origin}\r\n\r\n`));
    this.raw.on('error',()=>{});this.raw.on('close',()=>{this.closed=true;this.emit('close',{});});
    this.raw.on('data',data=>{this.buffer=Buffer.concat([this.buffer,data]);if(!this.upgraded){const end=this.buffer.indexOf('\r\n\r\n');if(end<0)return;const header=this.buffer.subarray(0,end).toString('ascii');assert.match(header,/^HTTP\/1.1 101 /);assert.ok(header.includes(this.accept));this.buffer=this.buffer.subarray(end+4);this.upgraded=true;this.emit('open',{});}
      while(this.buffer.length>=2){let size=this.buffer[1]&127,offset=2;if(size===126){if(this.buffer.length<4)return;size=this.buffer.readUInt16BE(2);offset=4;}else if(size===127){if(this.buffer.length<10)return;size=Number(this.buffer.readBigUInt64BE(2));offset=10;}assert.ok(size<=65536);assert.equal(this.buffer[1]&0x80,0);if(this.buffer.length<offset+size)return;const opcode=this.buffer[0]&15;const body=this.buffer.subarray(offset,offset+size);this.buffer=this.buffer.subarray(offset+size);if(opcode===1)this.emit('message',{data:body.toString('utf8')});else if(opcode===8)this.close();}
    });
  }
  addEventListener(event,listener){let listeners=this.listeners.get(event);if(!listeners)this.listeners.set(event,listeners=new Set());listeners.add(listener);}
  emit(event,value){for(const listener of this.listeners.get(event)??[])listener(value);}
  send(text){this.sent.push(JSON.parse(text));this.raw.write(masked(text));}
  close(){this.raw.destroy();}
}
function awaitState(client,predicate){if(predicate(client.snapshot()))return Promise.resolve(client.snapshot());const done=deferred();const stop=client.subscribe(state=>{if(predicate(state))done.resolve(state);});return bounded(done.promise,'client state').finally(stop);}

export {SocketAdapter,awaitState};
