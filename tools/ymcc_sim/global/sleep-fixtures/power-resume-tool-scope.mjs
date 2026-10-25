/** Identity closure for the unchanged legacy power-resume data-URL producer. */
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { sourcePath } from '../runner.mjs';
import {
  BUNDLED_AUTHORITY_SOURCES, lockBundledNodeTools, classifyBundledInput,
  verifyBundledToolScope, verifyBundledImplementation,
} from '../bundled-node-tool-authority.mjs';

const defaultRoot = path.resolve(import.meta.dirname, '../../../..');
export const POWER_RESUME_TOOL_SOURCES = Object.freeze([
  ...BUNDLED_AUTHORITY_SOURCES, 'tools/ymcc_sim/global/sleep.mjs',
  'tools/ymcc_sim/global/sleep-fixtures/power-resume-tool-scope.mjs',
]);
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const hashFile = file => hash(fs.readFileSync(file));
const need = (ok, code) => { if (!ok) throw Error('POWER_RESUME_' + code); };
const key = file => path.resolve(file).toLowerCase();
const inside = (root, file) => {
  const relative = path.relative(root, file);
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith('..' + path.sep));
};
const OUTPUTS = Object.freeze({
  '.bundle.mjs': 'exact compiler bytes supplied to the original data-URL import',
  '.metafile.json': 'actual nested compiler dependency table',
  '.migrated.ts': 'original producer with the unchanged legacy source-guard migrations',
  '.import-url.txt': 'exact data URL supplied to the original dynamic import',
});

/** Missing tools block this case while preserving the rest of the sleep catalog. */
export function powerResumeToolDeclaration(repoRoot = defaultRoot, options = {}) {
  try { return { artifactInputs: lockBundledNodeTools(repoRoot, options).tools.map(row => row.path) }; }
  catch (error) { return { artifactInputs: [], blockedReason: 'POWER_RESUME_TOOL_AUTHORITY_UNAVAILABLE: ' + error.message }; }
}

export function beginPowerResumeTools(repoRoot, output, options = {}) {
  const repo = fs.realpathSync(repoRoot);
  verifyBundledImplementation(repo);
  const authority = lockBundledNodeTools(repo, options);
  const outputFile = output ? path.resolve(output)
    : path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'ymcc-power-resume-')), 'power-resume.json');
  fs.mkdirSync(path.dirname(outputFile), { recursive: true });
  const outputDir = fs.realpathSync(path.dirname(outputFile));
  need(key(outputDir) === key(path.dirname(outputFile)), 'EVIDENCE_DIRECTORY_FORWARDING_REJECTED');
  need(!inside(repo, outputFile) && !inside(repo, outputDir), 'EVIDENCE_MUST_BE_OUTSIDE_PRODUCT_ROOT');
  need(!fs.existsSync(outputFile), 'FRESH_EVIDENCE_REQUIRED');
  for (const suffix of Object.keys(OUTPUTS)) need(!fs.existsSync(outputFile + suffix), 'FRESH_COMPILE_ARTIFACT_REQUIRED');
  const sourceHashes = {};
  for (const relative of [...POWER_RESUME_TOOL_SOURCES,
    'tools/ymcc_sim/global/sleep-fixtures/power-resume-entry.mjs', 'tools/power_resume_selftest.ts',
    'src/bridge/powerResume.ts', 'src/App.vue', 'native/main.cpp']) {
    sourceHashes[relative] = hashFile(sourcePath(repo, relative));
  }
  return {
    repo, outputFile, outputDir, authority, sourceHashes,
    observedSources: new Map(), observedTools: new Map(), assets: [],
    report: { authority, sourceApproval: false, packageClosureComplete: true,
      toolDependencyInputs: [], compiledArtifacts: [], scopeVerified: false },
  };
}

function writeOutput(scope, suffix, bytes) {
  need(Object.hasOwn(OUTPUTS, suffix), 'UNDECLARED_OUTPUT');
  const file = scope.outputFile + suffix;
  need(!scope.assets.some(row => key(row.path) === key(file)), 'DUPLICATE_OUTPUT');
  fs.writeFileSync(file, bytes, { flag: 'wx' });
  need(key(fs.realpathSync(file)) === key(file), 'OUTPUT_FORWARDING_REJECTED');
  const row = { path: file, sha256: hashFile(file), role: OUTPUTS[suffix] };
  scope.assets.push(row);
  scope.sourceHashes[file] = row.sha256;
  return row;
}

export function verifyPowerResumeTools(scope) {
  const proof = verifyBundledToolScope(scope.authority, scope.repo);
  need(key(fs.realpathSync(scope.outputDir)) === key(scope.outputDir), 'EVIDENCE_DIRECTORY_FORWARDING_REJECTED');
  const known = new Set(Object.keys(OUTPUTS).map(suffix => key(scope.outputFile + suffix)));
  const recorded = new Map();
  for (const row of scope.assets) {
    need(known.has(key(row.path)) && !recorded.has(key(row.path)), 'UNDECLARED_OR_DUPLICATE_OUTPUT');
    need(key(fs.realpathSync(row.path)) === key(row.path) && inside(scope.outputDir, row.path), 'OUTPUT_FORWARDING_REJECTED');
    need(hashFile(row.path) === row.sha256 && scope.sourceHashes[row.path] === row.sha256, 'SOURCE_OR_OUTPUT_CHANGED:' + row.path);
    recorded.set(key(row.path), row.sha256);
  }
  for (const [relative, digest] of Object.entries(scope.sourceHashes)) {
    if (path.isAbsolute(relative)) {
      need(known.has(key(relative)) && recorded.get(key(relative)) === digest, 'UNDECLARED_ABSOLUTE_SOURCE');
    } else {
      need(hashFile(sourcePath(scope.repo, relative)) === digest, 'SOURCE_OR_OUTPUT_CHANGED:' + relative);
    }
  }
  scope.report.scopeVerified = true;
  scope.report.toolDependencyInputs = [...scope.observedTools.values()];
  scope.report.compiledArtifacts = scope.assets;
  return { graphSha256: proof.graphSha256, packageCount: proof.packageCount, fileCount: proof.fileCount,
    completeArtifacts: proof.tools.length, toolAuthorityVerified: true };
}

