// Local Steam CDP only; no internet or account/profile APIs.
export async function targets(){const r=await fetch('http://127.0.0.1:8080/json/list',{signal:AbortSignal.timeout(5000)});if(!r.ok)throw Error('Local Steam CDP unavailable');return r.json();}
export async function connect(url){
 if(!/^ws:\/\/127\.0\.0\.1:8080\/devtools\/page\//.test(url))throw Error('Non-local CDP endpoint rejected');
 const socket=new WebSocket(url),pending=new Map();let id=0;
 await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',()=>reject(Error('CDP connect failed')),{once:true});});
 socket.addEventListener('message',event=>{let m;try{m=JSON.parse(event.data);}catch{return;}const waiter=pending.get(m.id);if(waiter){pending.delete(m.id);clearTimeout(waiter.timer);m.error?waiter.reject(Error(JSON.stringify(m.error))):waiter.resolve(m.result);}});
 return {send(method,params={}){return new Promise((resolve,reject)=>{const next=++id;const timer=setTimeout(()=>{pending.delete(next);reject(Error('CDP deadline:'+method));},15000);pending.set(next,{resolve,reject,timer});socket.send(JSON.stringify({id:next,method,params}));});},async evaluate(expression){const r=await this.send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true,timeout:12000});if(r.exceptionDetails)throw Error(r.exceptionDetails.exception?.description||r.exceptionDetails.text);return r.result?.value;},close(){socket.close();for(const waiter of pending.values()){clearTimeout(waiter.timer);waiter.reject(Error('CDP closed'));}pending.clear();}};
}
