import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';
const bytes=fs.readFileSync(new URL('./current-case-shape.json',import.meta.url));
const sha=value=>createHash('sha256').update(value).digest('hex');
assert.equal(sha(bytes),'76f9125abc68d74ac6f8b52c925486840965e6303524bd995357035aa773b268','XINPUT_REVIEWED_ORIGINAL_CASE_SHAPE_CHANGED');
const spec=JSON.parse(bytes),project=cases=>cases.map(c=>({name:c.name,checks:c.checks.map(({name,expected})=>({name,expected}))}));
export function verifyExpandedCurrentCaseShape(report,sourceRoot){
 assert.equal(report.caseCount,spec.current.cases,'Original64 plus reviewed touch9 and explicit manual cases required');assert.equal(report.checks,spec.current.checks,'Original299 plus reviewed touch23 and explicit manual checks required');
 assert.deepEqual(project(report.cases.slice(0,spec.original.cases)),spec.original.shape,'Every original case/check name, expected value and order must remain');
 assert.equal(report.cases.slice(0,spec.original.cases).reduce((n,c)=>n+c.checks.length,0),spec.original.checks);
 const fixture=fs.readFileSync(path.join(sourceRoot,spec.added.source));assert.equal(sha(fixture),spec.added.sourceSha256,'Expanded fixture source needs explicit review');
 const names=[...fixture.toString('utf8').matchAll(/finish\("(touch-output-[^"]+)"\)/g)].map(m=>m[1]);assert.deepEqual(names,spec.added.names);
 const touch=report.cases.slice(spec.original.cases,spec.original.cases+spec.added.names.length);assert.deepEqual(touch.map(c=>c.name),names);assert.deepEqual(touch.map(c=>c.checks.length),spec.added.checksPerCase);
 assert.equal(sha(fixture),spec.manual.sourceSha256,'Reviewed manual fixture source must remain exact');
 const manual=report.cases.slice(spec.original.cases+spec.added.names.length);
 assert.equal(manual.length,spec.manual.cases);assert.equal(manual.reduce((n,c)=>n+c.checks.length,0),spec.manual.checks);
 assert.deepEqual(project(manual),spec.manual.shape,'Every explicit manual case/check name, expected value and order must remain');
 return {originalCases:64,originalChecks:299,addedCases:9,addedChecks:23,manualCases:spec.manual.cases,manualChecks:spec.manual.checks};
}
