// monitor_data_selftest.ts — 196/197 统一监控数据入口 harness（纯逻辑，无硬件、无 native）
//
// 196 覆盖：快照模式解析与 source 标记、in-flight 合并、takeover 确认、旧 native（unknown）
// 回退文件通路且不发 takeover、超时≠unknown（不回读文件）、available=false ⇒ null、
// 需求登记合并 {top,fps} 与释放一端不影响另一端、stop 后旧回复被丢弃、
// TopMonData 字段形状保真（FanView 只取 tempC）、文件通路跟随 setMonitorDataPowerControlDir。
//
// 197 覆盖（A 组反例转断言）：启动失败不伪装成功且有界重试/新意图重置预算、旧 disposer
// 不误停新订阅、未知 schema 拒收（不冒充 unknown、不回退文件）、同代际乱序序号拒收、
// native 重启（代际回退）接受、takeover 失败有界重试（≤预算）、无读者时停止经定时器收尾、
// 停止失效按字段自身时间戳（旧 fps 不因 top 新鲜而复活）。
//
// 裁决方独立探针 Isolated/Reviews/R197/review-monitor-hub.cjs（修复前 4/4 复现）在本 harness
// 之外单独运行；本文件用同一真实工厂（createMonitorDataHub）断言修复后行为。
//
// 198 覆盖：三态状态机（成功/拒绝/超时未知）、未确认 start 必达 stop、会话身份与重协调、
// 同会话低代际拒收、takeover 绑定会话/epoch、读回校准，以及**联调夹具**（形状桥）：native
// 冻结路径的实际 top 载荷文本（Batches\R198\evidence\frozen-top-payload-198.json，由 native
// 链⑦ 用产品唯一构造点 nativeMonitorTopPayloadText 生成）在前端不被当作新数据消费
// （可用环境变量 YMCC_R198_FROZEN_FIXTURE 覆盖夹具路径；默认按 cwd=src\YeManCC 解析）。
//
// 运行（隔离树内现打现跑，产物只落隔离区）：
//   node_modules/.bin/esbuild tools/monitor_data_selftest.ts --bundle --platform=node \
//     --format=cjs --outfile=../Patches/Tests/R196/monitor_data_selftest.cjs --tsconfig=tsconfig.json
//   node ../Patches/Tests/R196/monitor_data_selftest.cjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import {
  createMonitorDataHub,
  setMonitorDataPowerControlDir,
  type MonitorHubDeps,
} from '@/bridge/monitorData';

const NOW = 1_700_000_000_000;

interface FakeCall {
  cmd: string;
  args: Record<string, unknown>;
}

interface FakeDeps extends MonitorHubDeps {
  calls: FakeCall[];
  fileReads: string[];
}

function makeDeps(opts: {
  snapshot?: (call: number) => unknown;
  files?: Record<string, string>;
  now?: () => number;
}): FakeDeps {
  const calls: FakeCall[] = [];
  const fileReads: string[] = [];
  const files = opts.files ?? {};
  let snapshotCall = 0;
  return {
    calls,
    fileReads,
    now: opts.now ?? (() => NOW),
    invoke: async (cmd: string, args: object = {}) => {
      calls.push({ cmd, args: args as Record<string, unknown> });
      if (cmd === 'monitor.snapshot') {
        snapshotCall += 1;
        if (!opts.snapshot) throw new Error('unknown: monitor.snapshot');
        return opts.snapshot(snapshotCall);
      }
      if (cmd === 'monitor.start' || cmd === 'monitor.stop' || cmd === 'monitor.takeover') {
        return { ok: true };
      }
      return {};
    },
    setTimer: () => 1,
    clearTimer: () => {},
    readTextFile: async (path: string) => {
      fileReads.push(path);
      for (const [name, content] of Object.entries(files)) {
        if (path.endsWith(name)) return content;
      }
      throw new Error(`ENOENT ${path}`);
    },
  };
}

function snap(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    schema: 1,
    available: true,
    generation: 7,
    seq: 3,
    collectedAt: NOW - 200,
    heartbeatAt: NOW - 300,
    hwinfoOkAt: NOW - 400,
    topActive: true,
    fpsActive: true,
    hwDown: false,
    top: JSON.stringify({ ts: NOW - 200, tdpW: 15.5, tempC: 61, ac: 0, hasBattery: true, chargeW: -8, cpuUsage: 12 }),
    fps: JSON.stringify({ ts: NOW - 200, fps: 59.5, fps1: 48, gpu: 77, packagePower: 22, game: 'Hades', pid: 4242 }),
    ...overrides,
  };
}

