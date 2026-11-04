import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';
const out=path.resolve(process.argv[2]),source=path.resolve(process.argv[3]||path.join(import.meta.dirname,'../native/main.cpp'));fs.mkdirSync(out,{recursive:true});const text=fs.readFileSync(source,'utf8');
// Match the actual lambda body, independent of later initialize() arguments.
// Strings and comments cannot lend braces to a different source boundary.
function lambdaAt(start){
 const body=text.indexOf('{',start);assert(start>=0&&body>start,'lambda body required');
 let depth=0,quote=null,escape=false,lineComment=false,blockComment=false;
 for(let i=body;i<text.length;i++){
  const c=text[i],n=text[i+1];
  if(lineComment){if(c==='\n')lineComment=false;continue;}
  if(blockComment){if(c==='*'&&n==='/'){blockComment=false;i++;}continue;}
  if(quote){if(escape)escape=false;else if(c==='\\')escape=true;else if(c===quote)quote=null;continue;}
  if(c==='/'&&n==='/'){lineComment=true;i++;continue;}
  if(c==='/'&&n==='*'){blockComment=true;i++;continue;}
  if(c==='"'||c==="'"){quote=c;continue;}
  if(c==='{')depth++;else if(c==='}'&&!--depth)return text.slice(start,i+1);
 }
 throw Error('unterminated source lambda');
}
const at=text.indexOf('    cssGuard.confirmExisting =');assert(at>=0&&text.indexOf('    cssGuard.confirmExisting =',at+1)<0);
const confirm=lambdaAt(text.indexOf('[]',at));
const init=text.indexOf('    g_steamCssUse.initialize(');assert(init>=0);const begin=text.indexOf('[](const std::string& stage',init);assert(begin>init);const changed=lambdaAt(begin);
assert(confirm.includes('g_steamCssUse.confirmExisting(session)'));assert(!/CreateProcess|ShellExecute|SetForegroundWindow|RestartJSContext|appearanceModeReady/.test(confirm));assert(changed.includes('requestReadOnlyRebind(session, result.useGeneration)'));
assert(!changed.includes('cssGuard.linkFile')&&!changed.includes('g_steamSettingsObserver.start('),'changed extraction crossed the lambda body');
fs.writeFileSync(path.join(out,'production-css-manual.h'),'inline const auto productionManualConfirm = '+confirm+';\ninline const auto productionManualChanged = '+changed+';\n');fs.writeFileSync(path.join(out,'source.json'),JSON.stringify({source,sha256:crypto.createHash('sha256').update(text).digest('hex'),confirm,changed,scope:'Exact source lambdas; test stubs only the owned observer/use/diagnostic ports'},null,2));console.log('Extracted real manual completion and read-only confirmation callbacks');
