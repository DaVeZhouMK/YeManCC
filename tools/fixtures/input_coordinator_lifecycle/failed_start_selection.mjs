import {createHash} from 'node:crypto';

export const failedStartCase='actual-classified-failed-start-off-on-recovery';
// Includes setup and the queued lifecycle ownership assertion.
const originalAssertionCount=22;
const sha=text=>createHash('sha256').update(text).digest('hex');

// Project only the generated fixture. The real scenario body and all other
// consumers' frozen source bytes stay unchanged.
export function selectFailedStartFixture(text){
 const nl=text.includes('\r\n')?'\r\n':'\n';
 const first=` cases.push_back({{"name","${failedStartCase}"}});`;
 const last=' cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;';
 const next=' for(const bool naturalOrder:{false,true}){';
 const endAnchor=last+nl+next;
 const begin=text.indexOf(first), end=text.indexOf(endAnchor,begin);
 if(begin<0||text.split(first).length!==2||end<begin||text.indexOf(endAnchor)!==end||text.indexOf(endAnchor,end+1)!==-1)
  throw Error('FAILED_START_ORIGINAL_CASE_ANCHORS');
 const body=text.slice(begin,end+last.length);
 const launch=' if(cliBoundaryOnly!=4){'+nl+' {'+nl+' cases.push_back({{"name","central-switch-lifecycle-version-and-single-fallback-clock"}});';
 const block=[' const auto runSelectedFailedStart=[&](){',' fixtureFrameFirstAck=false;',body,' };',
  ' if(cliBoundaryOnly==5){runSelectedFailedStart();return;}',launch].join(nl);
 const edits=[
  ['static void switchRecoveryCases(int cliBoundaryOnly=0){','static void switchRecoveryCases(int cliBoundaryOnly=5){'],
  ['if(cliBoundaryOnly && cliBoundaryOnly!=4){','if(cliBoundaryOnly && cliBoundaryOnly!=4 && cliBoundaryOnly!=5){'],
  [body+nl+next,' runSelectedFailedStart();'+nl+next],
  [launch,block],
 ];
 function replaceUnique(input,before,after){
  if(input.split(before).length!==2)throw Error('FAILED_START_GENERATED_SELECTION_ANCHOR');
  return input.replace(before,()=>after);
 }
 const result=edits.reduce((value,[before,after])=>replaceUnique(value,before,after),text);
 const inverse=[...edits].reverse().reduce((value,[before,after])=>replaceUnique(value,after,before),result);
 if(inverse!==text||result.split(body).length!==2)throw Error('FAILED_START_ORIGINAL_BODY_CHANGED');
 return {text:result,receipt:{symbol:'switchRecoveryCases',selectedCase:failedStartCase,originalCaseSha256:sha(body),
  beforeSha256:sha(text),afterSha256:sha(result),assertionsModified:false,sourceCaptureModified:false}};
}

export function inspectFailedStartSelection(results){
 const [run]=results,raw=run?.result, rows=raw?.cases??[];
 const actualCases=rows.map(row=>row?.name), [row]=rows;
 const timeline=row?.timeline??[], stages=timeline.map(step=>step.stage);
 const expectedStages=['failed-start-candidate','failed-start-at9999','failed-start-at10000-before-reserve','failed-start-fallback-spent','failed-start-healthy-final'];
 const exact=results.length===1&&run?.mode==='switch-recovery'&&actualCases.length===1&&actualCases[0]===failedStartCase;
 const trace=raw?.assertionTrace??[];
 const passed=exact&&run.exitCode===0&&!run.signal&&!run.timedOut&&raw.status==='PASS'&&raw.forbiddenIo===0&&raw.failures?.length===0&&
  raw.checks===originalAssertionCount&&trace.length===raw.checks&&trace.every((check,index)=>check?.case===failedStartCase&&check.checkIndex===index+1&&check.passed===true)&&
  JSON.stringify(stages)===JSON.stringify(expectedStages)&&timeline[4].phase==='success'&&timeline[4].fallbackAttempts===1&&
  row.cleanup?.hostHandle===false&&row.cleanup?.prepared===false&&row.cleanup?.quarantined===false;
 return {status:passed?'PASS':'FAIL',selectedCase:failedStartCase,expectedCases:[failedStartCase],actualCases,exact,semanticPassed:passed,
  checks:raw?.checks??null,stages,terminalCleanup:row?.cleanup??null};
}
