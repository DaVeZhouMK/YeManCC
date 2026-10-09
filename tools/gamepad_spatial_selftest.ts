import { spatialNavigationTarget } from '../src/gamepad/spatial';

type Fake = { dataset: { gpRow: string; gpCol: string }; id: string };
const node = (id: string, row: number, col: number) =>
  ({ id, dataset: { gpRow: String(row), gpCol: String(col) } } as unknown as HTMLElement);

const row0 = [node('temperature', 0, 0), node('rpm', 0, 1), node('toggle', 0, 2)];
const row1 = [node('preset', 1, 0), node('reset', 1, 1), node('motion', 1, 2)];
const nodes = [node('node-1', 2, 0), node('node-2', 2, 1), node('node-3', 2, 2), node('node-4', 2, 3)];
const all = [...row0, ...row1, ...nodes];

function expect(actual: unknown, expected: unknown, label: string): void {
  if (actual !== expected) throw new Error(`${label}: expected ${String(expected)}, got ${String(actual)}`);
}

expect(spatialNavigationTarget(all, row0[0], { dx: 1, dy: 0 }), row0[1], 'row0 right');
expect(spatialNavigationTarget(all, row0[0], { dx: -1, dy: 0 }), null, 'row0 left boundary');
expect(spatialNavigationTarget(all, row0[2], { dx: 1, dy: 0 }), null, 'row0 right boundary');
expect(spatialNavigationTarget(all, row0[1], { dx: 0, dy: 1 }), row1[1], 'down keeps column');
expect(spatialNavigationTarget(all, row1[1], { dx: 0, dy: 1 }), nodes[1], 'down to node row');
expect(spatialNavigationTarget(all, nodes[0], { dx: 1, dy: 0 }), nodes[1], 'node1 right to node2');
expect(spatialNavigationTarget(all, nodes[0], { dx: -1, dy: 0 }), null, 'node1 left boundary');
expect(spatialNavigationTarget(all, nodes[3], { dx: 1, dy: 0 }), null, 'node4 right boundary');
expect(spatialNavigationTarget(all, nodes[3], { dx: 0, dy: 1 }), null, 'bottom boundary');
expect(spatialNavigationTarget([node('legacy', 0, 0)], node('legacy', 0, 0), { dx: 1, dy: 0 }), null, 'explicit single-cell edge');

console.log('gamepad spatial selftest: 10/10 passed');
