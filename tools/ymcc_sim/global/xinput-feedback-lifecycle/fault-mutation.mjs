// Original source faults only. The exceptional MSI operand is bound to the positive producer's exact function range.
import crypto from 'node:crypto';
const digest=value=>crypto.createHash('sha256').update(value).digest('hex');
const need=(ok,code)=>{if(!ok)throw Error(code);};
function mask(source){return source.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,value=>value.replace(/[^\r\n]/g,' '));}
function boundMsiFunction(source,receipt){
 const bytes=Buffer.from(source),wholeHash=digest(bytes);
 need(receipt?.mode==='production'&&receipt.currentFileSha256===wholeHash,'XINPUT_FAULT_RANGE_SOURCE_CHANGED');
 const ranges=receipt.currentRanges?.filter(row=>row.name==='msiDinputFeedbackWrite'&&row.kind==='function');
 need(ranges?.length===1,'XINPUT_FAULT_FUNCTION_RANGE_NOT_UNIQUE');const row=ranges[0];
 const a=row.start_utf8_byte,b=row.end_utf8_byte_exclusive;
 need(Number.isSafeInteger(a)&&Number.isSafeInteger(b)&&a>=0&&b>a&&b<=bytes.length&&row.source_sha256===wholeHash,'XINPUT_FAULT_FUNCTION_RANGE_INVALID');
 const body=bytes.subarray(a,b).toString('utf8');need(digest(body)===row.sha256,'XINPUT_FAULT_FUNCTION_HASH_CHANGED');
 need(/^static MsiDinputFeedbackResult msiDinputFeedbackWrite\s*\(/.test(body),'XINPUT_FAULT_FUNCTION_IDENTITY_CHANGED');
 const masked=mask(body),open=masked.indexOf('{');need(open>=0,'XINPUT_FAULT_FUNCTION_BODY_MISSING');
 let depth=0,closing=-1;
 for(let i=open;i<masked.length;i++){if(masked[i]==='{')depth++;if(masked[i]==='}'&&--depth===0){closing=i;break;}}
 need(closing===body.length-1,'XINPUT_FAULT_FUNCTION_RANGE_INCOMPLETE');
 return {row,body,masked,sourceStart:bytes.subarray(0,a).toString('utf8').length,sourceEnd:bytes.subarray(0,b).toString('utf8').length};
}
export function mutateXinputSourceFault(source,fault,{sourceRanges}={}){
 need(typeof source==='string'&&typeof fault?.before==='string'&&fault.before.length>0&&typeof fault.after==='string'&&fault.before!==fault.after,'XINPUT_FAULT_DESCRIPTOR_INVALID');
 let start,end,binding=null,functionStart,functionEnd;
 if(fault.id==='msi-final-authority-unchecked'){
  need(fault.file==='native/main.cpp'&&fault.before==='result.sourceStable=hardwareWriteAllowed()&&currentPowerGeneration()==power'&&fault.after==='result.sourceStable=true&&currentPowerGeneration()==power'&&fault.expectedCheck==='real MSI final authority is rechecked after open','XINPUT_ORIGINAL_MSI_FAULT_SEMANTICS_CHANGED');
  const bound=boundMsiFunction(source,sourceRanges);
  const assignments=[...bound.masked.matchAll(/\bresult\.sourceStable\s*=/g)];
  need(assignments.length===1,'XINPUT_MSI_FINAL_ASSIGNMENT_NOT_UNIQUE');
  const assignmentStart=assignments[0].index,assignmentEnd=bound.masked.indexOf(';',assignmentStart);
  need(assignmentEnd>assignmentStart,'XINPUT_MSI_FINAL_ASSIGNMENT_UNTERMINATED');
  const expression=bound.body.slice(assignmentStart,assignmentEnd);
  const anchors=[...mask(expression).matchAll(/\bhardwareWriteAllowed\s*\(\s*\)(?=\s*&&\s*currentPowerGeneration\s*\(\s*\)\s*==\s*power\b)/g)];
  need(anchors.length===1,'XINPUT_MSI_FINAL_AUTHORITY_OPERAND_NOT_UNIQUE');
  start=bound.sourceStart+assignmentStart+anchors[0].index;end=start+anchors[0][0].length;functionStart=bound.sourceStart;functionEnd=bound.sourceEnd;
  binding={assignmentAfter:expression.slice(0,anchors[0].index)+'true'+expression.slice(anchors[0].index+anchors[0][0].length)+';',name:bound.row.name,source_sha256:bound.row.source_sha256,start_utf8_byte:bound.row.start_utf8_byte,end_utf8_byte_exclusive:bound.row.end_utf8_byte_exclusive,sha256:bound.row.sha256,assignmentBefore:expression+';',strategy:'exact positive-producer full function, unique result.sourceStable assignment, hardware authority operand only'};
 }else{
  need(source.split(fault.before).length===2,'XINPUT_ACTUAL_SOURCE_FAULT_ANCHOR_NOT_UNIQUE');start=source.indexOf(fault.before);end=start+fault.before.length;
 }
 const replacement=binding?'true':fault.after,removed=source.slice(start,end),mutated=source.slice(0,start)+replacement+source.slice(end);
 need(mutated!==source,'XINPUT_FAULT_DID_NOT_CHANGE_SOURCE');
 const restored=mutated.slice(0,start)+removed+mutated.slice(start+replacement.length);
 need(restored===source,'XINPUT_FAULT_OUTSIDE_TARGET_CHANGED');
 if(binding)binding.functionAfterSha256=digest(source.slice(functionStart,start)+replacement+source.slice(end,functionEnd));

 return {source:mutated,proof:{id:fault.id,file:fault.file??'native/xinput_identity_refresh_policy.h',expectedCheck:fault.expectedCheck,sourceBeforeSha256:digest(source),sourceAfterSha256:digest(mutated),functionBinding:binding,edit:{start_utf8_byte:Buffer.byteLength(source.slice(0,start)),end_utf8_byte_exclusive:Buffer.byteLength(source.slice(0,end)),before:removed,after:replacement},exactSingleEditRestoresWholeSource:restored===source}};
}
