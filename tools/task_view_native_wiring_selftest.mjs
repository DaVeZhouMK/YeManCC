import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {parseArgs} from 'node:util';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const {values, positionals} = parseArgs({allowPositionals:true,options:{out:{type:'string'}}});
assert(positionals.length <= 1 && !(values.out && positionals.length), 'Supply one output directory');
const out = path.resolve(values.out || positionals[0] || path.join(root,'../../../Build/Validation/TaskView-Native-Wiring'));
fs.mkdirSync(out,{recursive:true});
const pythonOut = path.join(out,'native-producer');
const run = spawnSync('python',['tools/task_view_api_wiring_selftest.py','--out',pythonOut],
  {cwd:root,encoding:'utf8',windowsHide:true,timeout:60000});
fs.writeFileSync(path.join(out,'stdout.log'),run.stdout || '');
fs.writeFileSync(path.join(out,'stderr.log'),(run.stderr || '') + (run.error ? String(run.error) : ''));
let result = {pass:false,scope:'Actual source-extracted native producer with in-memory dispatcher and OS; no live Windows authorization proof',
  actualOSInput:false,productExecuted:false};
try {
  assert.equal(run.status,0,'Native producer fixture failed; see stdout/stderr logs');
  const receipt = JSON.parse(fs.readFileSync(path.join(pythonOut,'latest.json'),'utf8'));
  assert.equal(receipt.pass,true);
  assert.equal(receipt.realInputOrProductExecuted,false);
  assert.deepEqual(receipt.profiles.map(row=>row.profile),['Release','Checked']);
  assert(receipt.profiles.every(row=>row.pass && row.checks>0));
  result = {...result,pass:true,checks:receipt.profiles.reduce((n,row)=>n+row.checks,0),
    profiles:receipt.profiles,functionHashes:receipt.functionHashes,
    dispatcherHeaderSha256:receipt.dispatcherHeaderSha256,receiptHeaderSha256:receipt.receiptHeaderSha256};
} catch(error) { result.error=String(error); }
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');
fs.writeFileSync(path.join(out,'native-wiring-result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result));
process.exitCode=result.pass?0:1;
