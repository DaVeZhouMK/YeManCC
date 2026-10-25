// Small conservative lexical reader for the fixed C#/C++ source slices.
// Not a language compiler: records syntactic calls (including conditional branches),
// and fails closed when a required definition becomes ambiguous or unbalanced.
export function lex(text) {
  const out=[]; let i=0,line=1;
  const push=(kind,start,end,ln)=>out.push({kind,value:text.slice(start,end),start,end,line:ln});
  while(i<text.length) {
    const c=text[i],start=i,ln=line;
    if (/\s/.test(c)) { if(c==='\n')line++; i++; continue; }
    if(text.startsWith('//',i)) { while(i<text.length&&text[i]!=='\n')i++; continue; }
    if(text.startsWith('/*',i)) { i+=2; while(i<text.length&&!text.startsWith('*/',i)){if(text[i++]==='\n')line++;} if(i===text.length)throw Error('Unterminated comment'); i+=2; continue; }
    if(text.startsWith('R"',i)) {
      const left=text.indexOf('(',i+2),delimiter=text.slice(i+2,left);
      if(left<0||delimiter.length>16||/[\s\\()]/.test(delimiter))throw Error('Invalid C++ raw string');
      const close=')'+delimiter+'"',end=text.indexOf(close,left+1);if(end<0)throw Error('Unterminated C++ raw string');
      i=end+close.length;line+=(text.slice(start,i).match(/\n/g)||[]).length;push('string',start,i,ln);continue;
    }
    if(text.startsWith('"""',i)) {
      let n=3;while(text[i+n]==='"')n++;const end=text.indexOf('"'.repeat(n),i+n);if(end<0)throw Error('Unterminated C# raw string');
      i=end+n;line+=(text.slice(start,i).match(/\n/g)||[]).length;push('string',start,i,ln);continue;
    }
    if(c==='"'||c==="'"||(c==='@'&&text[i+1]==='"')) {
      const verb=c==='@',quote=verb?'"':c; i+=verb?2:1; let closed=false;
      while(i<text.length) {
        if(text[i]==='\n')line++;
        if(text[i]===quote) { if(verb&&text[i+1]==='"'){i+=2;continue;} i++;closed=true;break; }
        if(!verb&&text[i]==='\\'){i+=2;continue;} i++;
      }
      if(!closed)throw Error('Unterminated string');push('string',start,i,ln);continue;
    }
    if(/[A-Za-z_$]/.test(c)) { i++;while(i<text.length&&/[A-Za-z0-9_$]/.test(text[i]))i++;push('id',start,i,ln);continue; }
    if(/[0-9]/.test(c)) {i++;while(i<text.length&&/[A-Za-z0-9_.']/ .test(text[i]))i++;push('number',start,i,ln);continue;}
    i++;push('punct',start,i,ln);
  }
  return out;
}
export function paired(tokens,start,left='{',right='}') {
  if(tokens[start]?.value!==left)throw Error(`Expected ${left}`);
  let depth=0;
  for(let i=start;i<tokens.length;i++) { if(tokens[i].kind==='string')continue;
    if(tokens[i].value===left)depth++; if(tokens[i].value===right&&!--depth)return i;
  }
  throw Error(`Unbalanced ${left}`);
}
export function definition(text,name,{optional=false}={}) {
  const t=lex(text),found=[];
  for(let i=0;i<t.length-1;i++) {
    if(t[i].kind!=='id'||t[i].value!==name||t[i+1].value!=='(')continue;
    const end=paired(t,i+1,'(',')'); let b=end+1;
    // Skip a simple ctor initializer; not needed for the fixed method slices.
    if(t[b]?.value!=='{')continue;
    const e=paired(t,b); let a=i;
    while(a>0&&![';','{','}'].includes(t[a-1].value))a--;
    // C# declaration begins with an access modifier; C++ with static/template.
    const prefix=t.slice(a,i).map(x=>x.value);
    if(!prefix.some(x=>['public','private','protected','internal','static'].includes(x)))continue;
    const raw=text.slice(t[a].start,t[e].end);
    found.push({name,code:raw,body:text.slice(t[b].end,t[e].start),line:t[a].line,start:t[a].start,end:t[e].end,
      calls:calls(text.slice(t[b].end,t[e].start))});
  }
  if(found.length!==1) {if(optional&&!found.length)return null;throw Error(`Definition ${name}: expected 1, found ${found.length}`);}
  return found[0];
}
export function calls(body) {
  const t=lex(body),r=[];
  for(let i=0;i<t.length-1;i++)if(t[i].kind==='id'&&t[i+1].value==='('&& !['if','switch','for','foreach','while','catch','sizeof','typeof','nameof','lock','return'].includes(t[i].value)) {
    let a=i;while(a>=2&&['.','?',':','>','-'].includes(t[a-1].value)&&t[a-2].kind==='id')a-=2;
    r.push({callee:t.slice(a,i+1).map(x=>x.value).join(''),line:t[i].line});
  }
  return r;
}
export function declaration(text,name) {
  const t=lex(text),at=t.findIndex((x,i)=>x.kind==='id'&&x.value===name&& ['enum','class','struct'].some(k=>t.slice(Math.max(0,i-2),i).some(y=>y.value===k)));
  if(at<0)throw Error(`Missing declaration ${name}`);let b=at;while(t[b]?.value!=='{')if(++b===t.length)throw Error(`Missing body ${name}`);
  const e=paired(t,b);let a=at;while(a>0&&![';','{','}'].includes(t[a-1].value))a--;
  // Preserve only declaration, excluding namespace/using boundaries.
  while(a<at&&!['enum','struct','class'].includes(t[a].value))a++;
  return text.slice(t[a].start,t[e].end)+(t[e+1]?.value===';'?';':'');
}
export function lambdaForIpc(text,operation) {
  const t=lex(text);
  const at=t.findIndex((x,i)=>x.value==='ipc_on'&&t[i+2]?.kind==='string'&&t[i+2].value===JSON.stringify(operation));
  if(at<0)throw Error(`Missing IPC ${operation}`); let a=at+3;
  while(t[a]?.value!=='[')if(++a===t.length)throw Error('Lambda not found');
  let b=a;while(t[b]?.value!=='{')if(++b===t.length)throw Error('Lambda body not found');
  const e=paired(t,b);return {name:operation,code:text.slice(t[a].start,t[e].end),line:t[a].line,start:t[a].start,end:t[e].end};
}

const roles=new Set(['discover','open','physicalRead','decode','transform','publish','declaration','targetBuild','targetSubmit']);
const required=['open','physicalRead','decode','publish'];
export function classify(graph,capability,realm='ymcc') {
  if(!graph||!Array.isArray(graph.nodes)||!Array.isArray(graph.edges))throw Error('Invalid graph');
  const nodes=new Map();
  for(const n of graph.nodes){if(!n.id||nodes.has(n.id)||!roles.has(n.role)||!Array.isArray(n.capabilities)||!['ymcc','hc-reference','unresolved'].includes(n.realm))throw Error('Invalid/duplicate node');nodes.set(n.id,n);}
  for(const e of graph.edges)if(!nodes.has(e.from)||!nodes.has(e.to)||!['call','event','data','lifecycle','reference'].includes(e.kind))throw Error('Invalid edge');
  const queue=graph.nodes.filter(n=>n.entry&&n.realm===realm).map(n=>({id:n.id,step:0,trace:[]})),seen=new Set();
  while(queue.length) {
    const q=queue.shift(),n=nodes.get(q.id);
    // Target-side source-shaped objects must never satisfy a physical read.
    if(n.realm!==realm||['declaration','targetBuild','targetSubmit'].includes(n.role)||n.protocol!=='neptune'||!n.capabilities.includes(capability)||n.available!==true)continue;
    let step=q.step;if(n.role===required[step])step++;
    if(n.role==='physicalRead'&&n.origin!=='physical')continue;
    if(n.role==='decode'&&step<3)continue;
    const trace=[...q.trace,n.id]; if(step===required.length)return {actualConsumer:true,path:trace};
    const key=`${q.id}:${step}`;if(seen.has(key))continue;seen.add(key);
    for(const e of graph.edges)if(e.from===q.id&&!['reference','lifecycle'].includes(e.kind))queue.push({id:e.to,step,trace});
  }
  return {actualConsumer:false,path:[],missing:'No connected available open -> physicalRead -> decode -> publish path for this capability in the YMCC executable closure'};
}
export function queryVerdict(result) {
  const errors=[];
  if(result?.familyId!=='valve-steam-deck'||result?.supported!==true||!Array.isArray(result?.keys)||result.keys.length!==2)errors.push('catalog identity/count');
  for(const id of ['special','special2']) {
    const keys=result?.keys?.filter(x=>x.keyId===id)||[];
    if(keys.length!==1){errors.push(`${id}: multiplicity`);continue;}
    const k=keys[0];
    if(k.physicalForm!=='embedded'||k.backIndex!==0||k.captureImplemented!==false||k.captureRoute!=='physical-neptune-reader-unimplemented')errors.push(`${id}: physical capture contract`);
  }
  return {pass:errors.length===0,errors};
}

