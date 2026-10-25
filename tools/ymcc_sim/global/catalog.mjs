import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import ts from 'typescript';
import { parse as parseVue } from 'vue/compiler-sfc';

const taxonomy = JSON.parse(fs.readFileSync(new URL('./domains.json', import.meta.url), 'utf8').replace(/^\uFEFF/, ''));
const domainRules = taxonomy.domains.map(domain => ({ ...domain, rules: domain.patterns.map(pattern => new RegExp(pattern, 'i')) }));
const JS = new Set(['.ts', '.tsx', '.js', '.jsx', '.mjs', '.cjs', '.vue', '.html']);
const NATIVE = new Set(['.h', '.hpp', '.hh', '.cpp', '.cc', '.c', '.cs']);
const SKIP = new Set(['node_modules', '.git', 'deps', 'vendor', 'Build', 'dist', 'bin', 'obj', '__pycache__', '_scratch', 'packages']);
const SCOPES = ['behavioral-mock', 'source-contract', 'host-mock'];
const slash = value => value.replace(/\\/g, '/');
const own = (node, flag = ts.SyntaxKind.ExportKeyword) => node.modifiers?.some(modifier => modifier.kind === flag);
const discriminator = value => /^(?:operation|op|cmd|command|action)$|(?:Operation|Command|Cmd)$/.test(value);
const tableName = value => /^(?:ops|operations|commands|handlers|routes)$|(?:Operations|Commands|Handlers|Routes|OperationNames|CommandNames)$|_(?:OPS|OPERATIONS|COMMANDS|HANDLERS)$/i.test(value);
const propName = node => node && (ts.isIdentifier(node) || ts.isStringLiteralLike(node) || ts.isNumericLiteral(node)) ? node.text : undefined;

/** Read-only synchronous inventory (also awaitable). REGISTERED_TEST is a source
 * association, never a passing test or behavioral/hardware acceptance claim. */
