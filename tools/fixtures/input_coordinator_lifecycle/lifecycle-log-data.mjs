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
 const relative=path.relative(fs.realpathSync(repo),fs.realpathSync(file));need(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'PRODUCER_ESCAPED_ROOT');return {...(spec?.schemaVersion===2?parseLifecycleLogDataV2:parseLifecycleLogData)(fs.readFileSync(file),spec),path:file};
}

// Version2 is only the reviewed default core recipe. It verifies the entire
// producer and BOTH literals before selecting its base replacement. The second
// row belongs to --case sd-off-source-lost, which this native entry cannot select.
// The original version1 decoder and its one-row rejection remain unchanged.
export function parseLifecycleLogDataV2(input,spec){
 need(spec?.schemaVersion===2&&spec.relative===LIFECYCLE_LOG_PRODUCER&&spec.key==='appendNativeLifecycleLog'&&
  spec.selectionContract==='default-core-no-directed-sd-v1'&&spec.expectedLiteralRows===2&&spec.selectedLiteral===0&&
  Array.isArray(spec.literals)&&spec.literals.length===2,'VERSION2_EXPLICIT_SELECTION_REQUIRED');
 const bytes=Buffer.isBuffer(input)?input:Buffer.from(input,'utf8');need(bytes.length<=MAX_SOURCE,'PRODUCER_TOO_LARGE');
 need(Number.isSafeInteger(spec.bytes)&&bytes.length===spec.bytes&&/^[a-f0-9]{64}$/.test(spec.sha256??'')&&hash(bytes)===spec.sha256,'VERSION2_PRODUCER_PIN_CHANGED');
 const source=bytes.toString('utf8'),hits=[...source.matchAll(/\[\s*['"]appendNativeLifecycleLog['"]\s*,\s*/g)];
 need(hits.length===2,'VERSION2_EXACT_TWO_LITERAL_ROWS_REQUIRED');
 const expectedRoles=['base-default','sd-off-source-lost-only'],parsed=[];
 for(let index=0;index<2;index++){
  const row=spec.literals[index],offset=Buffer.byteLength(source.slice(0,hits[index].index),'utf8');
  need(row?.role===expectedRoles[index]&&row.sourceUtf8Byte===offset&&Number.isSafeInteger(row.sourceUtf8Bytes)&&row.sourceUtf8Bytes>0&&
   offset+row.sourceUtf8Bytes<=bytes.length&&/^[a-f0-9]{64}$/.test(row.rowSha256??''),'VERSION2_LITERAL_LOCATION_REQUIRED');
  const raw=bytes.subarray(offset,offset+row.sourceUtf8Bytes);need(hash(raw)===row.rowSha256,'VERSION2_LITERAL_PIN_CHANGED');
  parsed.push(parseLifecycleLogData(raw,{schemaVersion:1,relative:spec.relative,key:spec.key,
   decodedUtf8Bytes:row.decodedUtf8Bytes,decodedUtf8Sha256:row.decodedUtf8Sha256}));
 }
 need(parsed[0].decodedUtf8Bytes===spec.decodedUtf8Bytes&&parsed[0].decodedUtf8Sha256===spec.decodedUtf8Sha256,'VERSION2_SELECTED_DATA_PIN_CHANGED');
 return {...parsed[0],wholeSourceSha256:hash(bytes),producerExecuted:false,selectedDataRole:'base-default',
  verifiedConditionalLiteral:parsed[1].decodedUtf8Sha256,selectionContract:spec.selectionContract};
}
