import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';
const bytes=fs.readFileSync(new URL('./current-case-shape.json',import.meta.url));
const sha=value=>createHash('sha256').update(value).digest('hex');
assert.equal(sha(bytes),'08cb9e716e0c06e915566ca775836c467f361f0ba69b12a089d902e8481c601c','XINPUT_REVIEWED_ORIGINAL_CASE_SHAPE_CHANGED');
const spec=JSON.parse(bytes),project=cases=>cases.map(c=>({name:c.name,checks:c.checks.map(({name,expected})=>({name,expected}))}));
export function verifyExpandedCurrentCaseShape(report,sourceRoot){
 assert.equal(report.caseCount,spec.current.cases,'Original64 plus reviewed9 cases required');assert.equal(report.checks,spec.current.checks,'Original299 plus reviewed23 checks required');
 assert.deepEqual(project(report.cases.slice(0,spec.original.cases)),spec.original.shape,'Every original case/check name, expected value and order must remain');
 assert.equal(report.cases.slice(0,spec.original.cases).reduce((n,c)=>n+c.checks.length,0),spec.original.checks);
 const fixture=fs.readFileSync(path.join(sourceRoot,spec.added.source));assert.equal(sha(fixture),spec.added.sourceSha256,'Expanded fixture source needs explicit review');
 const names=[...fixture.toString('utf8').matchAll(/finish\("(touch-output-[^"]+)"\)/g)].map(m=>m[1]);assert.deepEqual(names,spec.added.names);
 const added=report.cases.slice(spec.original.cases);assert.deepEqual(added.map(c=>c.name),names);assert.deepEqual(added.map(c=>c.checks.length),spec.added.checksPerCase);
 return {originalCases:64,originalChecks:299,addedCases:9,addedChecks:23};
}
