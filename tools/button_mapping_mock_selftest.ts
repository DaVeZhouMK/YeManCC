import { evaluateButtonMapping, emptyButtonSession, mapButtons } from '../src/bridge/buttonMappingMock';

const rules = {
  south: { closure: 'CLOSED' as const, action: { type: 'button' as const, control: 'a' as const } },
  lt: { closure: 'CLOSED' as const, action: { type: 'trigger' as const, control: 'left' as const, threshold: .7 } },
};
const mapped = mapButtons(rules, new Set(['south', 'lt']));
if (!mapped.ok || !mapped.buttons.has('a') || mapped.triggers.left !== .7 || mapped.triggers.right !== 0) throw new Error('B1 mapping failed');
if (mapButtons({ south: { closure: 'UNENCLOSED' } }, new Set(['south'])).ok) throw new Error('B1 accepted unclosed route');

const t3 = {
  shifted: { closure: 'CLOSED' as const, action: { type: 'shift' as const, modifier: 'lb', physical: 'south', emit: { type: 'button' as const, control: 'x' as const } } },
  chord: { closure: 'CLOSED' as const, action: { type: 'chord' as const, keys: ['lb', 'rb'], emit: { type: 'button' as const, control: 'start' as const } } },
  hold: { closure: 'CLOSED' as const, action: { type: 'hold' as const, physical: 'east', durationMs: 500, emit: { type: 'button' as const, control: 'b' as const } } },
  tog: { closure: 'CLOSED' as const, action: { type: 'toggle' as const, physical: 'north', emit: { type: 'button' as const, control: 'y' as const } } },
};

const noShift = evaluateButtonMapping(t3, new Set(['south'])).result;
if (!noShift.ok || noShift.buttons.size !== 0) throw new Error('shift without modifier leaked');
const shift = evaluateButtonMapping(t3, new Set(['lb', 'south'])).result;
if (!shift.ok || !shift.buttons.has('x')) throw new Error('shift failed');
const incomplete = evaluateButtonMapping(t3, new Set(['lb'])).result;
if (!incomplete.ok || incomplete.buttons.size !== 0) throw new Error('incomplete chord leaked');
const chord = evaluateButtonMapping(t3, new Set(['lb', 'rb'])).result;
if (!chord.ok || !chord.buttons.has('start')) throw new Error('chord failed');

let session = emptyButtonSession();
let hold = evaluateButtonMapping(t3, new Set(['east']), 100, session);
session = hold.session;
if (!hold.result.ok || hold.result.buttons.has('b')) throw new Error('hold fired early');
hold = evaluateButtonMapping(t3, new Set(['east']), 700, session);
session = hold.session;
if (!hold.result.ok || !hold.result.buttons.has('b')) throw new Error('hold did not fire');
hold = evaluateButtonMapping(t3, new Set(), 800, session);
if (!hold.result.ok || hold.result.buttons.size !== 0 || hold.session.holdStartedAt.hold !== undefined) throw new Error('hold sticky after release');

session = emptyButtonSession();
let tog = evaluateButtonMapping(t3, new Set(['north']), 0, session);
session = tog.session;
if (!tog.result.ok || !tog.result.buttons.has('y')) throw new Error('toggle on failed');
tog = evaluateButtonMapping(t3, new Set(['north']), 10, session);
session = tog.session;
if (!tog.result.ok || !tog.result.buttons.has('y')) throw new Error('toggle dropped while held');
tog = evaluateButtonMapping(t3, new Set(), 20, session);
session = tog.session;
if (!tog.result.ok || !tog.result.buttons.has('y')) throw new Error('toggle did not latch');
tog = evaluateButtonMapping(t3, new Set(['north']), 30, session);
if (!tog.result.ok || tog.result.buttons.size !== 0) throw new Error('toggle off failed');
console.log('button mapping mock selftest: PASS');
