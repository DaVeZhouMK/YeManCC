import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const powershell=process.env.YMCC_COMPLETE_HOST_TEST_POWERSHELL??path.join(os.homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe');
let result;
function actualManifestCases(){
 if(result)return result;
 assert(fs.existsSync(powershell),'Actual PowerShell7 is required for the manifest preflight contract tests');
 const p=spawnSync(powershell,['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(here,'manifest-contract-tests.ps1'),'-SourceFile',path.join(here,'Run-CompleteHost.ps1'),'-ContractFile',path.join(here,'fixtures/98-success-contract.json'),'-ManifestFile',path.join(here,'fixtures/98-success/payload-manifest.json')],{encoding:'utf8',windowsHide:true,timeout:15000});
 assert.equal(p.status,0,p.stderr);assert.equal(p.signal,null);result=JSON.parse(p.stdout.replace(/^\uFEFF/,''));assert.equal(result.status,'PASS');assert.equal(result.hostExecutions,0);assert.equal(result.testCount,8);return result;
}
for(const name of ['actual-reviewed-schema2','old-unreviewed-schema1','unknown-schema3','missing-member','duplicate-member','wrong-hash','self-manifest-cannot-replace-runtime','path-traversal'])test('actual PowerShell manifest preflight: '+name,()=>{
 const r=actualManifestCases(),c=r.cases.find(x=>x.name===name);assert(c);assert.equal(c.passed,true);assert.equal(c.actualRejected,name!=='actual-reviewed-schema2');
});
