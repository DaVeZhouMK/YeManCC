import { auditBatteryRouteIdentity } from './battery_route_identity_audit.mjs';

const dmiFields = { ProductName: 'boardProduct', SystemModel: 'systemProduct' };
const reviewedProcessor = 'AMD Ryzen AI 9 HX 370';
const literal = /^[ -~]+$/;

export function projectBatteryFactoryRoutes(factoryRows, rules) {
  const selectors = [], unresolved = [];
  const classes = new Map(rules.map(rule => [rule.ruleId, rule.deviceClass]));
  for (const row of factoryRows) {
    const { deviceClass, effectiveStatus, coverage, identityEvidence, ymccRoutes } = row;
    if (effectiveStatus === 'not-declared' && ymccRoutes.length === 0) continue;
    const source = { deviceClass, effectiveStatus, coverage };
    if (identityEvidence.length === 0) unresolved.push({ ...source, reason: 'no-factory-identity-branch' });
    for (const branch of identityEvidence) {
      const [maker, product] = branch.switches;
      const field = Object.hasOwn(dmiFields, product?.field) ? dmiFields[product.field] : null;
      if (branch.switches.length !== 2 || maker?.field !== 'ManufacturerName' || !field || branch.conditions.length ||
          branch.switches.some(s => s.hasDefault || !s.values.length || s.values.some(v => !literal.test(v)))) {
        unresolved.push({ ...source, line: branch.line, reason: 'not-a-direct-literal-branch' });
        continue;
      }
      for (const manufacturer of maker.values) for (const value of product.values)
        selectors.push({ ...source, line: branch.line, manufacturer, field, value, registered: ymccRoutes.length > 0 });
    }
  }

  // The other-field contrast is not evidence that this field is populated on a device.
  const samples = selectors.flatMap(({manufacturer, field, value}, index) => {
    const other = field === 'boardProduct' ? 'systemProduct' : 'boardProduct';
    const identity = { manufacturer, boardProduct: '', systemProduct: '', processor: '' };
    return [
      { name: `${index}:source-field`, identity: { ...identity, [field]: value } },
      { name: `${index}:source-field-with-processor`, identity: { ...identity, [field]: value, processor: reviewedProcessor } },
      { name: `${index}:other-field`, identity: { ...identity, [other]: value } },
    ];
  });
  const {samples: projected} = auditBatteryRouteIdentity(rules, samples);
  const counts = {};
  const rows = selectors.map(({registered, ...r}, i) => {
    const [source, withProcessor, other] = projected.slice(i * 3, i * 3 + 3).map(x => x.selection);
    const sameClass = s => s.status === 'unique' && classes.get(s.ruleId) === r.deviceClass;
    const status = sameClass(source) ? 'same-field'
      : source.status !== 'no-match' ? 'source-field-conflict'
      : sameClass(withProcessor) ? 'same-field-processor-gated'
      : withProcessor.status !== 'no-match' ? 'source-field-processor-conflict'
      : sameClass(other) ? 'different-field'
      : registered ? 'registered-no-literal-match' : 'not-registered';
    counts[status] = (counts[status] ?? 0) + 1;
    return { ...r, status, source, withProcessor, other };
  });
  const factoryClasses = new Set(factoryRows.map(row => row.deviceClass));
  const rowsByClass = Map.groupBy(rows, row => row.deviceClass);
  const unresolvedClasses = new Set(unresolved.map(row => row.deviceClass));
  const candidatesWithAllFactoryLiteralsOnSourceField = [...rowsByClass].filter(([deviceClass, group]) =>
    !unresolvedClasses.has(deviceClass) && group.every(row => row.effectiveStatus === 'declared-runtime-probe-required' &&
      ['same-field', 'same-field-processor-gated'].includes(row.status))).map(([deviceClass]) => deviceClass);
  return { counts, reviewedProcessorWitness: reviewedProcessor, unresolved, candidatesWithAllFactoryLiteralsOnSourceField,
    rulesWithoutFactoryClass: rules.filter(rule => !factoryClasses.has(rule.deviceClass)).map(rule => rule.ruleId), rows };
}
