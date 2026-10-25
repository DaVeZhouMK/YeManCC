// This file reads one quoted data value; it never imports/evaluates the producer.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
export const LIFECYCLE_LOG_PRODUCER='tools/input_coordinator_lifecycle_selftest.mjs';
export const LIFECYCLE_LOG_DATA_ROLE='known-lifecycle-log-data';
const MAX_SOURCE=4*1024*1024,MAX_BODY=64*1024;
const hash=b=>createHash('sha256').update(b).digest('hex');
const need=(ok,code)=>{if(!ok)throw Error('CURRENT_LOG_'+code);};
export function parseLifecycleLogData(input,spec){
 need(spec?.schemaVersion===1&&spec.relative===LIFECYCLE_LOG_PRODUCER&&spec.key==='appendNativeLifecycleLog'&&/^[a-f0-9]{64}$/.test(spec.decodedUtf8Sha256??'')&&Number.isSafeInteger(spec.decodedUtf8Bytes)&&spec.decodedUtf8Bytes>0&&spec.decodedUtf8Bytes<=MAX_BODY,'DATA_PIN_REQUIRED');
 const bytes=Buffer.isBuffer(input)?input:Buffer.from(input,'utf8');need(bytes.length<=MAX_SOURCE,'PRODUCER_TOO_LARGE');const source=bytes.toString('utf8');
 const hits=[...source.matchAll(/\[\s*['"]appendNativeLifecycleLog['"]\s*,\s*/g)];need(hits.length===1,'EXACTLY_ONE_LITERAL_ROW_REQUIRED');
 let at=hits[0].index+hits[0][0].length;const literalStart=at,quote=source[at++];need(quote==="'"||quote==='"','QUOTED_LITERAL_REQUIRED');
 let body='',closed=false;const basic={n:'\n',r:'\r',t:'\t',b:'\b',f:'\f',v:'\v','0':'\0'};
 while(at<source.length){const ch=source[at++];if(ch===quote){closed=true;break;}need(ch!=='\n'&&ch!=='\r','UNESCAPED_NEWLINE');
  if(ch!=='\\')body+=ch;else{const escaped=source[at++];if(escaped==='x'||escaped==='u'){const count=escaped==='x'?2:4,digits=source.slice(at,at+count);need(new RegExp('^[a-fA-F0-9]{'+count+'}$').test(digits),'HEX_ESCAPE_REQUIRED');body+=String.fromCharCode(parseInt(digits,16));at+=count;}
   else if(Object.hasOwn(basic,escaped)){need(escaped!=='0'||!/[0-9]/.test(source[at]??''),'OCTAL_ESCAPE_REFUSED');body+=basic[escaped];}
   else if(escaped==='\\'||escaped==='"'||escaped==="'")body+=escaped;else throw Error('CURRENT_LOG_UNSUPPORTED_ESCAPE');}
  need(body.length<=MAX_BODY,'BODY_TOO_LARGE');
 }
 need(closed,'UNTERMINATED_LITERAL');const literalEnd=at;while(at<source.length&&/\s/.test(source[at]))at++;need(source[at]===']','LITERAL_SUFFIX_REQUIRED');
 const bodyBytes=Buffer.from(body,'utf8');need(bodyBytes.length===spec.decodedUtf8Bytes&&hash(bodyBytes)===spec.decodedUtf8Sha256,'DATA_VALUE_CHANGED');
 return {body,decodedUtf8Sha256:hash(bodyBytes),decodedUtf8Bytes:bodyBytes.length,wholeSourceSha256:hash(bytes),literalSourceSha256:hash(source.slice(literalStart,literalEnd)),producerExecuted:false};
}
export function readLifecycleLogData(repo,spec){
 need(typeof repo==='string'&&path.isAbsolute(repo),'ABSOLUTE_ROOT_REQUIRED');const file=path.join(repo,LIFECYCLE_LOG_PRODUCER),stat=fs.lstatSync(file);need(stat.isFile()&&!stat.isSymbolicLink(),'PRODUCER_NOT_REGULAR');need(stat.size<=MAX_SOURCE,'PRODUCER_TOO_LARGE');
 const relative=path.relative(fs.realpathSync(repo),fs.realpathSync(file));need(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'PRODUCER_ESCAPED_ROOT');return {...parseLifecycleLogData(fs.readFileSync(file),spec),path:file};
}
