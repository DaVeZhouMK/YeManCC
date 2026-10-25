import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.env.YMCC_MSI_SERVICE_MUTATION_OUT || 'G:/YeManCC-Work/Build/Validation/MSI-Shield-R2-20261013/service-mutations');fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync(path.join(root,'native/msi_service_observation.h'),'utf8');
const mutants=[
 ['write-rights','SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS','SERVICE_ALL_ACCESS'],
 ['discovery-authority','{"discoveryAuthorizesMutation", false}','{"discoveryAuthorizesMutation", true}'],
 ['controller-role-overclaim','{"controllerOnlyRoleVerified", false}','{"controllerOnlyRoleVerified", true}'],
 ['missing-readback-overclaim','result["existsKnown"] = error == ERROR_SERVICE_DOES_NOT_EXIST;','result["existsKnown"] = true;'],
 ['microsoft-prefix-overreach','prefix(name, L"MSI ")','prefix(name, L"Microsoft") || prefix(name, L"MSI ")'],
];
const results=[];
for(const [name,from,to] of mutants){assert(source.includes(from),'mutation anchor exists '+name);const header=path.join(out,name+'.h');fs.writeFileSync(header,source.replace(from,to));const child=spawnSync(process.execPath,[path.join(root,'tools/msi_service_observation_selftest.mjs')],{cwd:root,encoding:'utf8',env:{...process.env,YMCC_MSI_SERVICE_OBSERVATION_HEADER:header,YMCC_MSI_SERVICE_OBSERVATION_OUT:path.join(out,name)}});fs.writeFileSync(path.join(out,name+'.log'),(child.stdout||'')+(child.stderr||''));const resultPath=path.join(out,name,'results.txt');const text=fs.existsSync(resultPath)?fs.readFileSync(resultPath,'utf8'):'';const caught=child.status!==0&&/^FAIL /m.test(text);results.push({name,caught,fixtureRan:text.includes('liveServiceCalls=0'),exitCode:child.status});console.log((caught?'PASS ':'FAIL ')+name+' rejected by real-method fixture');}
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({mutants:results,liveMutations:0},null,2)+'\n');assert(results.every(r=>r.caught&&r.fixtureRan),'All deliberately bad behaviors must fail runtime assertions, not only compilation');
