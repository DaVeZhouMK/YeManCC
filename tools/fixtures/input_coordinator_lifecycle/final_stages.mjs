import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
const sha = text => createHash('sha256').update(text).digest('hex');
const ENTRY = 'int wmain(int argc,wchar_t**argv){try{';
const START = 'fspath::create_directories(g_yemanccDataDir);fixtureTargetReturnIndependentCase();';
const REPORT = ' const auto report=json{{"status",ok?"PASS":"FAIL"},{"checks",checks},{"assertionTrace",assertionTrace},{"orderedReturn",fixtureCurrentOrderedReturn},';
const WRITE = ' std::ofstream(fspath::path(g_yemanccDataDir)/L"fixture-result.json")<<report.dump(2);';
const RETURN = 'return ok&&forbiddenIo==0?0:1;';
// A private diagnostic stream only. It does not change assertions, report fields,
// native process admission, semantic/group/child oracles, or any timer/deadline.
// The synchronous observer has measurable overhead. No timing equivalence claimed.
const HELPER = String.raw`// BEGIN YMCC_PRIVATE_FINAL_STAGE_OBSERVER_V2
#include <cerrno>
static unsigned fixtureFinalStageLost=0;
struct FixtureFinalStageErrorState {
 DWORD lastError;int standardError;
 FixtureFinalStageErrorState() noexcept:lastError(GetLastError()),standardError(errno){}
 ~FixtureFinalStageErrorState() noexcept{errno=standardError;SetLastError(lastError);}
};
static void fixtureFinalStage(const char* stage,unsigned long long bytes=0) noexcept {
 FixtureFinalStageErrorState preserveErrorState;
 try {
  LARGE_INTEGER qpc{},frequency{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&frequency);
  static std::ofstream log{fspath::path(g_yemanccDataDir)/L"native-final-stages.jsonl",std::ios::binary|std::ios::out};
  if(!log){++fixtureFinalStageLost;return;}
  log<<"{\"schemaVersion\":1,\"stage\":\""<<stage<<"\",\"pid\":"<<GetCurrentProcessId()<<",\"tid\":"<<GetCurrentThreadId()<<",\"tickMs\":"<<GetTickCount64()<<",\"qpc\":\""<<qpc.QuadPart<<"\",\"qpcFrequency\":\""<<frequency.QuadPart<<"\",\"bytes\":"<<bytes<<",\"lost\":"<<fixtureFinalStageLost<<"}\n";
  log.flush();if(!log)++fixtureFinalStageLost;
 }catch(...){++fixtureFinalStageLost;}
}
template<class F> static auto fixtureFinalSerialize(F&& produce){
 fixtureFinalStage("serialize-start");
 try{auto bytes=produce();fixtureFinalStage("serialize-end",static_cast<unsigned long long>(bytes.size()));return bytes;}
 catch(...){fixtureFinalStage("serialize-throw");throw;}
}
// END YMCC_PRIVATE_FINAL_STAGE_OBSERVER_V2
`;
const count=(s,x)=>s.split(x).length-1;
function one(s,x,label){assert.equal(count(s,x),1,'EXACT_'+label+'_REQUIRED');}
export function observeFinalStages(original){
 assert.equal(typeof original,'string');
 assert(!original.includes('fixtureFinalStage'),'FINAL_STAGE_OBSERVER_ALREADY_PRESENT');
 for(const [s,label] of [[ENTRY,'ENTRY'],[START,'START'],[REPORT,'ORDERED_REPORT'],[WRITE,'WRITE'],[RETURN,'RETURN']])one(original,s,label);
 const newline=original.includes('\r\n')?'\r\n':'\n';
 const helper=HELPER.replace(/\r?\n/g,newline);
 const changes=[
  [ENTRY,helper+ENTRY],
  [START,'fspath::create_directories(g_yemanccDataDir);fixtureFinalStage("body-start");fixtureTargetReturnIndependentCase();'],
  [REPORT,' fixtureFinalStage("body-return");'+newline+REPORT],
  [WRITE,' fixtureFinalStage("report-built");'+newline+' fixtureFinalStage("report-write-start");'+newline+' std::ofstream(fspath::path(g_yemanccDataDir)/L"fixture-result.json")<<fixtureFinalSerialize([&](){return report.dump(2);});fixtureFinalStage("report-write-end");'],
  [RETURN,'fixtureFinalStage("main-return");'+RETURN]
 ];
 let text=original;
 for(const [from,to] of changes){one(text,from,'FORWARD_MARKER');text=text.replace(from,to);}
 let restored=text;
 for(const [from,to] of changes.toReversed()){one(restored,to,'REVERSE_MARKER');restored=restored.replace(to,from);}
 assert.equal(restored,original,'EXACT_ORIGINAL_RESTORE_REQUIRED');
 return {text,originalSha256:sha(original),observedSha256:sha(text),wholeOriginalRestored:true,originalAssertionsUnchanged:true,originalReportExpressionUnchanged:true,nativeBudgetMs:60000,budgetChanged:false,acceptanceChanged:false,originalSourceModified:false,diagnosticOnly:true,observerTimingTransparent:false,scope:'Private opt-in final-stage stream. The original full report expression, report.dump(2), checks, ordered cases, process semantics and admission remain. Missing stages/lost observations are UNKNOWN; a main-return marker is not natural process exit or resource destruction.'};
}
export function makePrivateFixture(input,output){
 input=path.resolve(input);output=path.resolve(output);
 assert(input!==output,'PRIVATE_OUTPUT_MUST_DIFFER');
 assert(!fs.existsSync(output),'FRESH_OUTPUT_REQUIRED');
 const data=fs.readFileSync(input);const text=data.toString('utf8');assert(Buffer.from(text,'utf8').equals(data),'UTF8_SOURCE_REQUIRED');
 const observed=observeFinalStages(text);
 fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,observed.text,{flag:'wx'});
 const {text:omitted,...receipt}=observed;
 const result={schemaVersion:1,source:input,output,...receipt};
 fs.writeFileSync(output+'.observation.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});
 return result;
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
 assert.equal(process.argv.length,4,'Usage: node observe-final-stages.mjs INPUT_CPP FRESH_PRIVATE_CPP');
 console.log(JSON.stringify(makePrivateFixture(process.argv[2],process.argv[3])));
}