const flush = (): Promise<void> => new Promise((resolve) => setTimeout(resolve, 0));

// 197：用例自建 deps（可控 invoke/时钟/定时器），仍使用真实工厂。
function rawHub(opts: {
  invoke: (cmd: string, args?: object, options?: { timeoutMs?: number }) => Promise<unknown>;
  now?: () => number;
  timers?: { setTimer: (fn: () => void, ms: number) => unknown; clearTimer: (h: unknown) => void };
  files?: Record<string, string>;
}): { hub: ReturnType<typeof createMonitorDataHub>; fileReads: string[] } {
  const fileReads: string[] = [];
  // 测试统一用假定时器：冻结时钟下真实定时器会造成"门未开→重排"空转（生产用真实时钟）。
  const timers = opts.timers ?? fakeTimers();
  const hub = createMonitorDataHub({
    invoke: opts.invoke,
    now: opts.now ?? (() => NOW),
    readTextFile: async (path: string) => {
      fileReads.push(path);
      for (const [name, content] of Object.entries(opts.files ?? {})) {
        if (path.endsWith(name)) return content;
      }
      throw new Error(`ENOENT ${path}`);
    },
    setTimer: timers.setTimer,
    clearTimer: timers.clearTimer,
  });
  return { hub, fileReads };
}

function fakeTimers(): {
  items: Array<{ fn: () => void; ms: number }>;
  setTimer: (fn: () => void, ms: number) => unknown;
  clearTimer: (h: unknown) => void;
  fireAll: () => void;
} {
  const items: Array<{ fn: () => void; ms: number }> = [];
  return {
    items,
    setTimer: (fn: () => void, ms: number) => {
      items.push({ fn, ms });
      return items.length;
    },
    clearTimer: () => {},
    fireAll: () => {
      const list = items.splice(0);
      for (const item of list) item.fn();
    },
  };
}

async function test(name: string, run: () => Promise<void>): Promise<void> {
  await run();
  console.log(`PASS ${name}`);
}

