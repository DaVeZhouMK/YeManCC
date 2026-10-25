import fs from 'node:fs';
import {closeBrace} from '../hc_remaining_model_capabilities/expectations.mjs';
import {FIXTURE,need,once} from './contract.mjs';
export function xboxCombinedTests(sourceSet){
 const raw=name=>fs.readFileSync(sourceSet.resolve(FIXTURE+'/'+name),'utf8');
 const original=raw('CombinedMotorTests.cpp'),xbox=raw('XboxCombinedMotorTests.cpp');
 const marker='static void combinedCase(';const start=xbox.indexOf(marker);need(start>=0&&xbox.indexOf(marker,start+1)<0,'D12_COMBINED_CASE_UNIQUE');
 const helper=xbox.slice(start,closeBrace(xbox,xbox.indexOf('{',start))+1);
 const mainStart=xbox.indexOf('int wmain(');need(mainStart>=0&&xbox.indexOf('int wmain(',mainStart+1)<0,'D12_MAIN_UNIQUE');let main=xbox.slice(mainStart);
 // Preserve all original Sony IDs and dispatch its byte-unchanged helper for the first25 cases.
 main=once(main,'"unsupported-elite-hid-source"','"unsupported-profile"','original-Sony-unsupported-case-id');
 main=once(main,')combinedCase(argv[1],argv[2],root/"xbox",',')xboxCombinedCase(argv[1],argv[2],root/"xbox",','Xbox-only-extended-combined-helper');
 return '#define wmain FixtureUnusedOriginalSonyMain\n'+original+'\n#undef wmain\n#define combinedCase xboxCombinedCase\n'+helper+'\n#undef combinedCase\n#define wmain D12UnusedFrameAdmissionEntry\n'+raw('FrameAdmissionControls.cpp')+'\n#undef wmain\n#define wmain D12PersonaAdmissionEntry\n'+raw('PersonaAdmissionControls.cpp')+'\n#undef wmain\n'+main;
}
