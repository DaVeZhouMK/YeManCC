import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const sha=text=>createHash('sha256').update(text).digest('hex');
const declaration="static void fixtureRecordCheckProgress(const json& row){\n if(g_yemanccDataDir.empty())throw std::logic_error(\"progress-root-required\");\n static std::ofstream stream{fspath::path(g_yemanccDataDir)/L\"native-check-progress.jsonl\",std::ios::binary|std::ios::out};\n if(!stream)throw std::runtime_error(\"progress-open-failed\");\n stream<<row.dump()<<'\\n';stream.flush();if(!stream)throw std::runtime_error(\"progress-write-failed\");\n}\n";
const marker='static void check(bool good,const char* name){';
const tail="if(!good)failures.push_back({{\"case\",owner},{\"assertion\",name},{\"sourceOrdinal\",ordinal},{\"nameOccurrence\",occurrence},{\"checkIndex\",checks}});}";
// Opt-in diagnostic endpoint. Each completed original assertion is copied to a
// private cached JSONL log, retaining its order, index, owner and pass/fail value.
export function observeCurrent105Progress(original){
 assert(!original.includes('fixtureRecordCheckProgress'),'CHECK_PROGRESS_ALREADY_PRESENT');
 assert.equal(original.split(marker).length,2,'EXACT_CURRENT105_CHECK_REQUIRED');
 assert.equal(original.split(tail).length,2,'EXACT_CURRENT105_FAILURE_RECORD_REQUIRED');
 const next=tail.slice(0,-1)+'fixtureRecordCheckProgress(assertionTrace.back());}';
 const text=original.replace(marker,declaration+marker).replace(tail,next);
 assert.equal(text.replace(declaration+marker,marker).replace(next,tail),original);
 return {text,originalSha256:sha(original),observedSha256:sha(text),wholeOriginalRestored:true,originalAssertionsUnchanged:true,scope:'Only copy the completed original assertion row to a private progress file; no report acceptance, assertion, clock, budget or product method replacement.'};
}
