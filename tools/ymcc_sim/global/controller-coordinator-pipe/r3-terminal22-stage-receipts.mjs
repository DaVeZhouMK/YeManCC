// Read-only stage evidence. A complete stage sequence never approves exit or cleanup.
import fs from 'node:fs';import path from 'node:path';import{createHash}from'node:crypto';
export const FINAL_STAGE_MAX_BYTES=16*1024;
const steps=Object.freeze(['body-start','body-return','report-built','report-write-start',
 'serialize-start','serialize-end','report-write-end','main-return']);
const failure=reason=>({status:'UNKNOWN',reason,naturalExitProven:false,resourceReleaseProven:false,acceptanceGranted:false});
export function parseFinalStages(text,expectedPid){
 try{
  if(typeof text!=='string'||Buffer.byteLength(text)>FINAL_STAGE_MAX_BYTES||!Number.isSafeInteger(expectedPid)||expectedPid<=0)throw Error('STAGE_INPUT_INVALID');
  if(!text.trim())return failure('STAGE_EMPTY');
  const lines=text.split(/\r?\n/);if(lines.at(-1)==='')lines.pop();
  if(lines.length>9)throw Error('STAGE_ROW_LIMIT');
  const rows=[];let cursor=0,thrown=false;
  for(const line of lines){
   if(!line)throw Error('STAGE_EMPTY_ROW');const row=JSON.parse(line);
   if(row.schemaVersion!==1||row.pid!==expectedPid||!Number.isSafeInteger(row.tid)||row.tid<=0||!Number.isSafeInteger(row.tickMs)||row.tickMs<0||!Number.isSafeInteger(row.bytes)||row.bytes<0||!Number.isSafeInteger(row.lost)||row.lost!==0||typeof row.qpc!=='string'||!/^\d{1,20}$/.test(row.qpc)||typeof row.qpcFrequency!=='string'||!/^\d{1,20}$/.test(row.qpcFrequency)||BigInt(row.qpcFrequency)<=0n)throw Error('STAGE_IDENTITY_OR_CLOCK_UNPROVEN');
   if(rows.length){const old=rows.at(-1);if(row.tid!==old.tid||row.qpcFrequency!==old.qpcFrequency||BigInt(row.qpc)<BigInt(old.qpc)||row.tickMs<old.tickMs)throw Error('STAGE_CLOCK_OR_THREAD_CHANGED');}
   if(thrown)throw Error('STAGE_AFTER_THROW');
   if(row.stage==='serialize-throw'&&steps[cursor]==='serialize-end'){thrown=true;}
   else{if(row.stage!==steps[cursor])throw Error('STAGE_SEQUENCE_CHANGED');++cursor;}
   rows.push(row);
  }
  const elapsed=(from,to)=>{const a=rows.find(r=>r.stage===from),b=rows.find(r=>r.stage===to);if(!a||!b)return null;return Number((BigInt(b.qpc)-BigInt(a.qpc))*1000000n/BigInt(a.qpcFrequency))/1000;};
  const phases={bodyMs:elapsed('body-start','body-return'),reportConstructionMs:elapsed('body-return','report-built'),serializationMs:elapsed('serialize-start','serialize-end'),reportWriteEnvelopeMs:elapsed('report-write-start','report-write-end'),observedThroughMainReturnMs:elapsed('body-start','main-return')};
  return{status:thrown?'OBSERVED_SERIALIZATION_THROW':cursor===steps.length?'OBSERVED_THROUGH_MAIN_RETURN':'OBSERVED_PREFIX',rows,lastStage:rows.at(-1).stage,phases,serializationBytes:rows.find(r=>r.stage==='serialize-end')?.bytes??null,scope:'Only measured original fixture phases. A missing end is not proof of deadlock. main-return is not OS exit, resource destruction, or physical return.',naturalExitProven:false,resourceReleaseProven:false,acceptanceGranted:false};
 }catch(error){return failure('STAGE_INVALID:'+error.message);}
}
export function captureFinalStages(runtimeRoot,expectedPid){
 const file=path.join(runtimeRoot,'native-final-stages.jsonl');
 try{
  const stat=fs.lstatSync(file);if(!stat.isFile()||stat.isSymbolicLink()||stat.size>FINAL_STAGE_MAX_BYTES)return{...failure('STAGE_FILE_NOT_BOUNDED_REGULAR'),path:file};
  const data=fs.readFileSync(file);if(data.length!==stat.size)return{...failure('STAGE_FILE_CHANGED_DURING_READ'),path:file};
  return{...parseFinalStages(data.toString('utf8'),expectedPid),file:{path:file,sha256:createHash('sha256').update(data).digest('hex'),bytes:data.length}};
 }catch(error){return{...failure(error.code==='ENOENT'?'STAGE_FILE_MISSING':'STAGE_READ_FAILED:'+error.code),path:file};}
}
