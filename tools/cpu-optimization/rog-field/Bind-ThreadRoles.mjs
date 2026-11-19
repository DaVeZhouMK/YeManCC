// Explicit, one-shot development intake. No app launch, thread query or sampling.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRoleCohort} from './thread-role-binding.mjs';

const names = new Set(['--identity', '--discovery', '--log', '--opening-intake', '--out']);
const args = new Map();
for (let i = 2; i < process.argv.length; i += 2) {
  const key = process.argv[i], value = process.argv[i + 1];
  if (!names.has(key) || args.has(key) || !value) throw new Error('Expected unique --identity --discovery --log --opening-intake --out paths');
  args.set(key, path.resolve(value));
}
if (args.size !== 5) throw new Error('All five explicit paths are required');
const pin = file => {
  const value = fs.readFileSync(file);
  return {path: file, bytes: value.length,
    sha256: crypto.createHash('sha256').update(value).digest('hex')};
};
const inputs = ['--identity', '--discovery', '--log', '--opening-intake'].map(key => pin(args.get(key)));
if (new Set(inputs.map(row => row.path.toLowerCase())).size !== 4 ||
    inputs.some(row => row.bytes > 16 * 1024 * 1024)) throw new Error('Invalid input identity or size');
const read = key => JSON.parse(fs.readFileSync(args.get(key), 'utf8').replace(/^\uFEFF/, ''));
const identity = read('--identity'), opening = read('--opening-intake');
const log = inputs.find(row => row.path === args.get('--log'));
const source = opening.logs?.find(row => row.snapshot &&
  path.resolve(row.snapshot.path).toLowerCase() === log.path.toLowerCase());
const sourceAt = Date.parse(source?.atUtc), cohortAt = Date.parse(identity.atUtc);
if (!source || source.snapshot.bytes !== log.bytes || source.snapshot.sha256 !== log.sha256 ||
    !Number.isFinite(sourceAt) || !Number.isFinite(cohortAt) || sourceAt > cohortAt) {
  throw new Error('Opening log snapshot identity or before-cohort observation proof missing');
}
const cohort = createRoleCohort(identity, read('--discovery'));
const eligible = [], rejected = [];
let malformedLines = 0, roleRecords = 0;
for (const line of fs.readFileSync(args.get('--log'), 'utf8').split(/\r?\n/)) {
  if (!line.trim()) continue;
  let event;
  try { event = JSON.parse(line); } catch { ++malformedLines; continue; }
  if (event?.event !== 'thread-role') continue;
  ++roleRecords;
  const result = cohort.bind(event);
  (result.accepted ? eligible : rejected).push(result);
}
for (const expected of inputs) {
  const current = pin(expected.path);
  if (current.bytes !== expected.bytes || current.sha256 !== expected.sha256) throw new Error('Input changed during intake');
}
const result = {atUtc: new Date().toISOString(), status: 'ROLE_IDENTITY_INTAKE_ONLY',
  eligible, rejected, malformedLines, roleRecords, sourceFiles: inputs,
  functionStacksCollected: false, cpuAcceptance: false, applicationControl: false};
fs.writeFileSync(args.get('--out'), JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: result.status, eligible: eligible.length,
  rejected: rejected.length, malformedLines}));
