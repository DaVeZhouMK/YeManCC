// The only adaptation is the Observer parameter type; preserve all other bytes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
export const producerReadInputs=['native/steam_ui_mode.h','tools/fixtures/steam_mode_read/producer_read.inc','tools/fixtures/steam_mode_read/producer_read.mjs'];
function closeAt(text,start,open,close){
 let depth=0;
 for(let i=start;i<text.length;++i){
  if(text.startsWith('//',i)){const end=text.indexOf('\n',i+2);i=end<0?text.length:end;continue;}
  if(text.startsWith('/*',i)){const end=text.indexOf('*/',i+2);assert(end>=0,'Unclosed C++ comment');i=end+1;continue;}
  if(text.startsWith('R"',i)){const begin=text.indexOf('(',i+2);assert(begin>=0,'Invalid raw string');const token=')'+text.slice(i+2,begin)+'"',end=text.indexOf(token,begin+1);assert(end>=0,'Unclosed raw string');i=end+token.length-1;continue;}
  if(text[i]==='"'||text[i]==="'"){const quote=text[i];let ended=false;while(++i<text.length){if(text[i]==='\\'){++i;continue;}if(text[i]===quote){ended=true;break;}}assert(ended,'Unclosed C++ string');continue;}
  if(text[i]===open)++depth;else if(text[i]===close&&!--depth)return i;
 }
 throw Error('Unclosed C++ '+open);
}
export function extractProducerRead(source){
 const matches=[...source.matchAll(/^inline\s+Json\s+read\s*\(/gm)];assert.equal(matches.length,1,'Unique complete steamui::read definition');
 const start=matches[0].index,paren=source.indexOf('(',start),endParen=closeAt(source,paren,'(',')');
 const body=source.slice(endParen+1).match(/^\s*\{/);assert(body,'Expected read function body');
 const open=endParen+1+body[0].lastIndexOf('{'),end=closeAt(source,open,'{','}')+1,original=source.slice(start,end);
 const parameter='steamsession::Observer& observer',adapted='mode_producer_port::IdentityObserver& observer';
 assert.equal(source.slice(start,endParen).split(parameter).length,2,'Unique Observer parameter adaptation');
 return {start,end,original,generated:original.replace(parameter,adapted)+'\n'};
}
const sourceUrl=new URL('../../../native/steam_ui_mode.h',import.meta.url),derivedUrl=new URL('./producer_read.inc',import.meta.url);
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
export function assertProducerRead(sourceBytes,derivedBytes){
 const slice=extractProducerRead(sourceBytes.toString('utf8'));
 assert.deepEqual(Buffer.from(derivedBytes),Buffer.from(slice.generated),'producer_read.inc must equal current complete steamui::read with only Observer parameter adaptation; regenerate with producer_read.mjs --write');
 return slice;
}
export function verifyProducerRead(){
 const bytes=fs.readFileSync(sourceUrl),actual=fs.readFileSync(derivedUrl),slice=assertProducerRead(bytes,actual);
 return {source:'native/steam_ui_mode.h',sourceBytes:bytes.length,sourceSha256:sha(bytes),derived:'tools/fixtures/steam_mode_read/producer_read.inc',derivedBytes:actual.length,derivedSha256:sha(actual),originalReadSha256:sha(slice.original),start:slice.start,end:slice.end,onlyObserverParameterAdapted:true};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const args=process.argv.slice(2);assert(args.length===1&&['--check','--write'].includes(args[0]),'Exactly --check or --write required');
 if(args[0]==='--write'){const next=extractProducerRead(fs.readFileSync(sourceUrl,'utf8')).generated;if(!fs.existsSync(derivedUrl)||fs.readFileSync(derivedUrl,'utf8')!==next)fs.writeFileSync(derivedUrl,next);}
 console.log(JSON.stringify({selection:args[0],passed:true,...verifyProducerRead()}));
}