export function buildCatalog({ repoRoot, adapters = [] } = {}) {
  if (typeof repoRoot !== 'string' || !repoRoot) throw new TypeError('repoRoot is required');
  if (!Array.isArray(adapters)) throw new TypeError('adapters must be an array');
  const root = fs.realpathSync(repoRoot);
  const diagnostics = [], surfaces = [];
  const seen = new Set(), scannedFiles = new Set(), sourceFiles = new Set(), manifests = new Set();
  const sourceCache = new Map(), lineIndexes = new Map();
  const sourceExclusions = new Map();
  const roots = ['src', 'native', 'InputHost', 'FanLab', 'capabilities', 'CustomSteamLibrary', 'decky-plugin/src', 'PowerControl/decky/plugins/ymcc-sidebar/src', 'public', 'GamepadPrerequisites', 'PowerControl/MemoryScan'];
  const inside = absolute => absolute === root || absolute.startsWith(root + path.sep);
  const relative = absolute => slash(path.relative(root, absolute));
  const error = (file, message) => diagnostics.push({ path: file, message });
  function walk(directory, visit) {
    const absolute = path.join(root, directory);
    if (!fs.existsSync(absolute)) return;
    let entries;
    try { entries = fs.readdirSync(absolute, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name, 'en')); }
    catch (cause) { error(directory, `Cannot enumerate source: ${cause.message}`); return; }
    for (const entry of entries) {
      const file = path.posix.join(directory, entry.name);
      if (entry.isSymbolicLink()) { error(file, 'Symbolic link skipped; target is not inventoried'); continue; }
      if (entry.isDirectory()) { if (!SKIP.has(entry.name)) walk(file, visit); }
      else if (entry.isFile()) visit(file);
    }
  }
  // Discover all first-party manifests, excluding fixture/tool manifests.
  walk('', file => { if (path.posix.basename(file) === 'package.json' && !file.startsWith('tools/')) manifests.add(file); });
  const collect = file => {
    if (!JS.has(path.extname(file)) && !NATIVE.has(path.extname(file))) return;
    // Reviewed injected tests remain adapter inputs, not product capabilities.
    // Do not exclude arbitrary files merely because their name contains 'test'.
    if (file.startsWith('native/BatteryHostTests/')) {
      sourceExclusions.set(file, {path:file, role:'reviewed-test-source', reason:'BatteryHost tests use fake providers and reference the production project; they are not the production service.'});
      return;
    }
    sourceFiles.add(file);
  };
  for (const directory of roots) walk(directory, collect);
  for (const manifest of manifests) {
    const directory = path.posix.dirname(manifest), sourceRoot = path.posix.join(directory, 'src');
    if (directory !== '.' && !roots.some(item => sourceRoot === item || sourceRoot.startsWith(item + '/')) && fs.existsSync(path.join(root, sourceRoot))) {
      roots.push(sourceRoot); walk(sourceRoot, collect);
    }
  }
  function classify(file, operation = '') {
    const normalize = value => value.replace(/([a-z0-9])([A-Z])/g, '$1 $2');
    const match = value => domainRules.find(domain => domain.rules.some(rule => rule.test(normalize(value))))?.id;
    return match(operation) || match(path.posix.basename(file).replace(/\.[^.]+$/, '')) || match(file.replace(/^(src|native)\//, '')) || 'unclassified';
  }
  function add(file, kind, line, operation, extra = {}) {
    const key = JSON.stringify([file, kind, line, operation ?? '', extra.expression ?? '']);
    if (seen.has(key)) return;
    seen.add(key);
    surfaces.push({ id: `${kind}:${createHash('sha256').update(key).digest('hex').slice(0, 20)}`,
      domain: classify(file, extra.dynamic ? extra.expression || '' : operation || ''), kind, path: file, line: Math.max(1, line),
      ...(operation === undefined ? {} : { operation }), dynamic: false, ...extra,
      coverage: 'NOT_COVERED', adapterIds: [], evidenceScopes: [] });
  }
  function read(file) {
    try { const text = fs.readFileSync(path.join(root, file), 'utf8').replace(/^\uFEFF/, ''); scannedFiles.add(file); return text; }
    catch (cause) { error(file, `Cannot read source: ${cause.message}`); return undefined; }
  }
  function lineAt(text, offset) {
    let lines = lineIndexes.get(text);
    if (!lines) { lines = [0]; for (let i = 0; i < text.length; i++) if (text.charCodeAt(i) === 10) lines.push(i + 1); lineIndexes.set(text, lines); }
    let lo = 0, hi = lines.length;
    while (lo + 1 < hi) { const mid = (lo + hi) >>> 1; if (lines[mid] <= offset) lo = mid; else hi = mid; }
    return lo + 1;
  }
  function parse(file) {
    if (sourceCache.has(file)) return sourceCache.get(file);
    const text = read(file), result = { file, text: text ?? '', parts: [] };
    sourceCache.set(file, result);
    if (text === undefined) return result;
    let blocks = [{ content: text, offset: 0 }];
    if (file.endsWith('.vue')) {
      const { descriptor, errors } = parseVue(text, { filename: file });
      for (const cause of errors) error(file, `Vue parse: ${typeof cause === 'string' ? cause : cause.message}`);
      blocks = [descriptor.script, descriptor.scriptSetup].filter(Boolean).map(block => ({ content: block.content, offset: block.loc.start.offset }));
    }
    if (file.endsWith('.html')) {
      // Standalone shipped pages keep their inline script source locations.
      blocks = [...text.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi)]
        .filter(match => !/\btype\s*=\s*["'](?:application\/(?:ld\+)?json|text\/template)["']/i.test(match[1]))
        .map(match => ({ content: match[2], offset: match.index + match[0].indexOf('>') + 1 }));
    }
    for (const block of blocks) {
      const sf = ts.createSourceFile(file, block.content, ts.ScriptTarget.Latest, true, /\.[jt]sx$/.test(file) ? ts.ScriptKind.TSX : ts.ScriptKind.TS);
      const part = { sf, offset: block.offset, bindings: new WeakMap(), file, text };
      result.parts.push(part);
      for (const diagnostic of sf.parseDiagnostics) error(file, `TypeScript parse: ${ts.flattenDiagnosticMessageText(diagnostic.messageText, ' ')}`);
      function bind(scope, name, binding) {
        if (!part.bindings.has(scope)) part.bindings.set(scope, new Map());
        part.bindings.get(scope).set(name, binding);
      }
      function scopeFor(node) {
        for (let parent = node.parent; parent; parent = parent.parent) if (ts.isSourceFile(parent) || ts.isBlock(parent) || ts.isFunctionLike(parent) || ts.isCatchClause(parent)) return parent;
        return sf;
      }
      function index(node) {
        if (ts.isVariableDeclaration(node) && ts.isIdentifier(node.name)) bind(scopeFor(node), node.name.text, { expression: node.parent.flags & ts.NodeFlags.Const ? node.initializer : undefined, part });
        else if (ts.isParameter(node) && ts.isIdentifier(node.name)) bind(node.parent, node.name.text, { part });
        else if (ts.isImportDeclaration(node) && ts.isStringLiteral(node.moduleSpecifier)) {
          const from = node.moduleSpecifier.text, clause = node.importClause;
          if (clause?.name) bind(sf, clause.name.text, { from, imported: 'default', part });
          if (clause?.namedBindings && ts.isNamedImports(clause.namedBindings)) for (const element of clause.namedBindings.elements) bind(sf, element.name.text, { from, imported: element.propertyName?.text || element.name.text, part });
        }
        ts.forEachChild(node, index);
      }
      index(sf);
    }
    return result;
  }
  function importedFile(fromFile, specifier) {
    if (!specifier.startsWith('.') && !specifier.startsWith('@/')) return undefined;
    const base = specifier.startsWith('@/') ? path.join(root, 'src', specifier.slice(2)) : path.resolve(root, path.dirname(fromFile), specifier);
    if (!inside(base)) return undefined;
    const candidates = [base, ...['.ts', '.tsx', '.js', '.mjs', '.vue', '/index.ts', '/index.js'].map(suffix => base + suffix)];
    return candidates.find(candidate => JS.has(path.extname(candidate)) && fs.existsSync(candidate) && !fs.lstatSync(candidate).isSymbolicLink() && fs.statSync(candidate).isFile());
  }
  function importedExpression(binding) {
    const absolute = importedFile(binding.part.file, binding.from);
    if (!absolute) return undefined;
    const target = parse(relative(absolute));
    for (const part of target.parts) for (const statement of part.sf.statements) {
      if (own(statement) && ts.isVariableStatement(statement)) for (const declaration of statement.declarationList.declarations) if (ts.isIdentifier(declaration.name) && declaration.name.text === binding.imported) return { expression: declaration.initializer, part };
      if (ts.isExportAssignment(statement) && binding.imported === 'default') return { expression: statement.expression, part };
    }
    return undefined;
  }
  const unknown = () => ({ values: [], unresolved: true });
  function resolve(node, part, stack = new Set()) {
    if (!node || stack.has(node) || stack.size > 40) return unknown();
    const next = new Set(stack).add(node);
    if (ts.isStringLiteralLike(node) || ts.isNumericLiteral(node)) return { values: [node.text], unresolved: false };
    if (ts.isParenthesizedExpression(node) || ts.isAsExpression(node) || ts.isTypeAssertionExpression(node) || ts.isNonNullExpression(node) || ts.isSatisfiesExpression(node)) return resolve(node.expression, part, next);
    if (ts.isIdentifier(node)) {
      for (let parent = node; parent; parent = parent.parent) {
        const binding = part.bindings.get(parent)?.get(node.text);
        if (binding) { const target = binding.from ? importedExpression(binding) : binding; return target ? resolve(target.expression, target.part, next) : unknown(); }
      }
      return unknown();
    }
    if (ts.isConditionalExpression(node)) {
      const left = resolve(node.whenTrue, part, next), right = resolve(node.whenFalse, part, next);
      return { values: [...new Set([...left.values, ...right.values])].slice(0, 64), unresolved: left.unresolved || right.unresolved || left.values.length + right.values.length > 64 };
    }
    const combine = (left, right) => ({ values: left.values.flatMap(a => right.values.map(b => a + b)).slice(0, 64), unresolved: left.unresolved || right.unresolved || left.values.length * right.values.length > 64 });
    if (ts.isBinaryExpression(node) && node.operatorToken.kind === ts.SyntaxKind.PlusToken) return combine(resolve(node.left, part, next), resolve(node.right, part, next));
    if (ts.isTemplateExpression(node)) {
      let result = { values: [node.head.text], unresolved: false };
      for (const span of node.templateSpans) result = combine(combine(result, resolve(span.expression, part, next)), { values: [span.literal.text], unresolved: false });
      return result;
    }
    return unknown();
  }
  function emitExpression(part, kind, node, anchor = node) {
    const resolved = resolve(node, part), line = lineAt(part.text, part.offset + anchor.getStart(part.sf));
    for (const operation of new Set(resolved.values)) add(part.file, kind, line, operation);
    if (resolved.unresolved) add(part.file, `${kind}-dynamic`, line, undefined, { dynamic: true,
      expression: node?.getText(part.sf) || '<missing expression>', unresolvedReason: 'Not statically enumerable; review or supply runtime evidence' });
  }
  function scanJs(file) {
    const parsed = parse(file);
    if (file.startsWith('src/views/') && file.endsWith('.vue')) add(file, 'view', 1);
    if (file.startsWith('src/bridge/')) add(file, 'bridge-module', 1);
    if (file.endsWith('.html')) add(file, 'standalone-page', 1);
    else if (!file.startsWith('src/')) add(file, 'native-module', 1);
    for (const part of parsed.parts) {
      const ipcEvents = new Set(), ipcCalls = new Set(['invoke']), ipcNamespaces = new Set(['ipc']);
      for (const statement of part.sf.statements) if (ts.isImportDeclaration(statement) && /(?:^|\/)ipc(?:\.[cm]?[jt]s)?$/.test(statement.moduleSpecifier.text || '')) {
        const bindings = statement.importClause?.namedBindings;
        if (bindings && ts.isNamespaceImport(bindings)) ipcNamespaces.add(bindings.name.text);
        if (bindings && ts.isNamedImports(bindings)) for (const item of bindings.elements) {
          const imported = item.propertyName?.text || item.name.text;
          if (['invoke', 'send', 'request', 'call'].includes(imported)) ipcCalls.add(item.name.text);
          if (['on', 'once', 'subscribe'].includes(imported)) ipcEvents.add(item.name.text);
        }
      }
      const line = node => lineAt(part.text, part.offset + node.getStart(part.sf));
      const nameOf = expression => ts.isPropertyAccessExpression(expression) ? expression.name.text : ts.isIdentifier(expression) ? expression.text : '';
      function exported(name, node) {
        add(file, 'bridge-export', line(node), name);
        if (node.initializer && ts.isObjectLiteralExpression(node.initializer)) for (const property of node.initializer.properties) {
          const member = propName(property.name);
          if (member && (ts.isMethodDeclaration(property) || (property.initializer && (ts.isArrowFunction(property.initializer) || ts.isFunctionExpression(property.initializer))))) add(file, 'bridge-export', line(property), `${name}.${member}`);
        }
      }
      function operationTable(node) {
        if (ts.isCallExpression(node) && node.arguments.length === 1) return operationTable(node.arguments[0]);
        if (ts.isArrayLiteralExpression(node)) for (const element of node.elements) emitExpression(part, 'operation-list', element);
        else if (ts.isObjectLiteralExpression(node)) for (const property of node.properties) {
          if (property.name && ts.isComputedPropertyName(property.name)) emitExpression(part, 'operation-list', property.name.expression, property);
          else if (propName(property.name)) add(file, 'operation-list', line(property), propName(property.name));
          else if (ts.isSpreadAssignment(property)) emitExpression(part, 'operation-list', property.expression);
        }
      }
      function visit(node) {
        if (file.startsWith('src/bridge/') && own(node)) {
          if ((ts.isFunctionDeclaration(node) || ts.isClassDeclaration(node)) && node.name) {
            exported(node.name.text, node);
            if (ts.isClassDeclaration(node)) for (const member of node.members) if (ts.isMethodDeclaration(member) && !own(member, ts.SyntaxKind.PrivateKeyword) && propName(member.name)) add(file, 'bridge-export', line(member), `${node.name.text}.${propName(member.name)}`);
          }
          if (ts.isVariableStatement(node)) for (const declaration of node.declarationList.declarations) if (ts.isIdentifier(declaration.name)) exported(declaration.name.text, declaration);
          if (ts.isExportDeclaration(node) && node.exportClause && ts.isNamedExports(node.exportClause)) for (const element of node.exportClause.elements) if (!element.isTypeOnly && !node.isTypeOnly) exported(element.name.text, element);
        }
        if (file.startsWith('src/bridge/') && ts.isExportDeclaration(node) && node.exportClause && ts.isNamedExports(node.exportClause)) for (const element of node.exportClause.elements) if (!element.isTypeOnly && !node.isTypeOnly) exported(element.name.text, element);
        if (file.startsWith('src/bridge/') && ts.isExportAssignment(node)) exported('default', { initializer: node.expression, getStart: () => node.getStart(part.sf) });
        if (ts.isObjectLiteralExpression(node)) {
          const properties = new Map(node.properties.filter(ts.isPropertyAssignment).map(property => [propName(property.name), property]));
          const route = properties.get('path');
          if (route && (properties.has('component') || properties.has('redirect') || properties.has('children') || /(?:^|\/)router(?:\.|\/)/.test(file))) emitExpression(part, 'route', route.initializer, route);
          for (const key of ['operation', 'op', 'cmd']) if (properties.has(key)) emitExpression(part, 'operation', properties.get(key).initializer, properties.get(key));
        }
        if (ts.isCallExpression(node)) {
          const method = nameOf(node.expression), receiver = ts.isPropertyAccessExpression(node.expression) ? node.expression.expression.getText(part.sf) : '';
          if (ipcCalls.has(method) || (['send', 'request', 'call'].includes(method) && /ipc|webview/i.test(receiver))) emitExpression(part, 'ipc-call', node.arguments[0], node);
          else if (ipcEvents.has(method) || (['on', 'once', 'subscribe'].includes(method) && ipcNamespaces.has(receiver))) emitExpression(part, 'ipc-event', node.arguments[0], node);
          else if (method === 'postMessage' && node.arguments[0]) {
            const message = node.arguments[0];
            if (ts.isObjectLiteralExpression(message)) for (const property of message.properties) {
              if (['cmd', 'operation', 'op'].includes(propName(property.name))) emitExpression(part, 'ipc-message', ts.isPropertyAssignment(property) ? property.initializer : property.name, node);
            }
            else if (/webview|ipc/i.test(receiver)) emitExpression(part, 'ipc-message', message, node);
          }
          if (file.startsWith('src/bridge/') && ['request', 'stateRequest', 'fetch'].includes(method) && node.arguments[0]) {
            const target = resolve(node.arguments[0], part);
            if (method === 'fetch' || target.values.some(value => /^\/?api\/|^https?:\/\//.test(value))) emitExpression(part, 'bridge-http', node.arguments[0], node);
          }
          if (/^(?:registerOperation|registerCommand|ipc_on|ipcOn|handle)$/.test(method) && (method !== 'handle' || /ipc/i.test(receiver))) emitExpression(part, 'operation-register', node.arguments[0], node);
        }
        if (ts.isBinaryExpression(node) && [ts.SyntaxKind.EqualsEqualsEqualsToken, ts.SyntaxKind.EqualsEqualsToken, ts.SyntaxKind.ExclamationEqualsEqualsToken, ts.SyntaxKind.ExclamationEqualsToken].includes(node.operatorToken.kind)) {
          if (discriminator(nameOf(node.left))) emitExpression(part, 'operation-branch', node.right, node);
          else if (discriminator(nameOf(node.right))) emitExpression(part, 'operation-branch', node.left, node);
        }
        if (ts.isSwitchStatement(node) && discriminator(nameOf(node.expression))) for (const clause of node.caseBlock.clauses) if (ts.isCaseClause(clause)) emitExpression(part, 'operation-branch', clause.expression, clause);
        if (ts.isVariableDeclaration(node) && ts.isIdentifier(node.name) && tableName(node.name.text) && node.initializer && !/^src\/router(?:\.|\/)/.test(file)) operationTable(node.initializer);
        ts.forEachChild(node, visit);
      }
      visit(part.sf);
    }
  }
  function scanNative(file) {
    const text = read(file);
    if (text === undefined) return;
    add(file, 'native-module', 1);
    const tokens = tokenizeNative(text), constants = new Map();
    // Only immutable literal initializers; ambiguous bindings remain dynamic.
    for (let i = 0; i < tokens.length - 3; i++) if (tokens[i].type === 'id' && tokens[i + 1].value === '=' && tokens[i + 2].type === 'string' && tokens[i + 3].value === ';') {
      const prefix = tokens.slice(Math.max(0, i - 6), i);
      if (!prefix.some(item => ['const', 'constexpr'].includes(item.value))) continue;
      const key = tokens[i].value;
      if (!constants.has(key)) constants.set(key, new Set());
      constants.get(key).add(tokens[i + 2].value);
    }
    const resolveTokens = range => {
      if (!range.length) return unknown();
      if (range.some(token => token.type === 'string') && range.every(token => token.type === 'string' || ['+', 'L', 'u8', 'u', 'U'].includes(token.value))) return { values: [range.filter(token => token.type === 'string').map(token => token.value).join('')], unresolved: false };
      if (range.length === 1 && constants.get(range[0].value)?.size === 1) return { values: [...constants.get(range[0].value)], unresolved: false };
      return unknown();
    };
    const emit = (kind, range, anchor) => {
      const resolved = resolveTokens(range), line = lineAt(text, anchor.start);
      for (const operation of resolved.values) add(file, kind, line, operation);
      if (resolved.unresolved) add(file, `${kind}-dynamic`, line, undefined, { dynamic: true,
        expression: range.length ? text.slice(range[0].start, range.at(-1).end) : '<missing expression>', unresolvedReason: 'Native expression is not statically enumerable' });
    };
    for (let i = 0; i < tokens.length; i++) {
      const token = tokens[i];
      if (/^(?:ipc_on|ipc_on_async|register_operation|register_command|RegisterOperation|RegisterCommand)$/.test(token.value) && tokens[i + 1]?.value === '(') {
        let end = i + 2, depth = 0;
        while (end < tokens.length) {
          const value = tokens[end].value;
          if (depth === 0 && [',', ')'].includes(value)) break;
          if (['(', '{', '['].includes(value)) depth++;
          if ([')', '}', ']'].includes(value)) depth--;
          end++;
        }
        const range = tokens.slice(i + 2, end);
        if (range[0]?.value !== 'const') emit('native-ipc', range, token);
      }
      if (token.type === 'id' && discriminator(token.value)) {
        if (['==', '!=', '===', '!=='].includes(tokens[i + 1]?.value)) {
          let end = i + 2;
          while (end < tokens.length && ![')', '&&', '||', ';', '?', ',', '{', '}'].includes(tokens[end].value)) end++;
          emit('native-operation', tokens.slice(i + 2, end), token);
        }
        if (['==', '!=', '===', '!=='].includes(tokens[i - 1]?.value) && tokens[i - 2]?.type === 'string') emit('native-operation', [tokens[i - 2]], token);
      }
      if (token.value === 'switch' && tokens[i + 1]?.value === '(') {
        let close = i + 2;
        while (close < tokens.length && tokens[close].value !== ')') close++;
        if (tokens.slice(i + 2, close).some(item => item.type === 'id' && discriminator(item.value))) {
          let cursor = close + 1, depth = 0;
          for (; cursor < tokens.length; cursor++) {
            if (tokens[cursor].value === '{') depth++;
            if (tokens[cursor].value === '}' && --depth === 0) break;
            if (tokens[cursor].value === 'case' && depth === 1) {
              let end = cursor + 1;
              while (end < tokens.length && tokens[end].value !== ':') end++;
              emit('native-operation', tokens.slice(cursor + 1, end), tokens[cursor]);
            }
          }
        }
      }
      let assignment = i + 1;
      if (tokens[assignment]?.value === '[') { while (assignment < tokens.length && tokens[assignment].value !== ']') assignment++; assignment++; }
      if (token.type === 'id' && tableName(token.value) && tokens[assignment]?.value === '=') {
        let end = assignment + 1;
        while (end < tokens.length && tokens[end].value !== ';') end++;
        const range = tokens.slice(assignment + 1, end), names = range.filter(item => item.type === 'string');
        for (const item of names) add(file, 'native-operation-list', lineAt(text, item.start), item.value);
        if (!names.length || range.some(item => item.type === 'id' && !['std', 'string', 'vector', 'array', 'const', 'char', 'auto'].includes(item.value))) emit('native-operation-list', range, token);
      }
    }
  }
  for (const file of [...sourceFiles].sort()) {
    if (/\.min\.[cm]?js$/i.test(file)) continue; // Bundled third-party assets have no first-party capability entry.
    if (/(?:^|\/)(?:tests?|fixtures?|[\w.-]*Tests)(?:\/|$)|(?:[._-](?:test|selftest|spec))\./i.test(file)) continue;
    if (JS.has(path.extname(file))) scanJs(file); else scanNative(file);
  }
  for (const file of [...manifests].sort()) {
    const text = read(file);
    if (text === undefined) continue;
    const tree = ts.parseJsonText(file, text);
    for (const diagnostic of tree.parseDiagnostics) error(file, `Package JSON: ${ts.flattenDiagnosticMessageText(diagnostic.messageText, ' ')}`);
    const object = tree.statements[0]?.expression;
    if (!object || !ts.isObjectLiteralExpression(object)) { error(file, 'Package manifest must be a JSON object'); continue; }
    const scripts = object.properties.find(property => propName(property.name) === 'scripts')?.initializer;
    if (scripts && ts.isObjectLiteralExpression(scripts)) for (const property of scripts.properties) if (ts.isPropertyAssignment(property)) add(file, 'package-script', lineAt(text, property.getStart(tree)), propName(property.name), { command: ts.isStringLiteralLike(property.initializer) ? property.initializer.text : property.initializer.getText(tree) });
  }
  // Parse/read gaps are explicit uncovered surfaces so a failed scan cannot look complete.
  for (const diagnostic of diagnostics) add(diagnostic.path, 'discovery-gap', 1, undefined, { domain: 'unclassified', diagnostic: diagnostic.message });
  const adapterIds = new Set();
  const registrations = adapters.map(adapter => {
    if (!adapter || typeof adapter.id !== 'string' || !adapter.id || adapterIds.has(adapter.id)) throw new TypeError('Adapter ids must be nonempty and unique');
    adapterIds.add(adapter.id);
    if (adapter.scope !== undefined && !SCOPES.includes(adapter.scope)) throw new TypeError(`Invalid adapter scope: ${adapter.id}`);
    if (adapter.evidenceInputs !== undefined && !Array.isArray(adapter.evidenceInputs)) throw new TypeError(`evidenceInputs must be an array: ${adapter.id}`);
    const matches = (adapter.evidenceInputs || []).map(input => {
      if (typeof input !== 'string' || !input.trim()) throw new TypeError(`Invalid evidenceInputs: ${adapter.id}`);
      const normalized = slash(input).replace(/^\.\//, '').replace(/\/$/, '');
      if (path.isAbsolute(input) || /^[A-Za-z]:|^\/|(?:^|\/)\.\.(?:\/|$)/.test(normalized)) throw new TypeError(`evidenceInputs must stay repo-relative: ${adapter.id}`);
      const glob = normalized.split('**').map(part => part.split('*').map(piece => piece.replace(/[.+?^${}()|[\]\\]/g, '\\$&')).join('[^/]*')).join('.*');
      return new RegExp(`^${glob}(?:/.*)?$`);
    });
    return { id: adapter.id, scope: adapter.scope || 'unspecified', matches };
  });
  for (const surface of surfaces) {
    const associated = registrations.filter(adapter => adapter.matches.some(pattern => pattern.test(surface.path)));
    surface.adapterIds = associated.map(adapter => adapter.id).sort();
    surface.evidenceScopes = [...new Set(associated.map(adapter => adapter.scope))].sort();
    if (associated.length && surface.kind !== 'discovery-gap') surface.coverage = 'REGISTERED_TEST';
  }
  surfaces.sort((a, b) => a.path.localeCompare(b.path, 'en') || a.line - b.line || a.kind.localeCompare(b.kind, 'en') || a.id.localeCompare(b.id, 'en'));
  function counts(items) {
    const registered = items.filter(item => item.coverage === 'REGISTERED_TEST').length, dynamic = items.filter(item => item.dynamic).length;
    return { surfaces: items.length, registered, uncovered: items.length - registered, dynamic,
      unclassified: items.filter(item => item.domain === 'unclassified').length,
      totalSurfaces: items.length, registeredTestSurfaces: registered, notCoveredSurfaces: items.length - registered, dynamicSurfaces: dynamic,
      byScope: Object.fromEntries([...SCOPES, 'unspecified'].map(scope => [scope, items.filter(item => item.evidenceScopes.includes(scope)).length])) };
  }
  const domains = domainRules.map(({ id, title }) => ({ id, title, ...counts(surfaces.filter(surface => surface.domain === id)), adapterIds: [...new Set(surfaces.filter(surface => surface.domain === id).flatMap(surface => surface.adapterIds))].sort() }));
  const summary = { ...counts(surfaces), adapterCount: adapters.length,
    byDomain: Object.fromEntries(domains.map(({ id, title, adapterIds, ...stats }) => [id, stats])),
    byKind: Object.fromEntries([...new Set(surfaces.map(surface => surface.kind))].sort().map(kind => [kind, surfaces.filter(surface => surface.kind === kind).length])),
    coverageMeaning: 'REGISTERED_TEST associates source paths with registered adapters; no test was executed by this inventory.',
    behavioralVerified: false,
    registrationComplete: surfaces.length > 0 && surfaces.every(surface => surface.coverage === 'REGISTERED_TEST') && !surfaces.some(surface => surface.dynamic || surface.domain === 'unclassified') && diagnostics.length === 0,
    discovery: { roots: roots.filter(directory => fs.existsSync(path.join(root, directory))), filesScanned: scannedFiles.size, manifestCount: manifests.size, diagnostics,
      limitations: ['Static discovery; dynamic expressions require review/runtime enumeration.', 'File/prefix source association does not prove every operation in a file was exercised.', 'Native lexical discovery does not preprocess C++ or prove reachability; conditional registrations are candidates.'] } };
  return { schemaVersion: 1, domains, surfaces, summary, sourceExclusions:[...sourceExclusions.values()].sort((a,b)=>a.path.localeCompare(b.path,'en')) };
}

// Ignore comments, character literals and embedded raw-string scripts. Their
// original source modules are scanned separately rather than counting embeds twice.
function tokenizeNative(text) {
  const tokens = [];
  let i = 0;
  while (i < text.length) {
    if (/\s/.test(text[i])) { i++; continue; }
    if (text.startsWith('//', i)) { const end = text.indexOf('\n', i + 2); i = end < 0 ? text.length : end + 1; continue; }
    if (text.startsWith('/*', i)) { const end = text.indexOf('*/', i + 2); i = end < 0 ? text.length : end + 2; continue; }
    const start = i, raw = /^(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(/.exec(text.slice(i, i + 24));
    if (raw) { const end = text.indexOf(`)${raw[1]}"`, i + raw[0].length); i = end < 0 ? text.length : end + raw[1].length + 2; tokens.push({ type: 'raw', value: '<embedded raw string>', start, end: i }); continue; }
    if (text[i] === '"' || text[i] === "'") {
      const quote = text[i++];
      let value = '';
      while (i < text.length && text[i] !== quote) {
        if (text[i] === '\\' && i + 1 < text.length) { const escaped = text[++i]; value += ({ n: '\n', r: '\r', t: '\t' })[escaped] ?? escaped; i++; }
        else value += text[i++];
      }
      if (i < text.length) i++;
      tokens.push({ type: quote === '"' ? 'string' : 'char', value, start, end: i }); continue;
    }
    if (/[A-Za-z_$]/.test(text[i])) {
      i++; while (i < text.length && /[A-Za-z0-9_$]/.test(text[i])) i++;
      tokens.push({ type: 'id', value: text.slice(start, i), start, end: i }); continue;
    }
    const operator = ['===', '!==', '==', '!=', '&&', '||', '=>'].find(value => text.startsWith(value, i));
    i += operator?.length || 1;
    tokens.push({ type: 'punct', value: operator || text[start], start, end: i });
  }
  return tokens;
}
