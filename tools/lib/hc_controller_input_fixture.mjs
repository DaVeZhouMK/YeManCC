// Test-only closure for extracted real controller consumers. Product source is never rewritten.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
export const controllerInputFixtureInputs=['capabilities/HcControllerInputCapabilityCatalog.h','capabilities/HcControllerInputCapabilityCatalog.generated.h','tools/lib/hc_controller_input_fixture.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
export function snapshotControllerInputFixture(repo,directory,read=file=>fs.readFileSync(file)) {
 fs.mkdirSync(directory,{recursive:true});const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),inputs=[];
 for(const file of controllerInputFixtureInputs) {const bytes=read(path.join(repo,file));inputs.push({path:file,sha256:sha(bytes)});if(file.endsWith('.h'))fs.writeFileSync(path.join(directory,path.basename(file)),bytes);}
 const header=path.join(directory,'HcControllerInputCapabilityCatalog.h').replaceAll('\\','/');
 return {inputs,preamble:'#include '+JSON.stringify(header)+'\n'};
}

export function verifyControllerInputFixture(repo,snapshot) {
 const changed=snapshot.inputs.filter(row=>createHash('sha256').update(fs.readFileSync(path.join(repo,row.path))).digest('hex')!==row.sha256);
 if(changed.length)throw Error('CONTROLLER_INPUT_FIXTURE_DEPENDENCIES_CHANGED');
 return true;
}
