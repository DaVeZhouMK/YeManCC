import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {observeFinalStages} from './final_stages.mjs';
import {observeCurrent105Fixture} from '../../input_coordinator_current105_source_adapters.mjs';
import {observeCurrent105Progress} from './check_progress.mjs';
import {buildCurrentCompileOnly} from '../../input_coordinator_current_compile_only.mjs';
const sha=s=>createHash('sha256').update(s).digest('hex');
const actual=observeCurrent105Fixture(fs.readFileSync(new URL('./fixture.cpp',import.meta.url),'utf8')).text;
function verified(source){const o=observeFinalStages(source);assert.equal(o.originalSha256,sha(source));assert.equal(o.observedSha256,sha(o.text));assert.equal(o.wholeOriginalRestored,true);assert.equal(o.originalAssertionsUnchanged,true);assert.equal(o.originalReportExpressionUnchanged,true);assert.equal(o.nativeBudgetMs,60000);assert.equal(o.observerTimingTransparent,false);assert.equal(o.acceptanceChanged,false);assert.equal(o.budgetChanged,false);assert(o.text.includes('return report.dump(2);'));return o;}
test('actual original ordered105 fixture receives reversible stage observations',()=>verified(actual));
test('stage observations compose with exact completed-assertion progress',()=>verified(observeCurrent105Progress(actual).text));
test('stage observations preserve actual CRLF source contract',()=>verified(actual.replace(/\r?\n/g,'\r\n')));
test('duplicate injection is refused',()=>assert.throws(()=>observeFinalStages(verified(actual).text),/ALREADY_PRESENT/));
test('ambiguous report expression is refused',()=>assert.throws(()=>observeFinalStages(actual+'\n'+actual),/EXACT_/));
test('legacy non-ordered report cannot bypass current fixture gate',()=>assert.throws(()=>observeFinalStages(fs.readFileSync(new URL('./fixture.cpp',import.meta.url),'utf8')),/ORDERED_REPORT/));
test('nonboolean producer observation is rejected before authority or filesystem work',()=>{for(const finalStages of ['true',null,1,{}])assert.throws(()=>buildCurrentCompileOnly({finalStages}),/BOOLEAN_FINAL_STAGES_REQUIRED/);});