async function main(): Promise<void> {
  await test('snapshot 模式：解析/source/并发合并/takeover 仅一次', async () => {
    const deps = makeDeps({ snapshot: () => snap() });
    const hub = createMonitorDataHub(deps);
    const release = hub.acquire('display', { top: true, fps: true }); // 真实消费端先登记需求（本用例断言两路 payload）
    await flush();
    const [a, b, c] = await Promise.all([hub.read(), hub.read(), hub.read()]);
    assert.equal(deps.calls.filter((x) => x.cmd === 'monitor.snapshot').length, 1, '并发读取必须合并为一次 IPC');
    assert.notEqual(a, b, 'coalesced monitor readers own separate snapshots');
    assert.notEqual(a?.top, b?.top, 'nested monitor data is not shared');
    assert.equal(a?.source, 'snapshot');
    assert.equal(a?.top?.tdpW, 15.5);
    assert.equal(a?.top?.tempC, 61);
    assert.equal(a?.fps?.game, 'Hades');
    assert.equal(a?.fps?.pid, 4242);
    assert.equal(a?.heartbeatAt, NOW - 300);
    assert.equal(a?.hwinfoOkAt, NOW - 400);
    assert.equal(b?.seq, 3);
    assert.equal(c?.generation, 7);
    assert.equal(deps.calls.filter((x) => x.cmd === 'monitor.takeover').length, 1, 'takeover 只声明一次');
    assert.equal(hub.transport(), 'snapshot');
    assert.equal(hub.pendingReads(), 0);
    release();
  });

  await test('旧 native（unknown）⇒ 回退文件通路，且不发 takeover', async () => {
    const deps = makeDeps({
      snapshot: () => {
        throw new Error('unknown: monitor.snapshot');
      },
      files: {
        'topmon.json': JSON.stringify({ ts: NOW - 500, tdpW: 9, freqMhz: 3000, tempC: 55, ac: 1, hasBattery: false }),
        'fps-status.json': JSON.stringify({ ts: NOW - 500, fps: 30, fps1: 25, gpu: 50, packagePower: 11, game: 'X', pid: 7 }),
        'fps-monitor.hb': JSON.stringify({ ts: NOW - 600 }),
        'hwinfo-ok': String(NOW - 700),
      },
    });
    const hub = createMonitorDataHub(deps);
    const v = await hub.read();
    assert.equal(hub.transport(), 'files');
    assert.equal(v?.source, 'files');
    assert.equal(v?.top?.tempC, 55);
    assert.equal(v?.fps?.game, 'X');
    assert.equal(v?.heartbeatAt, NOW - 600);
    assert.equal(v?.hwinfoOkAt, NOW - 700);
    assert.equal(deps.calls.filter((x) => x.cmd === 'monitor.takeover').length, 0, '兼容通路不得声明 takeover');
    assert.equal(deps.fileReads.length, 4, '一次读取覆盖四文件');
  });

  await test('快照超时 ≠ unknown：不回读文件，下次仍可恢复', async () => {
    let n = 0;
    const deps = makeDeps({
      snapshot: () => {
        n += 1;
        if (n === 1) throw new Error('IPC command timed out: monitor.snapshot');
        return snap();
      },
      files: { 'topmon.json': JSON.stringify({ ts: NOW, tempC: 1 }) },
    });
    const hub = createMonitorDataHub(deps);
    assert.equal(await hub.read(), null);
    assert.equal(deps.fileReads.length, 0, '超时不得偷偷回读残留文件');
    assert.equal(hub.transport(), 'unknown');
    const v2 = await hub.read();
    assert.equal(v2?.source, 'snapshot');
    assert.equal(hub.transport(), 'snapshot');
  });

  await test('快照 available=false ⇒ null（不返回陈旧值、不回退）', async () => {
    const deps = makeDeps({ snapshot: () => snap({ available: false }) });
    const hub = createMonitorDataHub(deps);
    assert.equal(await hub.read(), null);
    assert.equal(hub.transport(), 'snapshot');
    assert.equal(deps.fileReads.length, 0);
  });

  await test('需求登记：合并 {top,fps}，释放一端不影响另一端', async () => {
    const deps = makeDeps({ snapshot: () => snap() });
    const hub = createMonitorDataHub(deps);
    const releaseTop = hub.acquire('topbar', { top: true });
    const releaseFps = hub.acquire('autofloat', { fps: true });
    await flush();
    const starts = deps.calls.filter((x) => x.cmd === 'monitor.start');
    assert.equal(starts.length, 2);
    assert.deepEqual(starts[0].args, { top: true, fps: false });
    assert.deepEqual(starts[1].args, { top: false, fps: true });
    releaseTop();
    await flush();
    const stops1 = deps.calls.filter((x) => x.cmd === 'monitor.stop');
    assert.equal(stops1.length, 1);
    assert.deepEqual(stops1[0].args, { top: true, fps: false });
    assert.equal(hub.applied().fps, true, '释放 top 不得停 fps');
    releaseFps();
    await flush();
    const stops2 = deps.calls.filter((x) => x.cmd === 'monitor.stop');
    assert.equal(stops2.length, 2);
    assert.deepEqual(stops2[1].args, { top: false, fps: true });
    assert.deepEqual(hub.applied(), { top: false, fps: false });
    // 幂等释放
    releaseFps();
    await flush();
    assert.equal(deps.calls.filter((x) => x.cmd === 'monitor.stop').length, 2);
  });

  await test('stop 后旧回复被丢弃（fps 与心跳清空；top 保留）', async () => {
    let resolveSnap: ((v: unknown) => void) | null = null;
    const deps = makeDeps({
      snapshot: () => new Promise((resolve) => { resolveSnap = resolve; }),
    });
    const hub = createMonitorDataHub(deps);
    const inflight = hub.read();
    await flush();
    const releaseFps = hub.acquire('autofloat', { fps: true });
    releaseFps(); // 释放时刻 now = NOW ⇒ fps.ts<=NOW 的旧回复必须被丢弃
    const releaseTop = hub.acquire('display', { top: true }); // top 有活跃需求（本用例断言 top 保留）
    await flush();
    resolveSnap!(snap({ collectedAt: NOW, heartbeatAt: NOW, hwinfoOkAt: 0 }));
    const v = await inflight;
    assert.equal(v?.fps, null, '旧 fps 数据不得复活');
    assert.equal(v?.heartbeatAt, null);
    assert.notEqual(v?.top, null, 'top 需求未释放 ⇒ 数据保留');
    releaseTop();
  });

  await test('top payload 字段形状与默认值保真（FanView 只取 tempC）', async () => {
    const deps = makeDeps({
      snapshot: () => snap({ top: JSON.stringify({ ts: NOW, tempC: 47 }) }),
    });
    const hub = createMonitorDataHub(deps);
    const release = hub.acquire('display', { top: true });
    await flush();
    const v = await hub.read();
    const expected = [
      'ac', 'batteryPercent', 'chargeW', 'cpuUsage', 'freqMhz', 'gpuClockMhz', 'gpuPowerW',
      'hasBattery', 'hwDown', 'remainMin', 'tdpW', 'tempC', 'thermalThrottleFound',
      'thermalThrottleMax', 'ts', 'virtualMemoryCommittedFound', 'virtualMemoryCommittedMb',
      'virtualMemoryLoadFound', 'virtualMemoryLoadPct',
    ];
    assert.deepEqual(Object.keys(v!.top!).sort(), expected.sort());
    assert.equal(v!.top!.tempC, 47);
    assert.equal(v!.top!.remainMin, -1);
    assert.equal(v!.top!.ac, 1);
    assert.equal(v!.top!.batteryPercent, -1);
    release();
  });

  await test('文件通路跟随 setMonitorDataPowerControlDir', async () => {
    setMonitorDataPowerControlDir('D:\\X\\PowerControl');
    const deps = makeDeps({
      snapshot: () => {
        throw new Error('unknown: monitor.snapshot');
      },
      files: { 'topmon.json': JSON.stringify({ ts: NOW, tempC: 40 }) },
    });
    const hub = createMonitorDataHub(deps);
    await hub.read();
    assert.ok(deps.fileReads.length > 0 && deps.fileReads.every((p) => p.startsWith('D:\\X\\PowerControl\\')));
  });

  // ── 197 反例转断言（A 组）────────────────────────────────────────────────
  await test('[197/198] 启动失败不伪装成功；read 不绕退避；新意图重置预算', async () => {
    let now = NOW;
    let starts = 0;
    const timers = fakeTimers();
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.start') {
          starts += 1;
          throw new Error('transient');
        }
        if (cmd === 'monitor.stop' || cmd === 'monitor.takeover') return { ok: true };
        // 失败阶段报告"不活跃"，否则读回校准会把启动判为已确认（198 契约）而不再重试。
        return snap({ topActive: false, fpsActive: false });
      },
      now: () => now,
      timers,
    });
    const release = hub.acquire('topbar', { top: true });
    await flush();
    assert.equal(starts, 1, '登记即尝试一次');
    assert.deepEqual(hub.applied(), { top: false, fps: false }, '失败不得伪装成功');
    assert.equal(hub.pending().top, true, '结果未知进入 pending');
    for (let i = 0; i < 10; i++) {
      await hub.read();
      await flush();
    }
    assert.equal(starts, 1, '退避未到：日常 read 不得绕退避耗预算');
    for (let i = 0; i < 20; i++) {
      now += 9000; // 超过最大退避：允许按退避推进
      await hub.read();
      await flush();
    }
    assert.ok(starts <= 6 && starts >= 2, `有界重试：starts=${starts}`);
    assert.equal(hub.applied().top, false);
    release();
    const release2 = hub.acquire('topbar', { top: true, fps: true });
    await flush();
    assert.ok(starts >= 7 && starts <= 12, `新意图应重置预算：starts=${starts}`);
    release2();
  });

  await test('[197] 旧 disposer 不误停新订阅（同 owner token）', async () => {
    const { hub } = rawHub({ invoke: async () => ({ ok: true }) /* start/stop 回执成功；snapshot 不参与 */ });
    const oldRelease = hub.acquire('display', { top: true });
    hub.acquire('display', { top: true });
    await flush();
    oldRelease();
    await flush();
    assert.equal(hub.applied().top, true, '旧释放函数不得取消新登记');
    assert.deepEqual(hub.desired(), { top: true, fps: false });
  });

  await test('[197] 未知 schema 拒收：不当成功数据、不冒充 unknown、不回退文件', async () => {
    const { hub, fileReads } = rawHub({
      invoke: async (cmd: string) => (cmd === 'monitor.snapshot' ? { ...snap(), schema: 999 } : { ok: true }),
      files: { 'topmon.json': JSON.stringify({ ts: NOW, tempC: 9 }) },
    });
    const view = await hub.read();
    assert.equal(view, null);
    assert.equal(hub.transport(), 'snapshot', '未知 schema 不得触发 unknown 回退');
    assert.equal(fileReads.length, 0, '不得偷读残留文件');
  });

  await test('[197] 同代际乱序序号拒收、新序号接受', async () => {
    let call = 0;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.snapshot') {
          call += 1;
          if (call === 1) return snap({ seq: 5 });
          if (call === 2) return snap({ seq: 3 });
          return snap({ seq: 6 });
        }
        return { ok: true };
      },
    });
    assert.equal((await hub.read())?.seq, 5);
    assert.equal(await hub.read(), null, 'seq 回退必须拒收');
    assert.equal((await hub.read())?.seq, 6, '新序号必须接受');
  });

  await test('[198] 无会话证据的低代际一律拒收（不得当作重启）', async () => {
    let call = 0;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.snapshot') {
          call += 1;
          return call === 1 ? snap({ generation: 9, seq: 9 }) : snap({ generation: 1, seq: 1 });
        }
        return { ok: true };
      },
    });
    assert.equal((await hub.read())?.generation, 9);
    assert.equal(await hub.read(), null, '无会话字段时低代际必须拒收（兼容防护）');
  });

  await test('[198] 真实会话变化：接受、重置确认/接管并重发需求', async () => {
    let starts = 0;
    let takeovers = 0;
    let phase = 1;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.start') {
          starts += 1;
          return { ok: true };
        }
        if (cmd === 'monitor.takeover') {
          takeovers += 1;
          return { ok: true, cleaned: true };
        }
        if (cmd === 'monitor.snapshot') {
          return phase === 1
            ? snap({ session: 77, generation: 9, seq: 9, relayEpoch: 1 })
            : snap({ session: 88, generation: 1, seq: 1, relayEpoch: 1 });
        }
        return { ok: true };
      },
    });
    const release = hub.acquire('topbar', { top: true });
    await flush();
    assert.equal(starts, 1);
    await hub.read();
    await flush();
    assert.equal(hub.session(), 77);
    assert.equal(hub.applied().top, true, '首会话确认');
    phase = 2;
    const v = await hub.read();
    await flush();
    assert.notEqual(v, null, '真实换会话必须接受');
    assert.equal(hub.session(), 88);
    assert.equal(starts, 2, '换会话后必须重新协调仍有的需求（幂等 start）');
    assert.equal(takeovers, 2, '换会话后接管需重新声明');
    release();
  });

  await test('[197] takeover 失败有界重试（≤预算）且成功后停止', async () => {
    let takeovers = 0;
    let takeoverOk = false;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.takeover') {
          takeovers += 1;
          if (!takeoverOk) throw new Error('transient');
          return { ok: true, cleaned: true };
        }
        if (cmd === 'monitor.snapshot') return snap();
        return { ok: true };
      },
    });
    for (let i = 0; i < 8; i++) await hub.read();
    await flush();
    assert.equal(takeovers, 5, `有界：takeovers=${takeovers}`);
    assert.equal(hub.takeover(), 'pending');
    await hub.read();
    await flush();
    assert.equal(takeovers, 5, '预算耗尽后不得无限重试');
    // 新的需求意图恢复预算 → 下一次读取可再次尝试并确认
    takeoverOk = true;
    const rel = hub.acquire('recovery', { top: true });
    await hub.read();
    await flush();
    assert.equal(takeovers, 6);
    assert.equal(hub.takeover(), 'done');
    await hub.read();
    await flush();
    assert.equal(takeovers, 6, '成功后不得再尝试');
    rel();
  });

  await test('[197] takeover cleaned=false 不算确认（待重试）', async () => {
    let cleaned = false;
    let takeovers = 0;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.takeover') {
          takeovers += 1;
          return { ok: true, cleaned };
        }
        if (cmd === 'monitor.snapshot') return snap();
        return { ok: true };
      },
    });
    await hub.read();
    await flush();
    assert.equal(hub.takeover(), 'pending', '清理未完成不得算成功');
    assert.equal(takeovers, 1);
    cleaned = true;
    await hub.read();
    await flush();
    assert.equal(hub.takeover(), 'done');
    assert.equal(takeovers, 2);
  });

  await test('[197] 无读者时停止经定时器收尾（失败不谎报、预算有界）', async () => {
    let now = NOW;
    let stops = 0;
    let stopOk = false;
    const timers = fakeTimers();
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.stop') {
          stops += 1;
          if (!stopOk) throw new Error('transient');
          return { ok: true };
        }
        if (cmd === 'monitor.snapshot') return snap();
        return { ok: true };
      },
      now: () => now,
      timers,
    });
    const release = hub.acquire('autofloat', { fps: true });
    await flush();
    assert.equal(hub.applied().fps, true);
    release();
    await flush();
    assert.equal(hub.applied().fps, true, '停止失败不得谎报已停');
    assert.ok(timers.items.length >= 1, '失败后应安排有界收尾定时器');
    stopOk = true;
    now += 5000;
    timers.fireAll();
    await flush();
    await flush();
    assert.equal(hub.applied().fps, false, '无读者也必须经定时器收尾');
    assert.ok(stops >= 2);
  });

  await test('[197] 停止失效按字段自身时间戳（旧 fps 不因 top 新鲜而复活）', async () => {
    let now = NOW;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.snapshot') {
          return snap({
            collectedAt: now,
            top: JSON.stringify({ ts: now, tempC: 50 }),
            fps: JSON.stringify({ ts: NOW - 60000, fps: 60, fps1: 50, gpu: 10, packagePower: 5, game: 'G', pid: 1 }),
            heartbeatAt: NOW - 60000,
          });
        }
        return { ok: true };
      },
      now: () => now,
    });
    const release = hub.acquire('autofloat', { fps: true });
    const releaseTop = hub.acquire('display', { top: true }); // top 有活跃需求（本用例断言 top 保留）
    await flush();
    now = NOW + 1000;
    release();
    await flush();
    now = NOW + 20000;
    const v = await hub.read();
    assert.notEqual(v?.top, null, '有需求的 top 保留');
    assert.equal(v?.fps, null, '停止前的旧 fps 必须按其自身时间戳丢弃');
    assert.equal(v?.heartbeatAt, null);
    releaseTop();
  });

  await test('[198] 超时已执行：释放后必须补发幂等 stop（结果未知≠没执行）', async () => {
    let realTop = false;
    let stops = 0;
    let rejectStart: ((err: Error) => void) | null = null;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.start') {
          realTop = true;
          return new Promise((_resolve, reject) => {
            rejectStart = reject;
          });
        }
        if (cmd === 'monitor.stop') {
          stops += 1;
          realTop = false;
          return { ok: true };
        }
        return { available: false };
      },
    });
    const release = hub.acquire('top', { top: true });
    await flush();
    assert.equal(realTop, true, '模拟：native 已真实启动');
    assert.equal(hub.pending().top, true, 'start 结果未知进入 pending');
    release();
    rejectStart!(Error('reply timeout after native start succeeded'));
    await flush();
    await flush();
    assert.equal(stops >= 1, true, '释放后必须补发 stop（不能因未确认就不管）');
    assert.equal(realTop, false, '最终必须真正停止');
    assert.deepEqual(hub.applied(), { top: false, fps: false });
    assert.deepEqual(hub.pending(), { top: false, fps: false });
  });

  await test('[198] 负回执（ok:false）不得置为已确认', async () => {
    const { hub } = rawHub({ invoke: async () => ({ ok: false }) });
    hub.acquire('top', { top: true });
    await flush();
    assert.equal(hub.applied().top, false, 'ok:false 属拒绝');
    assert.equal(hub.pending().top, true, '拒绝后仍属结果未知（可能未执行）');
  });

  await test('[198] 迟到的 takeover 回执不得标记新会话为 done', async () => {
    let resolveTakeover: ((v: unknown) => void) | null = null;
    let phase = 1;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.takeover') {
          return new Promise((resolve) => {
            resolveTakeover = resolve;
          });
        }
        if (cmd === 'monitor.snapshot') {
          return phase === 1 ? snap({ session: 5, generation: 3, seq: 3 }) : snap({ session: 6, generation: 1, seq: 1 });
        }
        return { ok: true };
      },
    });
    await hub.read();
    await flush();
    assert.equal(hub.takeover(), 'pending', 'takeover 未回执前属 pending');
    phase = 2;
    await hub.read(); // 换会话：takeover 重置为 idle，并重新发起
    await flush();
    resolveTakeover!({ ok: true, cleaned: true }); // 旧会话的迟到成功回执
    await flush();
    await flush();
    assert.notEqual(hub.takeover(), 'done', '旧会话回执不得把新会话标成 done');
  });

  await test('[198] 读回校准：显式不活跃清确认；字段缺失不校准', async () => {
    let starts = 0;
    let phase = 1;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.start') {
          starts += 1;
          return { ok: true };
        }
        if (cmd === 'monitor.takeover') return { ok: true, cleaned: true };
        if (cmd === 'monitor.snapshot') {
          if (phase === 1) return snap({ session: 3, generation: 1, seq: 1, topActive: false });
          if (phase === 2) return { schema: 1, available: false, session: 3, generation: 1, seq: 1 };
          return snap({ session: 3, generation: 1, seq: 2, topActive: true });
        }
        return { ok: true };
      },
    });
    const release = hub.acquire('top', { top: true });
    await flush();
    assert.equal(starts, 1);
    await hub.read(); // 显式 topActive=false ⇒ 清确认（但需求仍在，pump 会重发）
    await flush();
    assert.ok(starts >= 2, '观察不活跃 ⇒ 需重新协调');
    phase = 2;
    await hub.read(); // 字段缺失（无 topActive）⇒ 不校准
    await flush();
    phase = 3;
    await hub.read(); // 显式活跃 ⇒ 确认
    await flush();
    assert.equal(hub.applied().top, true);
    release();
  });

  await test('[198] 联通夹具：冻结形状的原生 top 载荷不被当作新数据消费', async () => {
    // 夹具来源：native 链⑦ 用**产品唯一构造点** nativeMonitorTopPayloadText 生成"冻结路径"
    // 的实际载荷文本并落盘（Patches\Tests\monitor-chain-selftest-root\frozen-top-payload-198.json），
    // 批次内拷到 Batches\R198\evidence\ 作为固定证据（形状桥：文本来自原生实现，非手抄）。
    // native 侧冻结语义：hwFresh=false ⇒ ts=最近新鲜时间（不盖当前时间）、hwDown=true、
    // fps-status 载荷缺省、hwinfo-ok 删除（快照 hwinfoOkAt=0）；心跳独立保留。
    const fixturePath =
      process.env.YMCC_R198_FROZEN_FIXTURE ??
      path.resolve(process.cwd(), '../../Batches/R198/evidence/frozen-top-payload-198.json');
    const topText = readFileSync(fixturePath, 'utf8').trim();
    assert.ok(topText.length > 0, `原生夹具缺失/为空: ${fixturePath}`);
    const lastFresh = 1758350000000; // 与 native 链⑦同一条"最近确实新鲜"常数
    assert.ok(topText.includes(`"ts":${lastFresh}`), 'ts 必须沿用最近新鲜时间');
    assert.ok(topText.includes('"hwDown":true'), '冻结时必须显式标陈旧');

    const now = Date.now();
    const deps = makeDeps({
      snapshot: () => ({
        schema: 1,
        available: true,
        session: 77,
        relayEpoch: 2,
        generation: 12,
        seq: 4,
        collectedAt: now,
        heartbeatAt: now,
        hwinfoOkAt: 0, // 0 = 不健康（语义同 hwinfo-ok 缺失）
        topActive: true,
        fpsActive: true,
        hwDown: true,
        top: topText, // fps 载荷缺省：冻结期 native 不产出 fps-status
      }),
    });
    const hub = createMonitorDataHub(deps);
    const view = await hub.read();
    assert.ok(view, '快照仍可读（生命周期与心跳可验证）');
    assert.equal(view?.top?.ts, lastFresh, '展示数据保持旧时间（未盖当前时间）');
    assert.ok(now - (view?.top?.ts ?? 0) > 6000, '超过既有 <6s 新鲜度门槛 ⇒ 按无数据处理');
    assert.equal(view?.fps, null, '无 fps 载荷 ⇒ 控制数据路径（readStatus 真数据分支）无输入');
    // autofloat.hwinfoStatus(): view !== null && view.hwinfoOkAt !== null && now - hwinfoOkAt < 6000
    assert.equal(view?.hwinfoOkAt, null, 'hwinfoOkAt=0 ⇒ 健康门为 false（控制/恢复不得消费）');
    assert.equal(view?.hwDown, true);
    assert.equal(view?.heartbeatAt, now, '独立工作心跳仍在刷新（区分"守护存活但空闲"与"守护已死"）');
  });

  await test('[198] unavailable 快照携带活闩锁 ⇒ 被消费并重新协调（真实启停可核对）', async () => {
    // native 198：available=false 也带 topActive/fpsActive（读自活的模式闩锁，不是上次发布值）。
    // 语义：回执说"启动了"之后，读回观察到"其实没在采集"必须清确认并重新协调需求，不能只信回执；
    // 同时不得因为"不可用"就误发停止（不可用 ≠ 已停）。
    let stops = 0;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.start') return { ok: true };
        if (cmd === 'monitor.stop') {
          stops += 1;
          return { ok: true };
        }
        return {
          schema: 1,
          available: false,
          session: 42,
          generation: 5,
          seq: 7,
          relayEpoch: 0,
          topActive: false,
          fpsActive: false,
          hwDown: true,
        };
      },
      now: () => NOW,
    });
    const release = hub.acquire('top', { top: true });
    await flush();
    const startsAfterAcquire = hub.attempts().start;
    assert.equal(hub.applied().top, true, 'start 回执（ok:true）确认');
    const view = await hub.read();
    assert.equal(view, null, 'available=false ⇒ 无数据（不返回陈旧值）');
    await flush();
    assert.ok(
      hub.attempts().start > startsAfterAcquire,
      '读到"其实没在采集" ⇒ 清确认并重新协调（不靠回执猜）',
    );
    assert.equal(stops, 0, '不可用 ≠ 已停：不得误发停止');
    release();
    await flush();
  });

  await test('[199] 较早发出的读回复不得抹掉较晚启动的未确认状态（释放后必须发停止）', async () => {
    // 与裁决方反例同序：snapshot（native 未启动，回复延迟）→ acquire 发 start（native 已执行，
    // 回执延迟）→ release → 旧 snapshot 返回 inactive → start 回执超时。
    // 不变量：旧读回不得清掉较新 start 的 pending ⇒ 释放时必须发出停止（幂等），native 最终停。
    let resolveRead: ((v: unknown) => void) | null = null;
    let rejectStart: ((e: Error) => void) | null = null;
    let realTop = false;
    let stops = 0;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.snapshot') return new Promise((resolve) => { resolveRead = resolve as (v: unknown) => void; });
        if (cmd === 'monitor.start') {
          realTop = true;
          return new Promise((_, reject) => { rejectStart = reject as (e: Error) => void; });
        }
        if (cmd === 'monitor.stop') {
          stops += 1;
          realTop = false;
          return { ok: true };
        }
        return { ok: true, cleaned: true };
      },
    });
    const readPromise = hub.read(); // 该读回复将晚到，且内容是"当时未启动"
    await flush();
    const release = hub.acquire('top', { top: true });
    await flush(); // native 已执行 start，回执延迟
    release();
    resolveRead!({ schema: 1, available: false, session: 11, relayEpoch: 0, generation: 1, seq: 0, topActive: false, fpsActive: false });
    await readPromise;
    await flush();
    rejectStart!(new Error('start executed; reply timed out'));
    await flush();
    await flush();
    assert.ok(stops >= 1, `释放后必须发出停止（native 可能已启动）：stops=${stops}`);
    assert.equal(hub.desired().top, false, '需求已关闭');
    assert.equal(realTop, false, '模拟 native 最终为已停');
  });

  await test('[199] 旧 active 读回不得在停止收敛后重新触发命令', async () => {
    let resolveRead: ((v: unknown) => void) | null = null;
    const { hub } = rawHub({
      invoke: async (cmd: string) => {
        if (cmd === 'monitor.snapshot') return new Promise((resolve) => { resolveRead = resolve as (v: unknown) => void; });
        if (cmd === 'monitor.start') return { ok: true };
        if (cmd === 'monitor.stop') return { ok: true };
        return { ok: true, cleaned: true };
      },
    });
    const release = hub.acquire('top', { top: true });
    await flush();
    const readPromise = hub.read(); // 该读回复内容为"发出时 active=true"
    await flush();
    const before = hub.attempts();
    release(); // 停止收敛
    await flush();
    resolveRead!({
      schema: 1, available: true, session: 21, relayEpoch: 0, generation: 2, seq: 1,
      collectedAt: NOW, heartbeatAt: NOW, hwinfoOkAt: NOW, hwDown: false,
      topActive: true, fpsActive: false, top: JSON.stringify({ ts: NOW, tempC: 50 }),
    });
    await readPromise;
    await flush();
    const after = hub.attempts();
    assert.equal(after.start, before.start, '旧 active 读回不得再发 start');
    assert.equal(after.stop, before.stop + 1, '停止只发一次：旧读回不得再触发停止');
  });

  await test('[199] 坏状态的原生 top 载荷（读取失败/pollTime=0/旧映射）不被当作新数据消费', async () => {
    // 夹具由 native 链⑦ 用**同一实际发布函数**（monitorDisplayTimestampMs + 唯一构造点）生成。
    const fixturePath =
      process.env.YMCC_R199_BAD_STATES_FIXTURE ??
      path.resolve(process.cwd(), '../../Batches/R199/evidence/bad-states-top-payloads-199.json');
    const items = JSON.parse(readFileSync(fixturePath, 'utf8')) as Array<{
      ts: number;
      text: string;
    }>;
    assert.equal(items.length, 3, `夹具应含三种坏状态: ${fixturePath}`);
    const now = Date.now();
    for (const item of items) {
      const deps = makeDeps({
        snapshot: () => ({
          schema: 1, available: true, session: 91, relayEpoch: 0, generation: 3, seq: 2,
          collectedAt: now, heartbeatAt: now, hwinfoOkAt: 0,
          topActive: true, fpsActive: false, hwDown: true, top: item.text,
        }),
      });
      const hub = createMonitorDataHub(deps);
      const view = await hub.read();
      assert.ok(view, '快照仍可读（生命周期可验证）');
      assert.equal(view?.top?.ts, item.ts, 'ts 必须是最近有效时间，不得被盖上当前时间');
      assert.ok(now - item.ts > 6000, '超过既有 <6s 新鲜度门槛 ⇒ 展示/控制按无数据处理');
      assert.equal(view?.hwDown, true, '必须显式标陈旧');
      assert.equal(view?.hwinfoOkAt, null, 'hwinfoOkAt=0 ⇒ 健康门为 false');
    }
  });

  console.log('monitor-data selftest: ALL PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});