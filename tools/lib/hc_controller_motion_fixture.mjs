// Snapshot the exact total-library and original decoder headers for extracted consumers.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
const sha=b=>createHash('sha256').update(b).digest('hex');
export const controllerMotionFixtureInputs=[
 'capabilities/HcControllerMotionCapabilityCatalog.h','capabilities/HcControllerMotionCapabilityCatalog.generated.h',
 'capabilities/hc-device-catalog.generated.h','native/legion_input_protocol.h',
];
export function snapshotControllerMotionFixture(repo,out){
 const inputs=controllerMotionFixtureInputs.map(file=>{const p=path.join(repo,file),bytes=fs.readFileSync(p),copy=path.join(out,file);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return{file,path:p,copy,sha256:sha(bytes)};});
 return{inputs,preamble:'#include '+JSON.stringify(inputs[0].copy.replaceAll('\\','/'))+'\n'};
}
export function verifyControllerMotionFixture(snapshot){return snapshot.inputs.map(row=>({...row,unchanged:sha(fs.readFileSync(row.path))===row.sha256}));}
