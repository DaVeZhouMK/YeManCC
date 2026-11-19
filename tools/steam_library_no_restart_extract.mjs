import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

// Compatible invocation: node steam_library_no_restart_extract.mjs out [source] [--fifo|--worker-chain]
const argv = process.argv.slice(2);
const workerChain = argv.at(-1) === '--worker-chain';
const fifo = argv.at(-1) === '--fifo' || workerChain;
const positional = fifo ? argv.slice(0, -1) : argv;
assert(positional.length === 1 || positional.length === 2, 'usage: extractor out [source] [--fifo]');
assert(positional.every(value => value.length > 0 && !value.startsWith('-')), 'invalid positional argument');
const out = path.resolve(positional[0]);
const file = path.resolve(positional[1] ?? 'native/custom-steam-library/workspace_host.cpp');
const generatorFile = fileURLToPath(import.meta.url);
assert(!fs.existsSync(out), 'output already exists');
assert(fs.lstatSync(file).isFile(), 'source must be a regular file');
const sourceBytes = fs.readFileSync(file);
const generatorBytes = fs.readFileSync(generatorFile);
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const pin = (name, bytes) => ({ path: name, bytes: bytes.length, sha256: sha256(bytes) });
const startMarker = Buffer.from('    if (command == "commit") {');
const endMarker = Buffer.from('    if (command == "deleteFromSteam") {');
const start = sourceBytes.indexOf(startMarker);
const end = sourceBytes.indexOf(endMarker, start + startMarker.length);
assert(start > 0 && end > start, 'production command anchors missing or out of order');
assert(sourceBytes.indexOf(startMarker, start + startMarker.length) === -1, 'ambiguous commit anchor');
assert(sourceBytes.indexOf(endMarker, end + endMarker.length) === -1, 'ambiguous delete anchor');
// Offsets are bytes, end exclusive. No CR/LF conversion or string-based slicing.
const sliceBytes = sourceBytes.subarray(start, end);
const body = sliceBytes.toString('utf8');
assert(Buffer.from(body, 'utf8').equals(sliceBytes), 'source slice is not lossless UTF-8');
assert(body.includes('if (command == "refreshSteamArtwork")'), 'refresh branch missing');
assert(!/\b(?:ShellExecute(?:Ex)?W|CreateProcess(?:AsUser|WithToken)?W|TerminateProcess|WinExec|SendInput)\s*\(/.test(body), 'unexpected direct process/input side effect');
const prefix = Buffer.from('inline json productionLibraryCommand(const std::string& command,const json& arguments){\n');
const suffix = Buffer.from('\nreturn json::object();}\n');
const outputBytes = Buffer.concat([prefix, sliceBytes, suffix]);
const headerFile = path.join(out, 'production-library-no-restart.h');
const metadata = {
  // Original consumers retain these three fields, including the unnormalized body.
  file, sha256: sha256(sourceBytes), body,
  schemaVersion: 1,
  source: pin(file, sourceBytes),
  slice: {
    source: file, sha256: sha256(sliceBytes), bytes: sliceBytes.length,
    offset: start, startOffset: start, endOffset: end,
    outputOffset: prefix.length, outputEndOffset: prefix.length + sliceBytes.length,
    offsetUnit: 'byte', endExclusive: true,
  },
  output: pin(headerFile, outputBytes),
  generator: pin(generatorFile, generatorBytes),
};
let fifoHeaderFile, fifoOutputBytes, fifoMetadata;
if (fifo) {
  function member(name, startAnchor, endAnchor) {
    const first = Buffer.from(startAnchor);
    const last = Buffer.from(endAnchor);
    const startOffset = sourceBytes.indexOf(first);
    const endOffset = sourceBytes.indexOf(last);
    assert(startOffset >= 0 && endOffset > startOffset, `${name}: anchors missing or out of order`);
    assert(sourceBytes.indexOf(first, startOffset + 1) === -1, `${name}: ambiguous start anchor`);
    assert(sourceBytes.indexOf(last, endOffset + 1) === -1, `${name}: ambiguous end anchor`);
    const bytes = sourceBytes.subarray(startOffset, endOffset);
    return {
      bytes,
      metadata: {
        name, ...pin(file, bytes), source: file,
        offset: startOffset, startOffset, endOffset,
        offsetUnit: 'byte', endExclusive: true,
        anchors: { start: startAnchor, end: endAnchor, occurrences: 1 },
      },
    };
  }
  const close = member('steam-action-close-helper', 'static uint64_t steamActionNumber(', 'static int64_t jsonIntegerOr(');
  const queue = member('workspace-command-fifo', 'struct WorkspaceQueuedCommand {', 'struct DirectoryWatcher {');
  assert(close.metadata.endOffset <= queue.metadata.startOffset, 'FIFO source members overlap or are out of order');
  const separator = Buffer.from('\n');
  fifoOutputBytes = Buffer.concat([close.bytes, separator, queue.bytes]);
  close.metadata.outputOffset = 0;
  close.metadata.outputEndOffset = close.bytes.length;
  queue.metadata.outputOffset = close.bytes.length + separator.length;
  queue.metadata.outputEndOffset = fifoOutputBytes.length;
  fifoHeaderFile = path.join(out, 'production-library-fifo.h');
  fifoMetadata = {
    schemaVersion: 1, mode: 'fifo',
    source: pin(file, sourceBytes),
    generator: pin(generatorFile, generatorBytes),
    output: pin(fifoHeaderFile, fifoOutputBytes),
    offsetUnit: 'byte', endExclusive: true,
    members: [close.metadata, queue.metadata],
    composition: [
      { member: close.metadata.name },
      {
        literalHex: separator.toString('hex'), bytes: separator.length, sha256: sha256(separator),
        outputOffset: close.bytes.length, outputEndOffset: close.bytes.length + separator.length,
      },
      { member: queue.metadata.name },
    ],
  };
}
let workerHeaderFile, workerOutputBytes, workerMetadata;
if (workerChain) {
  // All offsets and scans refer to this one captured Buffer. No string windows,
  // body substitutions, generated forward declarations or model implementations.
  const unique = (anchor, name) => {
    const bytes = Buffer.from(anchor);
    const at = sourceBytes.indexOf(bytes);
    assert(at >= 0 && sourceBytes.indexOf(bytes, at + 1) === -1, `${name}: missing or ambiguous anchor`);
    assert(at === 0 || sourceBytes[at - 1] === 10, `${name}: anchor is not at a member boundary`);
    return at;
  };
  const byte = (at, value) => sourceBytes[at] === value;
  // C++ lexical brace scanner: comments, escaped literals, raw strings and
  // numeric separators cannot terminate a complete member early.
  function braceEnd(open, name) {
    assert(byte(open, 123), `${name}: opening brace missing`);
    let depth = 0;
    for (let i = open; i < sourceBytes.length; ++i) {
      const c = sourceBytes[i], next = sourceBytes[i + 1];
      if (c === 47 && next === 47) {
        const end = sourceBytes.indexOf(10, i + 2);
        assert(end >= 0, `${name}: unterminated line comment`);
        i = end; continue;
      }
      if (c === 47 && next === 42) {
        const end = sourceBytes.indexOf(Buffer.from('*/'), i + 2);
        assert(end >= 0, `${name}: unterminated block comment`);
        i = end + 1; continue;
      }
      if (c === 82 && next === 34) {
        const paren = sourceBytes.indexOf(40, i + 2);
        assert(paren >= 0 && paren - i - 2 <= 16, `${name}: invalid raw string delimiter`);
        const delimiter = sourceBytes.subarray(i + 2, paren);
        assert(!/[\s()\\]/.test(delimiter.toString('ascii')), `${name}: invalid raw string delimiter`);
        const terminator = Buffer.concat([Buffer.from(')'), delimiter, Buffer.from('"')]);
        const end = sourceBytes.indexOf(terminator, paren + 1);
        assert(end >= 0, `${name}: unterminated raw string`);
        i = end + terminator.length - 1; continue;
      }
      if (c === 39 && /[0-9A-Za-z]/.test(String.fromCharCode(next ?? 0))) {
        let tokenStart = i - 1;
        while (tokenStart >= 0 && /[0-9A-Za-z_'.]/.test(String.fromCharCode(sourceBytes[tokenStart]))) --tokenStart;
        if (/[0-9]/.test(String.fromCharCode(sourceBytes[tokenStart + 1] ?? 0))) continue;
      }
      if (c === 34 || c === 39) {
        const quote = c;
        let closed = false;
        while (++i < sourceBytes.length) {
          if (sourceBytes[i] === 92) { ++i; continue; }
          if (sourceBytes[i] === quote) { closed = true; break; }
          assert(sourceBytes[i] !== 10 && sourceBytes[i] !== 13, `${name}: invalid multiline literal`);
        }
        assert(closed, `${name}: unterminated literal`);
        continue;
      }
      if (c === 123) ++depth;
      if (c === 125 && --depth === 0) return i + 1;
    }
    assert.fail(`${name}: unterminated member`);
  }
  function captured(name, startOffset, endOffset, anchors, kind, dependencies = []) {
    const bytes = sourceBytes.subarray(startOffset, endOffset);
    assert(endOffset > startOffset, `${name}: empty or reversed member`);
    return { bytes, metadata: {
      name, ...pin(file, bytes), source: file, kind,
      offset: startOffset, startOffset, endOffset, offsetUnit: 'byte', endExclusive: true,
      anchors, dependencies,
    } };
  }
  function complete(name, anchor, dependencies = [], kind = 'function') {
    const startOffset = unique(anchor, name);
    assert(anchor.endsWith('{'), `${name}: complete signature must include opening brace`);
    let endOffset = braceEnd(startOffset + Buffer.byteLength(anchor) - 1, name);
    if (kind === 'struct') {
      assert(byte(endOffset, 59), `${name}: struct terminator missing`);
      ++endOffset;
    }
    return captured(name, startOffset, endOffset, { start: anchor, occurrences: 1, end: 'lexical-brace-scanner' }, kind, dependencies);
  }
  function declaration(name, anchor) {
    const startOffset = unique(anchor, name);
    assert(anchor.endsWith(';'), `${name}: declaration terminator missing`);
    return captured(name, startOffset, startOffset + Buffer.byteLength(anchor), { start: anchor, occurrences: 1 }, 'declaration');
  }
  const members = [];
  const add = (name, anchor, dependencies = [], kind = 'function') => members.push(complete(name, anchor, dependencies, kind));
  members.push(declaration('kWorkerDefaultTimeoutMs', 'static constexpr DWORD kWorkerDefaultTimeoutMs = 15u * 60u * 1000u;'));
  add('toWide', 'static std::wstring toWide(const std::string& value) {', ['Win32']);
  add('toUtf8', 'static std::string toUtf8(const std::wstring& value) {', ['Win32']);
  add('quoteArgument', 'static std::wstring quoteArgument(const std::wstring& value) {');
  add('initializeWorkerProcessJob', 'static void initializeWorkerProcessJob() noexcept {', ['host-globals', 'Win32']);
  add('assignProcessToHostWorkerJob', 'static void assignProcessToHostWorkerJob(HANDLE process) noexcept {', ['host-globals', 'Win32']);
  add('closeWorkerProcessJob', 'static void closeWorkerProcessJob() noexcept {', ['host-globals', 'Win32']);
  members.push(declaration('jsonStringOr-declaration', 'static std::string jsonStringOr(const json& value, const char* key, const std::string& fallback = {});'));
  add('jsonStringOr', 'static std::string jsonStringOr(const json& value, const char* key, const std::string& fallback) {', ['jsonStringOr-declaration', 'nlohmann-json']);
  add('jsonArrayOr', 'static json jsonArrayOr(const json& value, const char* key) {', ['nlohmann-json']);
  add('jsonBoolOr', 'static bool jsonBoolOr(const json& value, const char* key, bool fallback = false) {', ['nlohmann-json']);
  add('hostSteamRunning', 'static bool hostSteamRunning(std::wstring* firstPath = nullptr) {', ['toWide', 'steam-information', 'Win32']);
  add('normalizedPathKey', 'static std::wstring normalizedPathKey(const fs::path& value) {', ['Win32']);
  add('samePath', 'static bool samePath(const fs::path& left, const fs::path& right) {', ['normalizedPathKey']);
  add('HostDataRootReadiness', 'struct HostDataRootReadiness {', [], 'struct');
  add('hostRootLexicalKey', 'static std::wstring hostRootLexicalKey(const fs::path& path) {', ['Win32']);
  add('hostRootPathInspectable', 'static bool hostRootPathInspectable(const fs::path& path) {', ['library-paths', 'Win32']);
  add('hostRootBoundPath', 'static bool hostRootBoundPath(const json& value,const fs::path& expected) {', ['toWide', 'hostRootLexicalKey', 'nlohmann-json']);
  add('hostRootUnsigned', 'static bool hostRootUnsigned(const json& value){', ['nlohmann-json']);
  add('readHostRootRecord', 'static std::optional<json> readHostRootRecord(const fs::path& path,size_t maximum) {', ['hostRootPathInspectable', 'library-config', 'nlohmann-json']);
  add('hostDataRootReadiness', 'static HostDataRootReadiness hostDataRootReadiness() noexcept {', ['HostDataRootReadiness', 'readHostRootRecord', 'hostRootBoundPath', 'hostRootUnsigned', 'hostRootLexicalKey', 'toWide', 'host-globals', 'library-paths']);
  add('requireHostDataRootReady', 'static void requireHostDataRootReady() {', ['hostDataRootReadiness']);
  add('hostDiscoveryDocumentValid', 'static bool hostDiscoveryDocumentValid(const fs::path& path,const json& value) {', ['nlohmann-json']);
  add('readHostDiscoveryState', 'static json readHostDiscoveryState(const fs::path& path,const json& fallback) {', ['hostDiscoveryDocumentValid', 'library-config', 'nlohmann-json']);

  const blockStartAnchor = 'struct ProcessResult {';
  const blockEndAnchor = 'static fs::path batchPlanPath() {';
  const blockStart = unique(blockStartAnchor, 'worker-process-chain:start');
  const blockEnd = unique(blockEndAnchor, 'worker-process-chain:end');
  const processBlock = captured('worker-process-chain', blockStart, blockEnd,
    { start: blockStartAnchor, end: blockEndAnchor, occurrences: 1 }, 'contiguous-source-block',
    ['kWorkerDefaultTimeoutMs', 'toWide', 'toUtf8', 'quoteArgument', 'assignProcessToHostWorkerJob', 'samePath', 'hostDataRootReadiness', 'requireHostDataRootReady', 'host-globals', 'process-efficiency', 'Win32']);
  // Independently scan every requested function/type contained in the block.
  // These are containment records, not overlapping composition members.
  const nested = [
    complete('ProcessResult', 'struct ProcessResult {', [], 'struct'),
    complete('HostProcessAdmission', 'enum class HostProcessAdmission {', [], 'struct'),
    complete('resolveExecutablePath', 'static fs::path resolveExecutablePath(const fs::path& executable) {'),
    complete('HostProcessAttributes', 'struct HostProcessAttributes {', [], 'struct'),
    complete('runProcess', 'static ProcessResult runProcess(\r\n    const fs::path& executable,\r\n    const std::vector<std::wstring>& arguments,\r\n    // Never let a host-side worker silently inherit an hour-long default.\r\n    // Search and scrape callers use a shorter explicit fence; this default is\r\n    // only for legacy local commands which have not supplied one.\r\n    DWORD timeoutMs = kWorkerDefaultTimeoutMs,\r\n    std::atomic<bool>* cancelFlag = nullptr,\r\n    HANDLE* activeProcessSlot = nullptr,\r\n    HostProcessAdmission admission = HostProcessAdmission::readyRoot) {'),
    complete('runWorker', 'static ProcessResult runWorker(\r\n    const std::vector<std::wstring>& arguments,\r\n    std::atomic<bool>* cancelFlag = nullptr,\r\n    HANDLE* activeProcessSlot = nullptr) {'),
    complete('runWorkerBounded', 'static ProcessResult runWorkerBounded(\r\n    const std::vector<std::wstring>& arguments,\r\n    DWORD timeoutMs,\r\n    std::atomic<bool>* cancelFlag = nullptr,\r\n    HANDLE* activeProcessSlot = nullptr) {'),
    complete('runStartupTransactionCleanup', 'static ProcessResult runStartupTransactionCleanup() {'),
    complete('workspacePlanPath', 'static fs::path workspacePlanPath() {'),
  ];
  for (let i = 0; i < nested.length; ++i) {
    const item = nested[i].metadata;
    assert(item.startOffset >= blockStart && item.endOffset <= blockEnd, `${item.name}: outside process block`);
    if (i) assert(nested[i - 1].metadata.endOffset <= item.startOffset, `${item.name}: nested members overlap`);
  }
  processBlock.metadata.containedMembers = nested.map(item => item.metadata);
  members.push(processBlock);
  add('steamPlanMatchesTarget', 'static bool steamPlanMatchesTarget(const json& plan, const json& selected) {', ['jsonStringOr', 'samePath', 'toWide']);
  add('isPlanForCurrentLibrary', 'static bool isPlanForCurrentLibrary(const json& plan, const json& expectedTarget = steamTargetSnapshot()) {', ['hostDiscoveryDocumentValid', 'worker-process-chain', 'jsonStringOr', 'samePath', 'toWide', 'steamPlanMatchesTarget', 'steamTargetSnapshot', 'host-globals']);
  add('steamTargetArguments', 'static std::vector<std::wstring> steamTargetArguments(const json& target) {', ['jsonStringOr', 'jsonArrayOr', 'toWide']);
  add('requireSuccess', 'static void requireSuccess(const ProcessResult& result) {', ['worker-process-chain']);
  add('readSteamAddCompletion', 'static json readSteamAddCompletion(const ProcessResult& result, const fs::path& outputPath, const json& target) {', ['requireSuccess', 'jsonStringOr', 'toWide', 'samePath', 'readHostDiscoveryState', 'isPlanForCurrentLibrary', 'plan-receipt', 'host-globals']);

  // Public dependencies remain actual headers/consumer-owned ports. Their
  // implementations are never pasted halfway or replaced with blank models.
  const declaredExternal = [
    { name: 'steam-information', kind: 'production-header', include: '../steam_information_client.h', symbols: ['ymcc::steaminfo::read', 'ymcc::steaminfo::Presence'], reason: 'Actual public YMCC Steam facts reader used by the complete hostSteamRunning consumer. Steam unknown throws unchanged; do not substitute an owned-only census or process-name model.' },
    { name: 'host-globals', kind: 'consumer-owned-state', symbols: ['g_workerProcessJobMutex', 'g_workerProcessJob', 'g_worker', 'g_labRoot', 'g_dataRoot', 'g_shuttingDown', 'g_updateHealthOnly', 'g_cancelRequested', 'g_activeProcess', 'g_activeProcessMutex'], reason: 'Provided by the real Host controller; preserve original lifetime and ownership.' },
    { name: 'steamTargetSnapshot', kind: 'consumer-owned-target-port', symbols: ['steamTargetSnapshot'], reason: 'Original target getter is an external UI/configuration boundary, including the default argument of isPlanForCurrentLibrary; do not synthesize a replacement.' },
    { name: 'process-efficiency', kind: 'production-header', include: '../process_efficiency.h', symbols: ['ymcc::processEfficiency::createProcess'], reason: 'Use the complete formal header, including its production dependencies.' },
    { name: 'library-config', kind: 'production-header', include: 'custom_steam_library_config.h', symbols: ['custom_steam_library::config::readBytes', 'custom_steam_library::config::readRuntime'], reason: 'Complete shared file-handle/read/schema policy; include the original header and transitive dependencies, not partial copies.' },
    { name: 'library-paths', kind: 'production-header', include: 'custom_steam_library_paths.h', symbols: ['custom_steam_library::ioPath', 'custom_steam_library::legacyDataRoots'], reason: 'Complete common path policy, including legacy root discovery; do not model its file/path decisions.' },
    { name: 'plan-receipt', kind: 'production-header', include: 'custom_steam_library_plan_receipt.h', symbols: ['custom_steam_library::plan_receipt::addCompletionDigest'], reason: 'Consume the real digest implementation and its transitive dependencies.' },
    { name: 'nlohmann-json', kind: 'production-header', include: 'json.hpp', symbols: ['json'], reason: 'Original nlohmann::json type and parser.' },
    { name: 'Win32', kind: 'platform', symbols: ['Win32 API/types/constants'], reason: 'Real Win32 APIs required; extraction is not an execution or compiler proof.' },
    { name: 'standard-library', kind: 'platform', symbols: ['fs=std::filesystem', 'std types/algorithms/containers'], reason: 'Consumer supplies the standard includes and namespace alias used by the original Host.' },
  ];
  const names = new Set();
  for (const member of members) {
    assert(!names.has(member.metadata.name), 'duplicate worker member');
    names.add(member.metadata.name);
  }
  const externalNames = new Set(declaredExternal.map(item => item.name));
  for (const member of members) for (const dependency of member.metadata.dependencies) {
    assert(names.has(dependency) || externalNames.has(dependency), `${member.metadata.name}: undeclared dependency ${dependency}`);
    if (names.has(dependency)) assert(members.findIndex(item => item.metadata.name === dependency) < members.indexOf(member), `${member.metadata.name}: dependency out of order`);
  }
  const bySource = [...members].sort((a, b) => a.metadata.startOffset - b.metadata.startOffset);
  for (let i = 1; i < bySource.length; ++i)
    assert(bySource[i - 1].metadata.endOffset <= bySource[i].metadata.startOffset, 'worker source members overlap');
  const composition = [], chunks = [];
  let outputOffset = 0;
  for (const member of members) {
    if (chunks.length) {
      const separator = Buffer.from('\n');
      chunks.push(separator);
      composition.push({ literalHex: separator.toString('hex'), bytes: separator.length, sha256: sha256(separator), outputOffset, outputEndOffset: outputOffset + separator.length });
      outputOffset += separator.length;
    }
    member.metadata.outputOffset = outputOffset;
    member.metadata.outputEndOffset = outputOffset + member.bytes.length;
    composition.push({ member: member.metadata.name, bytes: member.bytes.length, sha256: sha256(member.bytes), outputOffset, outputEndOffset: member.metadata.outputEndOffset });
    if (member.metadata.containedMembers) for (const contained of member.metadata.containedMembers) {
      contained.outputOffset = outputOffset + contained.startOffset - member.metadata.startOffset;
      contained.outputEndOffset = outputOffset + contained.endOffset - member.metadata.startOffset;
    }
    chunks.push(member.bytes);
    outputOffset += member.bytes.length;
  }
  workerOutputBytes = Buffer.concat(chunks);
  workerHeaderFile = path.join(out, 'production-library-worker-chain.h');
  workerMetadata = {
    schemaVersion: 1, mode: 'worker-chain', impliesFifo: true,
    source: pin(file, sourceBytes), generator: pin(generatorFile, generatorBytes),
    output: pin(workerHeaderFile, workerOutputBytes), offsetUnit: 'byte', endExclusive: true,
    members: members.map(item => item.metadata), composition, declaredExternal,
    closure: { sourceMembersComplete: true, standalone: false, dependencies: members.map(item => ({ member: item.metadata.name, requires: item.metadata.dependencies })), externalHeadersAndConsumerPortsRequired: true, transitiveHeaderPins: 'Consumer must pin complete actual compiler dependency graph; not inferred by this source-only extractor.' },
    pins: { source: pin(file, sourceBytes), generator: pin(generatorFile, generatorBytes), output: pin(workerHeaderFile, workerOutputBytes), impliedFifo: fifoMetadata.output, command: metadata.output },
  };
}
assert(sourceBytes.equals(fs.readFileSync(file)), 'source changed during extraction');
assert(generatorBytes.equals(fs.readFileSync(generatorFile)), 'generator changed during extraction');
if (workerChain) workerMetadata.identityBeforeAfter = {
  before: { source: pin(file, sourceBytes), generator: pin(generatorFile, generatorBytes) },
  after: { source: pin(file, fs.readFileSync(file)), generator: pin(generatorFile, fs.readFileSync(generatorFile)) },
};
if (workerChain) {
  assert(workerMetadata.identityBeforeAfter.before.source.sha256 === workerMetadata.identityBeforeAfter.after.source.sha256, 'worker source changed during final identity read');
  assert(workerMetadata.identityBeforeAfter.before.generator.sha256 === workerMetadata.identityBeforeAfter.after.generator.sha256, 'worker generator changed during final identity read');
}
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.mkdirSync(out); // Exclusive directory admission; never reuse an old output.
function writeExclusiveFlushed(name, bytes) {
  const fd = fs.openSync(name, 'wx');
  try {
    fs.writeFileSync(fd, bytes);
    fs.fsyncSync(fd);
  } finally {
    fs.closeSync(fd);
  }
  assert(fs.readFileSync(name).equals(bytes), `output readback mismatch: ${name}`);
}
writeExclusiveFlushed(headerFile, outputBytes);
writeExclusiveFlushed(path.join(out, 'source.json'), Buffer.from(JSON.stringify(metadata, null, 2) + '\n', 'utf8'));
if (fifo) {
  writeExclusiveFlushed(fifoHeaderFile, fifoOutputBytes);
  writeExclusiveFlushed(path.join(out, 'fifo-source.json'), Buffer.from(JSON.stringify(fifoMetadata, null, 2) + '\n', 'utf8'));
}
if (workerChain) {
  writeExclusiveFlushed(workerHeaderFile, workerOutputBytes);
  writeExclusiveFlushed(path.join(out, 'worker-chain-source.json'), Buffer.from(JSON.stringify(workerMetadata, null, 2) + '\n', 'utf8'));
  assert(sourceBytes.equals(fs.readFileSync(file)), 'source changed during worker output publication');
  assert(generatorBytes.equals(fs.readFileSync(generatorFile)), 'generator changed during worker output publication');
}

