import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
// A short path belongs to one invocation, independently of the formal case's
// potentially long output path. No machine/user TEMP setting is modified.
export function createOwnedWorkspace({repo, output, primary, sourceHashes = {}}) {
  const evidence = path.resolve(repo, '../../../Testing/YMCC/Evidence');
  assert(fs.statSync(evidence).isDirectory(), 'OWNED_EVIDENCE_ROOT_REQUIRED');
  assert.equal(fs.realpathSync(evidence).toLowerCase(), evidence.toLowerCase(), 'OWNED_EVIDENCE_ROOT_REDIRECTED');
  const base = path.join(evidence, 'VC-CaptureReceipt23', 't');
  fs.mkdirSync(base, {recursive: true});
  assert.equal(fs.realpathSync(base).toLowerCase(), base.toLowerCase(), 'OWNED_TEMP_ROOT_REDIRECTED');
  const root = fs.mkdtempSync(path.join(base, 'r-'));
  const owner = {schemaVersion: 1, kind: 'controller-signal-evidence-owned-workspace',
    root, repository: path.resolve(repo), formalOutput: path.resolve(output),
    createdAt: new Date().toISOString(), pid: process.pid, primary, sourceHashes};
  fs.writeFileSync(path.join(root, 'owner.json'), JSON.stringify(owner, null, 2) + '\n', {flag: 'wx'});
  const hash = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const identity = {path: path.join(root, 'owner.json'), sha256: hash(path.join(root, 'owner.json'))};
  let next = 0;
  function allocate(id) {
    assert.equal(hash(identity.path), identity.sha256, 'OWNED_WORKSPACE_IDENTITY_CHANGED');
    const job = path.join(root, 'j' + String(next++).padStart(2, '0'));
    fs.mkdirSync(job);
    const temp = path.join(job, 't');
    fs.mkdirSync(temp);
    const buildOutput = path.join(job, 'o');
    assert(temp.length <= 110 && buildOutput.length <= 110, 'OWNED_WORKSPACE_PATH_TOO_LONG');
    const row = {id, directory: job, temp, tempPathLength: temp.length, buildOutput,
      outputPathLength: buildOutput.length, owner: identity};
    fs.writeFileSync(path.join(job, 'job.json'), JSON.stringify(row, null, 2) + '\n', {flag: 'wx'});
    return {...row, jobIdentity: {path: path.join(job, 'job.json'), sha256: hash(path.join(job, 'job.json'))}};
  }
  function verify(job) {
    assert.equal(hash(identity.path), identity.sha256, 'OWNED_WORKSPACE_IDENTITY_CHANGED');
    if (job) {
      assert.equal(hash(job.jobIdentity.path), job.jobIdentity.sha256, 'OWNED_JOB_IDENTITY_CHANGED');
      assert.equal(fs.realpathSync(job.temp).toLowerCase(), job.temp.toLowerCase(), 'OWNED_JOB_TEMP_REDIRECTED');
    }
  }
  return {root, identity, allocate, verify};
}
