import { normalizeFanNodes, validateFanNodes } from '@/bridge/fanCurve';

const base = [
  { tempC: 0, dutyPercent: 0 },
  { tempC: 40, dutyPercent: 20 },
  { tempC: 70, dutyPercent: 45 },
  { tempC: 100, dutyPercent: 90 },
];

const node1Duty = normalizeFanNodes(base, 0, 'dutyPercent', 50);
if (node1Duty[0].dutyPercent !== 50 || node1Duty[1].dutyPercent < 50 || !validateFanNodes(node1Duty)) {
  throw new Error(`node1 duty edit was locked or produced an invalid curve: ${JSON.stringify(node1Duty)}`);
}
const node1Temp = normalizeFanNodes(node1Duty, 0, 'tempC', 55);
if (node1Temp[0].tempC !== 0 || !validateFanNodes(node1Temp)) {
  throw new Error(`node1 temperature was not anchored at 0°C: ${JSON.stringify(node1Temp)}`);
}
const node3Duty = normalizeFanNodes(base, 2, 'dutyPercent', 10);
if (node3Duty[2].dutyPercent < node3Duty[1].dutyPercent || node3Duty[0].dutyPercent !== 0 || !validateFanNodes(node3Duty)) {
  throw new Error(`editing a later duty changed an earlier node or broke monotonicity: ${JSON.stringify(node3Duty)}`);
}
const node4Duty = normalizeFanNodes(base, 3, 'dutyPercent', 0);
if (node4Duty[3].dutyPercent !== 50 || !validateFanNodes(node4Duty)) {
  throw new Error(`node4 minimum was not enforced: ${JSON.stringify(node4Duty)}`);
}
const node4BelowNode3 = normalizeFanNodes([
  { tempC: 0, dutyPercent: 0 },
  { tempC: 40, dutyPercent: 20 },
  { tempC: 70, dutyPercent: 80 },
  { tempC: 100, dutyPercent: 90 },
], 3, 'dutyPercent', 50);
if (node4BelowNode3[3].dutyPercent !== 80 || !validateFanNodes(node4BelowNode3)) {
  throw new Error(`node4 was allowed below node3: ${JSON.stringify(node4BelowNode3)}`);
}
console.log(JSON.stringify({ ok: true, checks: [
  'node1-duty-editable', 'node1-temp-fixed-zero',
  'later-edit-does-not-rewrite-earlier-duty', 'node4-minimum-50',
  'node4-not-below-node3',
] }));