export async function auditedPowerResumeBuild(scope, options) {
  scope.report.beforeCompileProof = verifyPowerResumeTools(scope);
  need(options.stdin && typeof options.stdin.contents === 'string', 'ORIGINAL_STDIN_REQUIRED');
  need(key(process.cwd()) === key(scope.repo), 'COMPILE_CWD_CHANGED');
  const require = createRequire(path.join(scope.repo, 'package.json'));
  need(key(fs.realpathSync(require.resolve('esbuild'))) === key(scope.authority.compilerEntry), 'COMPILER_ENTRY_NOT_BOUND');
  const esbuild = require(scope.authority.compilerEntry);
  const inputPlugin = { name: 'power-resume-bound-inputs', setup(build) {
    build.onLoad({ filter: /\.(?:[cm]?js|jsx|tsx?|json)$/, namespace: 'file' }, ({ path: file }) => {
      const bytes = fs.readFileSync(file);
      const classified = classifyBundledInput(scope.repo, file, bytes, scope.authority);
      (classified.role === 'tool' ? scope.observedTools : scope.observedSources).set(key(fs.realpathSync(file)), classified.row);
      const extension = path.extname(file).slice(1);
      return { contents: bytes.toString('utf8'), resolveDir: path.dirname(file),
        loader: ['ts', 'tsx', 'jsx', 'json'].includes(extension) ? extension : 'js' };
    });
  } };
  const built = await esbuild.build({ ...options, metafile: true, plugins: [inputPlugin, ...(options.plugins ?? [])] });
  need(built.outputFiles.length === 1, 'EXACT_ONE_ORIGINAL_BUNDLE_REQUIRED');
  const observed = new Set();
  let virtualCount = 0;
  for (const [input, metadata] of Object.entries(built.metafile.inputs)) {
    // esbuild names stdin with the original sourcefile; it is not an on-disk read.
    if (input === '<stdin>' || input === options.stdin.sourcefile) {
      need(metadata.bytes === Buffer.byteLength(options.stdin.contents), 'VIRTUAL_STDIN_BYTES_CHANGED');
      scope.report.virtualInput = { path: input, sha256: hash(Buffer.from(options.stdin.contents)),
        sourceFile: 'tools/power_resume_selftest.ts', kind: 'existing exact legacy guard migration' };
      virtualCount++;
      continue;
    }
    need(!input.includes(':') || path.isAbsolute(input), 'UNEXPECTED_VIRTUAL_INPUT');
    const file = path.resolve(scope.repo, input), bytes = fs.readFileSync(file);
    const classified = classifyBundledInput(scope.repo, file, bytes, scope.authority);
    const identity = key(fs.realpathSync(file));
    const loaded = (classified.role === 'tool' ? scope.observedTools : scope.observedSources).get(identity);
    need(loaded?.sha256 === classified.row.sha256 && metadata.bytes === bytes.length, 'METAFILE_INPUT_NOT_OBSERVED:' + input);
    need(!observed.has(identity), 'DUPLICATE_METAFILE_INPUT');
    observed.add(identity);
  }
  need(virtualCount === 1 && observed.size === scope.observedSources.size + scope.observedTools.size, 'METAFILE_INPUT_CLOSURE_CHANGED');
  for (const row of scope.observedSources.values()) scope.sourceHashes[row.path] = row.sha256;
  writeOutput(scope, '.bundle.mjs', built.outputFiles[0].contents);
  writeOutput(scope, '.metafile.json', Buffer.from(JSON.stringify(built.metafile, null, 2) + '\n'));
  writeOutput(scope, '.migrated.ts', Buffer.from(options.stdin.contents));
  scope.report.executedBundleSha256 = hash(built.outputFiles[0].contents);
  scope.report.migratedSourceSha256 = hash(Buffer.from(options.stdin.contents));
  scope.report.afterCompileProof = verifyPowerResumeTools(scope);
  return built;
}

export function originalPowerResumeDataUrl(scope, built) {
  const bytes = Buffer.from(built.outputFiles[0].contents);
  need(hash(bytes) === scope.report.executedBundleSha256, 'EXECUTED_BUNDLE_BYTES_CHANGED');
  verifyPowerResumeTools(scope);
  const url = 'data:text/javascript;base64,' + bytes.toString('base64');
  const recorded = writeOutput(scope, '.import-url.txt', Buffer.from(url));
  scope.report.originalImportUrlSha256 = recorded.sha256;
  scope.report.executionKind = 'original data URL import with exact recorded compiler bytes';
  scope.report.beforeExecutionProof = verifyPowerResumeTools(scope);
  return url;
}

export function finishPowerResumeTools(scope, summary) {
  need(scope.assets.length === Object.keys(OUTPUTS).length && scope.report.beforeExecutionProof, 'COMPLETE_EXECUTED_OUTPUTS_REQUIRED');
  scope.report.afterExecutionProof = verifyPowerResumeTools(scope);
  fs.writeFileSync(scope.outputFile, JSON.stringify({ ...summary, sourceApproval: false,
    sourceHashes: scope.sourceHashes, toolScope: scope.report }, null, 2) + '\n', { flag: 'wx' });
  return scope.outputFile;
}
