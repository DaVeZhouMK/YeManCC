import test from "node:test";
import assert from "node:assert/strict";
import { promises as fs } from "node:fs";
import path from "node:path";
import os from "node:os";
import { fileURLToPath } from "node:url";
import { compareRuntimeCompatibility, runRuntimeConnectivityRegressions } from "./runtime-connectivity.mjs";
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../../..");
const temporaryParent = path.resolve(os.tmpdir());
async function isolated(action) {
  const root = await fs.mkdtemp(path.join(temporaryParent, "ymcc-runtime-contract-"));
  try { return await action(root); }
  finally {
    const resolved = path.resolve(root), relative = path.relative(temporaryParent, resolved);
    assert.ok(relative && !relative.startsWith("..") && !path.isAbsolute(relative) && path.basename(resolved).startsWith("ymcc-runtime-contract-"));
    await fs.rm(resolved, { recursive: true, force: true });
  }
}
const identity = { assemblyName: "Example", assemblyIdentity: "Example, Version=1.0.0.0", sha256: "a".repeat(64), moduleVersionId: "00000000-0000-0000-0000-000000000001", bytes: 10 };
function compatible() { return { schemaVersion: 1, runtimeId: "test-candidate", coreIdentity: { ...identity }, requiredInterfaces: ["required(real,input)"], shared: [{ file: "Example.dll", identity: { ...identity } }] }; }

test("same completed capability snapshot is compatible", () => {
  assert.deepEqual(compareRuntimeCompatibility({ status: "PASS", compatibility: compatible() }, { status: "PASS", compatibility: compatible() }), { status: "COMPATIBLE", differences: [] });
});
test("changed assembly version, bytes or required API produce precise incompatibility", () => {
  const candidate = compatible(); candidate.shared[0].identity.assemblyIdentity = "Example, Version=2.0.0.0";
  candidate.coreIdentity.sha256 = "b".repeat(64); candidate.requiredInterfaces = ["changed-api"];
  const result = compareRuntimeCompatibility(compatible(), candidate);
  assert.equal(result.status, "INCOMPATIBLE");
  assert.deepEqual(result.differences.map(item => item.field), ["requiredInterfaces", "coreIdentity", "shared.Example.dll"]);
});
test("missing receipt fields and failed gate never become compatible", () => {
  assert.equal(compareRuntimeCompatibility({ schemaVersion: 1 }, { schemaVersion: 1 }).status, "INCOMPATIBLE");
  assert.equal(compareRuntimeCompatibility({ status: "FAIL", compatibility: compatible() }, { status: "PASS", compatibility: compatible() }).status, "INCOMPATIBLE");
  const duplicate = compatible(); duplicate.shared.push(duplicate.shared[0]);
  assert.equal(compareRuntimeCompatibility(compatible(), duplicate).status, "INCOMPATIBLE");
});
test("a missing real payload is BLOCKED with a fresh explicit receipt", async () => isolated(async root => {
  const evidenceDir = path.join(root, "missing");
  const result = await runRuntimeConnectivityRegressions({ repoRoot, evidenceDir, runtimeRoot: path.join(root, "absent-runtime") });
  assert.equal(result.status, "BLOCKED"); assert.equal(result.failure.code, "REQUIRED_REAL_PAYLOAD_MISSING");
  assert.equal(result.executedProductModules.length, 0); assert.equal(result.checks.length, 0);
  const persisted = JSON.parse(await fs.readFile(path.join(evidenceDir, "result.json"), "utf8"));
  assert.equal(persisted.status, "BLOCKED");
  await assert.rejects(runRuntimeConnectivityRegressions({ repoRoot, evidenceDir }), error => error.code === "EEXIST");
}));
test("missing PS7 cannot use a mock CLR or old successful evidence", async () => isolated(async root => {
  const result = await runRuntimeConnectivityRegressions({ repoRoot, evidenceDir: path.join(root, "missing-pwsh"), pwshPath: path.join(root, "no-such-pwsh.exe") });
  assert.equal(result.status, "BLOCKED"); assert.equal(result.failure.code, "POWERSHELL_OR_OWNED_JOB_UNAVAILABLE");
  assert.equal(result.process.launchError, "ENOENT"); assert.equal(result.checks.length, 0);
}));
test("an exited process without CLR receipt never passes", async () => isolated(async root => {
  const result = await runRuntimeConnectivityRegressions({ repoRoot, evidenceDir: path.join(root, "no-receipt"), pwshPath: process.execPath });
  assert.equal(result.status, "BLOCKED"); assert.notEqual(result.process.exitCode, 0); assert.equal(result.checks.length, 0);
}));
test("real bounded owned-job timeout remains BLOCKED", async () => isolated(async root => {
  const result = await runRuntimeConnectivityRegressions({ repoRoot, evidenceDir: path.join(root, "timeout"), timeoutMs: 1000 });
  assert.equal(result.status, "BLOCKED"); assert.equal(result.failure.code, "REAL_CLR_RUN_TIMEOUT");
  assert.equal(result.process.timedOut, true); assert.equal(result.checks.length, 0);
}));
test("source tree output and unbounded timeouts are refused before execution", async () => {
  await assert.rejects(runRuntimeConnectivityRegressions({ repoRoot, evidenceDir: path.join(repoRoot, "must-not-create") }), error => error.code === "EVIDENCE_MUST_BE_OUTSIDE_REPOSITORY");
  await assert.rejects(runRuntimeConnectivityRegressions({ repoRoot, evidenceDir: path.join(temporaryParent, "must-not-create"), timeoutMs: 0 }), error => error.code === "BOUNDED_TIMEOUT_REQUIRED");
});
