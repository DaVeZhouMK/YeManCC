import { spawn } from "node:child_process";
import { createHash, randomUUID } from "node:crypto";
import { promises as fs } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const defaultRepo = path.resolve(here, "../../../..");
const defaultPwsh = "C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe";
const sourceFiles = [
  "FanLab/RuntimeDependencyContract.cs", "FanLab/DeviceCoreSharedDependencies.cs",
  "FanLab/real-host/Program.cs", "FanLab/real-host/YeManFanHost.csproj", "FanLab/LightSetter/YeManLightSetter.csproj",
  "deps/ymcc-device-core-runtime/profile.json", "deps/ymcc-device-core-runtime/upstream.runtime.json",
  "tools/hc-device-runtime/verify-runtime.ps1", "tools/ymcc_sim/global/host-process.ps1",
  ...["runtime-connectivity.mjs", "Run-Regression.ps1", "Program.cs", "RuntimeConnectivityProbe.csproj"].map(name => `tools/ymcc_sim/global/runtime-connectivity/${name}`),
];
const sha256 = bytes => createHash("sha256").update(bytes).digest("hex");
const readJson = async file => JSON.parse((await fs.readFile(file, "utf8")).replace(/^\uFEFF/, ""));
async function snapshot(repoRoot, files) {
  return Promise.all(files.map(async relative => {
    try { return { path: relative, sha256: sha256(await fs.readFile(path.resolve(repoRoot, relative))), status: "OK" }; }
    catch (error) { return { path: relative, sha256: null, status: "MISSING", code: error.code }; }
  }));
}
function within(root, target) { const relative = path.relative(root, target); return relative === "" || (!relative.startsWith("..") && !path.isAbsolute(relative)); }
function fault(code, message = code) { return Object.assign(new Error(message), { code }); }

/** Compare reusable capability/identity snapshots; differences never silently pass. */
export function compareRuntimeCompatibility(baseline, candidate) {
  const previous = baseline?.compatibility ?? baseline;
  const current = candidate?.compatibility ?? candidate;
  const differences = [];
  if (!previous || !current || previous.schemaVersion !== 1 || current.schemaVersion !== 1) {
    return { status: "INCOMPATIBLE", differences: [{ field: "schemaVersion", code: "COMPATIBILITY_SCHEMA_MISSING" }] };
  }
  const validIdentity = value => value && typeof value.assemblyIdentity === "string" && value.assemblyIdentity.length > 0 &&
    typeof value.moduleVersionId === "string" && /^[0-9a-f-]{36}$/i.test(value.moduleVersionId) && /^[0-9a-f]{64}$/i.test(value.sha256 ?? "");
  const valid = value => typeof value.runtimeId === "string" && value.runtimeId.length > 0 && validIdentity(value.coreIdentity) &&
    Array.isArray(value.requiredInterfaces) && value.requiredInterfaces.length > 0 && value.requiredInterfaces.every(item => typeof item === "string" && item.length > 0) &&
    Array.isArray(value.shared) && value.shared.length > 0 && new Set(value.shared.map(item => item.file)).size === value.shared.length &&
    value.shared.every(item => typeof item.file === "string" && validIdentity(item.identity));
  if (!valid(previous) || !valid(current) || (baseline?.status && baseline.status !== "PASS") || (candidate?.status && candidate.status !== "PASS"))
    return { status: "INCOMPATIBLE", differences: [{ field: "requiredContract", code: "COMPATIBILITY_CONTRACT_INCOMPLETE_OR_FAILED" }] };
  const compare = (field, left, right) => { if (JSON.stringify(left) !== JSON.stringify(right)) differences.push({ field, baseline: left ?? null, candidate: right ?? null }); };
  compare("runtimeId", previous.runtimeId, current.runtimeId);
  compare("requiredInterfaces", previous.requiredInterfaces, current.requiredInterfaces);
  compare("coreIdentity", previous.coreIdentity, current.coreIdentity);
  const oldLibraries = new Map((previous.shared ?? []).map(item => [item.file, item.identity]));
  const newLibraries = new Map((current.shared ?? []).map(item => [item.file, item.identity]));
  for (const name of [...new Set([...oldLibraries.keys(), ...newLibraries.keys()])].sort()) compare(`shared.${name}`, oldLibraries.get(name), newLibraries.get(name));
  return { status: differences.length ? "INCOMPATIBLE" : "COMPATIBLE", differences };
}

