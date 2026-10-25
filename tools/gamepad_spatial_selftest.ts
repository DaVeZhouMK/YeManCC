import { spatialNavigationTarget, nearestSpatialTarget, spatialNavigationRect } from '../src/gamepad/spatial';

type Fake = { dataset: { gpRow: string; gpCol: string }; id: string };
const node = (id: string, row: number, col: number) =>
  ({ id, closest: () => null, dataset: { gpRow: String(row), gpCol: String(col) } } as unknown as HTMLElement);

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

// Geometry fixtures cover the responsive pages used at 720p and 800p.
// A row edge must stop horizontally; vertical navigation visits the next row.
const scope = {};
const rectangle = (id: string, x: number, y: number, width = 90, height = 32, options: {hidden?: boolean; disabled?: boolean; scope?: object; pane?: {dataset:{gpPane:string}}} = {}) => ({
  id, tagName: 'BUTTON', parentElement: null,
  getBoundingClientRect: () => ({left:x, right:x+width, top:y, bottom:y+height, width, height}),
  closest: (selector: string) => selector === '[data-gp-scope]' ? options.scope || scope : selector === '[data-gp-pane]' ? options.pane || null : selector.includes('[hidden]') && options.hidden ? {} : null,
  matches: () => !!options.disabled,
} as unknown as HTMLElement);
const previousStyle = globalThis.getComputedStyle;
globalThis.getComputedStyle = (() => ({display:'block', visibility:'visible'})) as unknown as typeof getComputedStyle;
try {
  const current = rectangle('current', 100, 100), right = rectangle('right', 210, 100),
    diagonal = rectangle('diagonal', 220, 45), nextRow = rectangle('next-row', 5, 145),
    farAligned = rectangle('far-aligned', 100, 250), lowerRight = rectangle('lower-right', 220, 150),
    sameRowSwitch = rectangle('switch', 310, 104, 40, 24);
  const rects = [current, right, diagonal, nextRow, farAligned, lowerRight, sameRowSwitch];
  expect(nearestSpatialTarget(rects, current, {dx:1,dy:0}), right, 'nearest right stays on row');
  expect(nearestSpatialTarget(rects, sameRowSwitch, {dx:1,dy:0}), null, 'row edge never falls back diagonally');
  expect(nearestSpatialTarget(rects, current, {dx:0,dy:1}), nextRow, 'next row wins over distant aligned control');
  expect(nearestSpatialTarget(rects, right, {dx:0,dy:-1}), diagonal, 'up visits adjacent row');
  expect(nearestSpatialTarget([current,sameRowSwitch], current, {dx:0,dy:1}), null, 'unequal control heights do not create vertical stops on same row');
  const hidden = rectangle('hidden', 200, 100,90,32,{hidden:true}), disabled=rectangle('disabled',210,100,90,32,{disabled:true}), foreign=rectangle('foreign',220,100,90,32,{scope:{}});
  expect(nearestSpatialTarget([current,hidden,disabled,foreign], current, {dx:1,dy:0}), null, 'hidden disabled and foreign scope excluded');
  expect(nearestSpatialTarget([current,right], current, {dx:1,dy:1}), null, 'diagonal command is a no-op');
  const thinAbove=rectangle('thin-above',100,100,200,6),thinBelow=rectangle('thin-below',100,220,200,6),tallBeside=rectangle('tall-beside',320,207,70,32);
  expect(nearestSpatialTarget([thinAbove,thinBelow,tallBeside],thinAbove,{dx:0,dy:1}),thinBelow,'different heights in next row retain the visual column');
  const tallCard=rectangle('tall-card',100,199,130,72),edgeOverlap=rectangle('edge-overlap',500,252,200,40);
  expect(nearestSpatialTarget([tallCard,edgeOverlap],tallCard,{dx:1,dy:0}),null,'small edge overlap is not a horizontal row');
  const settingsPane={dataset:{gpPane:'settings'}},listPane={dataset:{gpPane:'list'}},headerPane={dataset:{gpPane:'settings'}};
  const record=rectangle('record',500,250,200,40,{pane:settingsPane}),trigger=rectangle('trigger',500,375,200,38,{pane:settingsPane}),foreignRow=rectangle('foreign-row',100,310,130,72,{pane:listPane}),sharedHeader=rectangle('settings-header',500,100,200,40,{pane:headerPane});
  expect(nearestSpatialTarget([record,trigger,foreignRow,sharedHeader],record,{dx:0,dy:1}),trigger,'another pane cannot intercept vertical navigation');
  expect(nearestSpatialTarget([record,trigger,foreignRow,sharedHeader],trigger,{dx:0,dy:-1}),record,'settings pane returns within its own column');
  expect(nearestSpatialTarget([record,trigger,foreignRow,sharedHeader],record,{dx:0,dy:-1}),sharedHeader,'shared pane identifier links an independent header');
  expect(nearestSpatialTarget([record,trigger,foreignRow,sharedHeader],sharedHeader,{dx:0,dy:1}),record,'shared header re-enters its own settings pane');
  const scrollPane = {parentElement:scope, scrollTop:250, scrollLeft:0, offsetHeight:200, getBoundingClientRect:()=>({height:200})};
  const scrolled = rectangle('scrolled', 100, -100);
  Object.assign(scrolled,{parentElement:scrollPane});
  globalThis.getComputedStyle = (()=>({display:'block',visibility:'visible',overflowY:'auto',overflowX:'hidden'})) as unknown as typeof getComputedStyle;
  expect(spatialNavigationRect(scrolled).top,150,'pane scrolling keeps logical layout position');
  expect(nearestSpatialTarget([current,scrolled,farAligned], current, {dx:0,dy:1}), scrolled, 'scrolled-out adjacent row remains reachable above distant aligned controls');
} finally { globalThis.getComputedStyle = previousStyle; }
console.log('gamepad spatial selftest: 25/25 passed');
