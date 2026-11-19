import fs from 'node:fs';
import { createHash } from 'node:crypto';
import ts from 'typescript';

const sourceSha256 = '2eb89e3244f44baeb508ccfaa6d98c73cc1849e4b7bdb08eba71b165aae52726';
const K = ts.SyntaxKind;
const norm = s => s.trim().toUpperCase();
const familyTests = [
  ['"AOKZOE"==oe.Manufacturer', 'AOKZOE'],
  ['oe.Manufacturer&&-1!=oe.Manufacturer.indexOf("ONE-NETBOOK")', 'ONE-NETBOOK'],
  ['"ASUSTeK COMPUTER INC."==oe.Manufacturer', 'ASUSTeK COMPUTER INC.'],
  ['"Micro-Star International Co., Ltd."==oe.Manufacturer', 'Micro-Star International Co., Ltd.'],
];

export function auditOneXConsoleBattery(file, catalogRows) {
  const bytes = fs.readFileSync(file);
  if (createHash('sha256').update(bytes).digest('hex') !== sourceSha256)
    throw new Error('ONEXCONSOLE_BATTERY_AUDIT_SOURCE_MISMATCH');
  const source = ts.createSourceFile('background.js', bytes.toString('utf8'), ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
  if (source.parseDiagnostics.length) throw new Error('ONEXCONSOLE_BATTERY_AUDIT_PARSE_FAILED');
  const start = source.text.indexOf('q.usePawnIOMode=');
  const end = source.text.indexOf('J.initEc(O,M,z,0,1,N)');
  if (start < 0 || end <= start) throw new Error('ONEXCONSOLE_BATTERY_AUDIT_MODULE_MISSING');

  function positives(expr) {
    if (ts.isParenthesizedExpression(expr)) return positives(expr.expression);
    if (!ts.isBinaryExpression(expr)) return [];
    const op = expr.operatorToken.kind;
    if (op === K.CommaToken) return positives(expr.right);
    if (op === K.BarBarToken || op === K.AmpersandAmpersandToken) return [...positives(expr.left), ...positives(expr.right)];
    if (op !== K.EqualsEqualsToken && op !== K.EqualsEqualsEqualsToken) return [];
    return productLiteral(expr);
  }

  function productLiteral(expr) {
    return [[expr.left, expr.right], [expr.right, expr.left]].flatMap(([left, right]) =>
      left.getText(source) === 'oe.Product' && ts.isStringLiteral(right) ? [right.text] : []);
  }

  const branches = [];
  function visit(node) {
    if (node.end < start || node.pos > end) return;
    if (ts.isBinaryExpression(node) && node.operatorToken.kind === K.EqualsToken &&
        node.left.getText(source) === 'q.supportFunc.enableBatteryProtection' && node.right.getText(source) === '!0') {
      const conditions = [], exactProducts = new Set();
      for (let child = node, parent = child.parent; parent; child = parent, parent = parent.parent) {
        let condition;
        if (ts.isIfStatement(parent) && child === parent.thenStatement) condition = parent.expression;
        if (ts.isConditionalExpression(parent) && child === parent.whenTrue) condition = parent.condition;
        if (ts.isBinaryExpression(parent) && parent.operatorToken.kind === K.AmpersandAmpersandToken && child === parent.right)
          condition = parent.left;
        if (condition) {
          conditions.push(condition.getText(source));
          for (const product of positives(condition)) exactProducts.add(product);
        }
        // The terminal X2 Mini Pro and AOKZOE A1X branches return on a mismatched Product.
        if (ts.isBlock(parent)) for (const prior of parent.statements) {
          if (prior === child) break;
          if (!ts.isIfStatement(prior) || prior.elseStatement || !ts.isReturnStatement(prior.thenStatement)) continue;
          const expr = prior.expression;
          if (ts.isBinaryExpression(expr) && expr.operatorToken.kind === K.ExclamationEqualsToken)
            for (const product of productLiteral(expr)) exactProducts.add(product);
        }
      }
      const manufacturer = familyTests.find(([pattern]) => conditions.some(c => c.includes(pattern)))?.[1] ?? null;
      if (!manufacturer) throw new Error('ONEXCONSOLE_BATTERY_AUDIT_UNCLASSIFIED_BRANCH');
      branches.push({ offset: node.getStart(source), manufacturer, exactProducts: [...exactProducts],
        positiveConditions: conditions.filter(c => c.length < 350),
        firmwareConditions: conditions.filter(c => /\boe\.(?:BIOS|Ec)/.test(c) && c.length < 220) });
    }
    ts.forEachChild(node, visit);
  }
  visit(source);
  if (branches.length !== 29 || branches.some(b => !b.exactProducts.length) ||
      new Set(branches.flatMap(b => b.exactProducts)).size !== 39 ||
      !branches.some(b => b.manufacturer === 'ONE-NETBOOK' && b.exactProducts.includes('ONEXPLAYER X2Mini PRO')) ||
      !branches.some(b => b.manufacturer === 'AOKZOE' && b.exactProducts.length === 1 && b.exactProducts[0] === 'AOKZOE A1X'))
    throw new Error(`ONEXCONSOLE_BATTERY_AUDIT_BRANCH_COVERAGE_CHANGED: ${branches.length} branches, no Product at offsets ${branches.filter(b => !b.exactProducts.length).map(b => b.offset).join(', ')}`);

  const offerings = branches.flatMap(branch => branch.exactProducts.map(product => {
    // Literal comparison only: OEM Product and YMCC system/baseboard predicates
    // are not interchangeable without an observed DMI pair.
    const matchingLiterals = catalogRows.filter(row => {
      const r = row.identity;
      const m = r.manufacturerMatch === 'ordinal-contains'
        ? r.manufacturers.some(v => branch.manufacturer.includes(v))
        : r.manufacturers.some(v => norm(v) === norm(branch.manufacturer));
      return m && r.values.some(v => r.valueMatch === 'ordinal-exact' ? v === product : norm(v) === norm(product));
    }).map(row => ({ ruleId: row.ruleId, class: row.deviceClass, field: row.identity.field, firmwareGate: row.firmwareGate }));
    return { manufacturer: branch.manufacturer, product, offset: branch.offset,
      firmwareConditions: branch.firmwareConditions, matchingLiterals,
      status: matchingLiterals.length ? 'runtime-literal-present-not-validated' : 'vendor-enabled-literal-without-runtime-match' };
  }));
  return { scope: 'Static OneXConsole 0.10.4-fix3 battery-enable branches; no hardware, native transaction or admission implied',
    sourceSha256, offsetUnit: 'UTF-16 code units', branches, offerings,
    manufacturerOnly: branches.filter(b => !b.exactProducts.length) };
}