async function executeOwned(pwshPath, helper, script, argsFile, timeoutMs, evidenceDir) {
  return new Promise(resolve => {
    const child = spawn(pwshPath, ["-NoLogo", "-NoProfile", "-NonInteractive", "-File", helper,
      "-ScriptPath", script, "-ArgumentsPath", argsFile], { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
    const result = { pid: child.pid ?? null, spawnedAt: new Date().toISOString(), executable: pwshPath, cleanup: "owned ChildProcess handle; descendants in kill-on-close Windows Job", exitCode: null, timedOut: false, launchError: null, outputTruncated: false };
    let out = "", err = "", finished = false;
    const collect = (current, chunk) => { const next = current + chunk.toString("utf8"); if (Buffer.byteLength(next) > 262144) { result.outputTruncated = true; return next.slice(-131072); } return next; };
    child.stdout.on("data", chunk => { out = collect(out, chunk); });
    child.stderr.on("data", chunk => { err = collect(err, chunk); });
    const timer = setTimeout(() => { result.timedOut = true; child.kill(); }, timeoutMs);
    child.on("error", error => { result.launchError = error.code ?? error.name; });
    child.on("close", async (code, signal) => {
      if (finished) return; finished = true; clearTimeout(timer); result.exitCode = code; result.signal = signal;
      try { await Promise.all([fs.writeFile(path.join(evidenceDir, "runner.stdout.log"), out), fs.writeFile(path.join(evidenceDir, "runner.stderr.log"), err)]); }
      catch (error) { result.launchError ??= error.code ?? "EVIDENCE_WRITE_FAILED"; }
      resolve(result);
    });
  });
}

async function collectBuildInputs(repoRoot) {
  const files = [...sourceFiles, "native/ProcessEfficiency.cs", "FanLab/fan-backend/YeManFanBackend.csproj"];
  async function visit(relative) {
    for (const entry of await fs.readdir(path.join(repoRoot, relative), { withFileTypes: true })) {
      if (["bin", "obj"].includes(entry.name)) continue;
      const name = `${relative}/${entry.name}`;
      if (entry.isDirectory()) await visit(name);
      else if (entry.isFile() && /\.(cs|csproj|props|targets)$/.test(entry.name)) files.push(name);
    }
  }
  await Promise.all([visit("FanLab/real-host"), visit("FanLab/fan-backend")]);
  for (const name of ["Directory.Build.props", "Directory.Build.targets", "global.json", "NuGet.Config"]) {
    try { await fs.access(path.join(repoRoot, name)); files.push(name); } catch { /* Optional repository build inputs. */ }
  }
  return [...new Set(files)].sort();
}
/** Execute real CLR and linked formal product sources; no manager or Host startup. */
export async function runRuntimeConnectivityRegressions(input = {}) {
  const repoRoot = path.resolve(input.repoRoot ?? defaultRepo);
  if (!input.evidenceDir) throw fault("EVIDENCE_DIR_REQUIRED");
  const evidenceDir = path.resolve(input.evidenceDir);
  if (within(repoRoot, evidenceDir)) throw fault("EVIDENCE_MUST_BE_OUTSIDE_REPOSITORY");
  const timeoutMs = input.timeoutMs ?? 120000;
  if (!Number.isInteger(timeoutMs) || timeoutMs < 1000 || timeoutMs > 600000) throw fault("BOUNDED_TIMEOUT_REQUIRED");
  if (!/^[A-Za-z0-9_.-]+$/.test(input.probeAssembly ?? "Newtonsoft.Json")) throw fault("PROBE_ASSEMBLY_NAME_INVALID");
  await fs.mkdir(evidenceDir, { recursive: false });
  const runId = randomUUID();
  const startedAt = new Date().toISOString();
  let report = { schemaVersion: 1, id: "runtime.connectivity-contract", runId, status: "BLOCKED", startedAt,
    sourceChanges: ["FanLab/RuntimeDependencyContract.cs", "FanLab/DeviceCoreSharedDependencies.cs:Resolve hook (parent integration)",
      "FanLab/real-host/YeManFanHost.csproj:linked production module (parent integration)", "FanLab/LightSetter/YeManLightSetter.csproj:linked production module (parent integration)"],
    sourceHashes: [], executedProductModules: [], inputConsumerStateFeedback: [], checks: [], compatibility: null,
    remainingGaps: [], claims: { physicalHardwareValidated: false, operatingSystemSleepPerformed: false, hostStarted: false,
      managerConstructorsExecuted: false, mockHostApiExecuted: false, actualClrRequired: true } };
  try {
    const buildInputs = await collectBuildInputs(repoRoot);
    const before = await snapshot(repoRoot, buildInputs); report.sourceHashes = before;
    if (before.some(item => item.status !== "OK")) throw fault("REQUIRED_PRODUCT_SOURCE_MISSING");
    const profile = await readJson(path.join(repoRoot, "deps/ymcc-device-core-runtime/profile.json"));
    if (!profile.runtimeId || !/^[A-Za-z0-9_.-]+$/.test(profile.runtimeId)) throw fault("RUNTIME_PROFILE_ID_INVALID");
    const runtimeRoot = path.resolve(input.runtimeRoot ?? path.join(repoRoot, "PowerControl/handheldcompanion-runtime", profile.runtimeId));
    const sharedRuntimeRoot = path.resolve(input.sharedRuntimeRoot ?? path.join(repoRoot, "PowerControl/ymcc-shared-runtime"));
    const hostPayloadRoot = path.resolve(input.hostPayloadRoot ?? path.join(repoRoot, "PowerControl/fan-host"));
    report.inputs = { runtimeRoot, sharedRuntimeRoot, hostPayloadRoot, probeAssembly: input.probeAssembly ?? "Newtonsoft.Json", timeoutMs };
    const payloadFiles = [path.join(runtimeRoot, "HandheldCompanion.runtime.json"), path.join(runtimeRoot, "HandheldCompanion.dll"),
      path.join(hostPayloadRoot, "YeManFanHost.payload.json"), path.join(hostPayloadRoot, "YeManFanHost.dll")];
    for (const file of payloadFiles) { try { await fs.access(file); } catch { throw fault("REQUIRED_REAL_PAYLOAD_MISSING"); } }
    const runtimeManifest = await readJson(path.join(runtimeRoot, "HandheldCompanion.runtime.json"));
    for (const item of runtimeManifest.files ?? []) {
      const name = item.path;
      if (typeof name !== "string" || !within(runtimeRoot, path.resolve(runtimeRoot, name))) throw fault("UNSAFE_RUNTIME_MANIFEST_PATH");
      payloadFiles.push(path.resolve(runtimeRoot, name));
    }
    for (const item of runtimeManifest.sharedDependencies?.files ?? []) {
      if (typeof item.path !== "string" || path.basename(item.path) !== item.path) throw fault("UNSAFE_SHARED_MANIFEST_PATH");
      payloadFiles.push(path.join(sharedRuntimeRoot, item.path));
    }
    const initialPayloadHashes = await snapshot(repoRoot, [...new Set(payloadFiles)]);
    if (initialPayloadHashes.some(item => item.status !== "OK")) throw fault("REQUIRED_REAL_PAYLOAD_MISSING");
    report.payloadHashes = initialPayloadHashes;
    const testArgs = ["-RepoRoot", repoRoot, "-EvidenceDir", evidenceDir, "-RuntimeRoot", runtimeRoot,
      "-SharedRuntimeRoot", sharedRuntimeRoot, "-HostPayloadRoot", hostPayloadRoot, "-ProbeAssembly", input.probeAssembly ?? "Newtonsoft.Json"];
    const argsFile = path.join(evidenceDir, "owned-arguments.json"); await fs.writeFile(argsFile, JSON.stringify(testArgs));
    report.process = await executeOwned(input.pwshPath ?? defaultPwsh, path.join(repoRoot, "tools/ymcc_sim/global/host-process.ps1"),
      path.join(here, "Run-Regression.ps1"), argsFile, timeoutMs, evidenceDir);
    if (report.process.timedOut) throw fault("REAL_CLR_RUN_TIMEOUT");
    if (report.process.launchError) throw fault("POWERSHELL_OR_OWNED_JOB_UNAVAILABLE", report.process.launchError);
    if (report.process.outputTruncated) throw fault("RUN_OUTPUT_TRUNCATED");
    const verification = await readJson(path.join(evidenceDir, "verification.json"));
    let probe = null;
    try { probe = await readJson(path.join(evidenceDir, "probe-result.json")); } catch { /* A missing CLR receipt cannot pass. */ }
    report.status = verification.status === "PASS" && report.process.exitCode === 0 && probe?.status === "PASS" ? "PASS"
      : verification.status === "BLOCKED" ? "BLOCKED" : "FAIL";
    report.stage = verification.stage; report.failure = verification.failure;
    report.checks = probe?.checks ?? verification.checks ?? [];
    report.executedProductModules = probe?.executedProductModules ?? [];
    report.inputConsumerStateFeedback = report.checks.map(item => ({ id: item.id, ...item.inputConsumerStateFeedback }));
    report.compatibility = probe?.compatibility ?? null; report.beforeRepair = probe?.beforeRepair ?? [];
    report.safety = probe?.safety ?? null; report.currentDiskClosure = verification.currentDiskClosure; report.compiledProduct = verification.compiledProduct;
    report.remainingGaps = probe?.remainingGaps ?? ["actual CLR receipt missing"];
    const after = await snapshot(repoRoot, buildInputs);
    const payloadAfter = await snapshot(repoRoot, [...new Set(payloadFiles)]);
    report.changedDuringRun = [...after.filter((item, index) => item.sha256 !== before[index].sha256),
      ...payloadAfter.filter((item, index) => item.sha256 !== initialPayloadHashes[index].sha256)];
    if (report.changedDuringRun.length) { report.status = "FAIL"; report.failure = { code: "SOURCE_OR_PAYLOAD_CHANGED_DURING_RUN" }; }
    if (input.priorResult) {
      const old = await readJson(path.resolve(input.priorResult));
      const rows = Object.values(old).filter(Array.isArray).flat().find(item => item?.id === "fan.runtime-connection");
      const priorSources = rows?.sourceInputs ?? [];
      const current = await snapshot(repoRoot, priorSources.map(item => item.path));
      report.priorEvidence = { path: path.resolve(input.priorResult), caseStatus: rows?.status ?? "MISSING", managerGateRerun: false,
        sourceComparison: current.map((item, index) => ({ ...item, priorSha256: priorSources[index].sha256, unchanged: item.sha256 === priorSources[index].sha256 })) };
    }
    if (input.baselineResult) {
      report.compatibilityComparison = compareRuntimeCompatibility(await readJson(path.resolve(input.baselineResult)), report);
      if (report.compatibilityComparison.status !== "COMPATIBLE") { report.status = "FAIL"; report.failure = { code: "COMPATIBILITY_DIFFERENCE_REQUIRES_REVIEW" }; }
    }
  } catch (error) {
    report.status = "BLOCKED";
    report.failure = { code: error.code ?? "RUNTIME_CONNECTIVITY_INFRASTRUCTURE_FAILURE", exceptionType: error.name };
    report.remainingGaps.push("required runtime, compiler, source or receipt was unavailable; no mock fallback accepted");
  }
  report.finishedAt = new Date().toISOString();
  await fs.writeFile(path.join(evidenceDir, "result.json"), JSON.stringify(report, null, 2));
  return report;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const mapping = { "repo-root": "repoRoot", "evidence-dir": "evidenceDir", "runtime-root": "runtimeRoot", "shared-runtime-root": "sharedRuntimeRoot",
    "host-payload-root": "hostPayloadRoot", "pwsh-path": "pwshPath", "timeout-ms": "timeoutMs", "probe-assembly": "probeAssembly",
    "prior-result": "priorResult", "baseline-result": "baselineResult" };
  try {
    const args = {};
    for (let i = 2; i < process.argv.length; i += 2) {
      const key = mapping[process.argv[i]?.replace(/^--/, "")];
      if (!key || process.argv[i + 1] === undefined) throw fault("UNKNOWN_OR_INCOMPLETE_ARGUMENT");
      args[key] = key === "timeoutMs" ? Number(process.argv[i + 1]) : process.argv[i + 1];
    }
    const result = await runRuntimeConnectivityRegressions(args);
    console.log(JSON.stringify({ status: result.status, checks: result.checks.length, evidenceDir: path.resolve(args.evidenceDir), failure: result.failure ?? null }));
    process.exitCode = result.status === "PASS" ? 0 : result.status === "FAIL" ? 1 : 2;
  } catch (error) { console.error(JSON.stringify({ status: "BLOCKED", code: error.code ?? error.name })); process.exitCode = 2; }
}


