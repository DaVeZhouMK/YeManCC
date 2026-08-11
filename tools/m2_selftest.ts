// m2_selftest.ts — M2 端到端自测（硬规则：先脚本化自测，禁止编译→让用户测→再修）
// 通过 mock-shell 把 yeman.ts 接到真实 Node 实现：程序 JSON 配置真实磁盘往返 + schtasks /Query 真跑。
//
// 运行：pnpm run test:m2  （见 package.json script：esbuild 打包后用 node 跑）
import './mock-shell';
import * as yeman from '@/bridge/yeman';
import {
  getTdpTarget,
  TDP_FLOAT_EXECUTION_LABELS,
  TDP_FLOAT_STRATEGY_ORDER,
} from '@/bridge/autofloat';
import { runPowerResumeTransaction } from '@/bridge/powerResume';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';

const TMP = path.join(os.tmpdir(), 'yeman-m2-test');
fs.rmSync(TMP, { recursive: true, force: true });
fs.mkdirSync(TMP, { recursive: true });
yeman.setPowerControlDir(TMP);

let pass = 0;
let fail = 0;
const fails: string[] = [];

async function check(name: string, fn: () => Promise<void> | void) {
  try {
    await fn();
    pass++;
    console.log('  ✓ ' + name);
  } catch (e: any) {
    fail++;
    fails.push(name + ' -> ' + (e?.message ?? e));
    console.log('  ✗ ' + name + ' :: ' + (e?.message ?? e));
  }
}
function assert(cond: any, msg: string) {
  if (!cond) throw new Error(msg);
}

async function main() {
  console.log('M2 自测 (yeman.ts via mock shell):');

  // 0: 旧 txt 一次性迁移到程序 JSON 配置（只读，不写回 txt）
  await check('config: 无 control-config.json 时从 tdp.txt/FPS-ac.txt 迁移', async () => {
    fs.rmSync(path.join(TMP, 'control-config.json'), { force: true });
    fs.writeFileSync(path.join(TMP, 'tdp.txt'), '200');
    fs.writeFileSync(path.join(TMP, 'FPS-ac.txt'), '90');
    assert((await yeman.readTdp('ac')) === 200, '迁移后 readTdp !== 200');
    assert((await yeman.readFps('ac')) === 90, '迁移后 readFps !== 90');
    // 迁移完成后清理旧 txt（模拟用户已迁移；后续用例断言不再产生新 txt）
    fs.rmSync(path.join(TMP, 'tdp.txt'), { force: true });
    fs.rmSync(path.join(TMP, 'FPS-ac.txt'), { force: true });
  });

  // 1-4: 程序 JSON 配置真实磁盘往返
  await check('config: saveTdp(ac,200) 写+读一致', async () => {
    await yeman.saveTdp('ac', 200);
    assert((await yeman.readTdp('ac')) === 200, 'readTdp(ac) !== 200');
  });
  await check('config: saveTdp(dc,35) 与 AC 共用 TDP 最大值', async () => {
    await yeman.saveTdp('dc', 35);
    assert((await yeman.readTdp('ac')) === 35, 'readTdp(ac) !== 35');
    assert(fs.existsSync(path.join(TMP, 'yeman-settings.json')), '缺少 yeman-settings.json');
    assert((JSON.parse(fs.readFileSync(path.join(TMP, 'yeman-settings.json'), 'utf8')).tdp.tdpMax) === 35, '统一配置 tdp.tdpMax 错误');
    assert(!fs.existsSync(path.join(TMP, 'control-config.json')), '不应生成 control-config.json');
    assert(!fs.existsSync(path.join(TMP, 'tdp.txt')), '不应写 tdp.txt');
  });
  await check('config: saveFps(ac,90) 写+读一致', async () => {
    await yeman.saveFps('ac', 90);
    assert((await yeman.readFps('ac')) === 90, 'readFps(ac) !== 90');
  });
  await check('config: saveFps(dc,60) 与 AC 共用 FPS 上限', async () => {
    await yeman.saveFps('dc', 60);
    assert((await yeman.readFps('ac')) === 60, 'readFps(ac) !== 60');
    assert(JSON.parse(fs.readFileSync(path.join(TMP, 'yeman-settings.json'), 'utf8')).tdp.fpsLimit === 60, '统一配置 tdp.fpsLimit 错误');
    assert(!fs.existsSync(path.join(TMP, 'FPS-ac.txt')), '不应写 FPS-ac.txt');
    assert(!fs.existsSync(path.join(TMP, 'FPS-dc.txt')), '不应写 FPS-dc.txt');
  });

  // 5-6: 厂商识别
  await check('vendor: AMD.txt -> amd', async () => {
    fs.writeFileSync(path.join(TMP, 'AMD.txt'), '');
    assert((await yeman.detectVendor()) === 'amd', 'detectVendor != amd');
  });
  await check('vendor: intel.txt -> intel', async () => {
    fs.rmSync(path.join(TMP, 'AMD.txt'), { force: true });
    fs.writeFileSync(path.join(TMP, 'intel.txt'), '');
    assert((await yeman.detectVendor()) === 'intel', 'detectVendor != intel');
  });

  // 7: pawnio 自检不抛
  await check('pawnio: checkPawnio 返回对象', async () => {
    const s = await yeman.checkPawnio();
    assert(typeof s.exePresent === 'boolean' && typeof s.driverPresent === 'boolean', 'shape 错误');
  });

  // 8-9: schtasks 真实执行（/Query、/Delete）
  await check('task: taskExists 真实 schtasks /Query 返回 boolean', async () => {
    const r = await yeman.taskExists('监控-开机启动监控锁帧软件RTSS');
    assert(typeof r === 'boolean', 'taskExists 非 boolean');
  });
  await check('task: deleteTask 真实 schtasks /Delete 返回 boolean', async () => {
    const r = await yeman.deleteTask('__YeManCC_NonExistent__');
    assert(typeof r === 'boolean', 'deleteTask 非 boolean');
  });

  // 10: toggleTask 关闭路径（无 XML）不抛
  await check('task: toggleTask(name,false) 安全返回 false', async () => {
    const r = await yeman.toggleTask('__YeManCC_NonExistent__', false);
    assert(r === false, 'toggle off 应返回 false');
  });

  // 11-12: powercfg 命令组合真实执行（bogus GUID 仅校验命令可下发）
  await check('powercfg: setAcValueIndex 返回 RunResult', async () => {
    const r = await yeman.setAcValueIndex('sub', 'set', 'val', '00000000-0000-0000-0000-000000000000');
    assert(typeof r.exitCode === 'number' && 'stdout' in r, 'RunResult 形状错误');
  });
  await check('powercfg: setHibernate 使用完整休眠且不改测试机状态', async () => {
    const calls = (globalThis as any).__mockShellCalls as Array<{ program: string; args: string[] }>;
    calls.length = 0;
    (globalThis as any).__mockShellDryRun = true;
    try {
      const r = await yeman.setHibernate(true);
      assert(typeof r.exitCode === 'number', 'setHibernate 形状错误');
      assert(calls.length === 2, `开启休眠应执行 2 条命令，实际 ${calls.length}`);
      assert(calls[0].args.join(' ') === '/hibernate on', '未先开启 Windows 休眠');
      assert(calls[1].args.join(' ') === '/hibernate /type full', '系统休眠必须使用 full 类型');
      assert(calls.every((call: any) => call.timeoutMs === 10000), '休眠命令必须使用 10 秒有界超时');
    } finally {
      (globalThis as any).__mockShellDryRun = false;
    }
  });

  // CPU 写入规则：所有写入类参数直接提交，不做 /query 或重复值跳过。
  await check('CPU浮动: 三类联动强制直写且单条失败不阻塞', async () => {
    const calls = (globalThis as any).__mockShellCalls as Array<{ program: string; args: string[]; timeoutMs?: number }>;
    const writes = (globalThis as any).__mockRegistryWrites as Array<{ root: string; path: string; name: string; value: any }>;
    (globalThis as any).__mockShellDryRun = true;
    calls.length = 0;
    writes.length = 0;
    const base = {
      acFreq: 3000, dcFreq: 3000, acTurbo: true, dcTurbo: true,
      acAggr: 80, dcAggr: 80, acMinState: 20, dcMinState: 20,
      acThrottle: 2 as const, dcThrottle: 2 as const,
      sides: ['ac', 'dc'] as Array<'ac' | 'dc'>, restoreMaxState: false,
    };
    assert(await yeman.applyPowerParams(base) === true, '首次写入应报告 changed');
    assert(writes.length === 22, '首次应完整提交 AC/DC 共 22 条注册表值');
    assert(calls.length === 0, 'CPU 注册表路径不应产生 powercfg 写入');
    assert(writes.every((w) => w.root === 'HKLM' && w.name.match(/^(AC|DC)SettingIndex$/)), '必须写入 HKLM 的 AC/DCSettingIndex');
    const freqGuids = [
      '75b0ae3f-bce0-45a7-8c89-c9611c25e100',
      '75b0ae3f-bce0-45a7-8c89-c9611c25e101',
      '75b0ae3f-bce0-45a7-8c89-c9611c25e102',
    ];
    for (const guid of freqGuids) {
      assert(writes.some((w) => w.path.endsWith(`\\54533251-82be-4824-96c1-47b60b740d00\\${guid}`)), `缺少频率注册表键 ${guid}`);
    }
    calls.length = 0;
    writes.length = 0;
    assert(await yeman.applyPowerParams({ ...base, previousApplied: base }) === true, '重复值也应强制写入');
    assert(writes.length === 22, 'previousApplied 不得跳过写入');
    calls.length = 0;
    writes.length = 0;
    (globalThis as any).__mockRegistryThrowOnce = 'SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\1cb8b882-a900-4b9f-9bac-99d151e64441\\54533251-82be-4824-96c1-47b60b740d00\\36687f9e-e3a5-4dbf-b1dc-15eb381c6863|ACSettingIndex';
    assert(await yeman.applyPowerParams({ ...base, acAggr: 70, dcAggr: 70, previousApplied: base }) === true, '单条失败不应阻塞整批');
    assert(writes.length === 22, '单条失败后仍需继续完整提交 22 条');
    assert(calls.length === 0, '单条失败后不应产生 powercfg 查询或写入');
    (globalThis as any).__mockShellDryRun = false;
  });

  // 13-14: createTask XML 映射校验（缺模板/缺文件应抛错）
  await check('task: createTask 旧 TDP 任务已移除', async () => {
    let threw = false;
    try {
      await yeman.createTask('TDP-开机启动野蛮快设TDP挡位');
    } catch {
      threw = true;
    }
    assert(threw, '旧 TDP 任务应不可创建');
  });
  await check('task: createTask 旧 AC TDP 任务已移除', async () => {
    let threw = false;
    try {
      await yeman.createTask('TDP-插电AC模式TDP调节');
    } catch {
      threw = true;
    }
    assert(threw, '旧 AC TDP 任务应不可创建');
  });

  // 额外：任务清单关键任务完整性
  await check('task: TASKS 包含当前启动/监控任务', () => {
    assert(yeman.TASKS.some((t: any) => t.name === '野蛮控制中心-开机启动'), '缺少「野蛮控制中心-开机启动」任务');
    assert(yeman.TASKS.some((t: any) => t.name === '监控-开机启动监控锁帧软件RTSS'), '缺少 RTSS 监控任务');
    assert(!yeman.TASKS.some((t: any) => /TDP-|锁帧-.*AC|锁帧-.*DC/.test(t.name)), 'TASKS 仍有旧 AC/DC TDP/锁帧任务');
  });

  await check('performance UI: 75W 主体与执行策略小字固定映射', () => {
    const watts = TDP_FLOAT_STRATEGY_ORDER.map((strategy) => getTdpTarget(75, strategy));
    assert(JSON.stringify(watts) === JSON.stringify([75, 60, 52, 45, 37]), `75W 映射错误: ${watts.join('/')}`);
    const labels = TDP_FLOAT_STRATEGY_ORDER.map((strategy) => TDP_FLOAT_EXECUTION_LABELS[strategy]);
    assert(
      JSON.stringify(labels) === JSON.stringify([
        '无下降执行', '小幅浮动执行', '中幅浮动执行', '大幅浮动执行', '激进浮动执行',
      ]),
      `执行策略顺序错误: ${labels.join('/')}`,
    );
  });

  await runResumeTests();
  console.log(`\n结果: ${pass} 通过 / ${fail} 失败`);
  if (fail > 0) {
    console.log('失败项:');
    fails.forEach((f) => console.log('  - ' + f));
    process.exit(1);
  }
  async function runResumeTests() {
  // 睡眠唤醒恢复事务：native gate 先提交，daemon 句柄随后做有界恢复。
  await check('resume: daemonRequired=true 失败仍提交 gate', async () => {
    let completeCalls = 0;
    let seenMeta: any = null;
    const result = await runPowerResumeTransaction(2, true, {
      resumeDaemon: async () => false,
      completeResume: async (_generation, meta) => {
        completeCalls++;
        seenMeta = meta;
        return { ok: true };
      },
      sleep: async () => {},
      daemonAttempts: 2,
      commitAttempts: 3,
    });
    assert(result.committed === true, 'daemon 失败不应阻断提交');
    assert(result.daemonReady === false, 'daemonReady 应为 false');
    assert(completeCalls === 1, `completeResume 调用次数=${completeCalls}`);
    assert(seenMeta?.daemonRequired === true && seenMeta?.daemonReady === false, '提交元数据错误');
  });

  await check('resume: completeResume 前两次失败、第三次成功', async () => {
    let completeCalls = 0;
    const result = await runPowerResumeTransaction(3, false, {
      resumeDaemon: async () => true,
      completeResume: async () => {
        completeCalls++;
        return completeCalls >= 3 ? { ok: true } : { ok: false, reason: 'native_recovery_not_ready' };
      },
      sleep: async () => {},
      commitAttempts: 3,
    });
    assert(result.committed === true && result.commitAttempts === 3, '第三次提交应成功');
    assert(completeCalls === 3, `completeResume 调用次数=${completeCalls}`);
  });

  await check('resume: 新 generation 到来时旧事务不提交', async () => {
    let current = false;
    let completeCalls = 0;
    let daemonCalls = 0;
    const result = await runPowerResumeTransaction(4, true, {
      resumeDaemon: async () => {
        daemonCalls++;
        return true;
      },
      completeResume: async () => {
        completeCalls++;
        return { ok: true };
      },
      isGenerationCurrent: () => current,
      sleep: async () => {},
    });
    assert(result.committed === false && result.reason === 'superseded', '旧代际应被丢弃');
    assert(completeCalls === 0, '旧代际不应调用 completeResume');
    assert(daemonCalls === 0, '旧代际不应恢复 daemon');
  });

  await check('resume: 非必要 daemon 不改变手动模式常驻策略', async () => {
    let requiredSeen: boolean | null = null;
    const result = await runPowerResumeTransaction(5, false, {
      resumeDaemon: async (required) => {
        requiredSeen = required;
        return true;
      },
      completeResume: async () => ({ ok: true }),
      sleep: async () => {},
    });
    assert(result.committed === true && requiredSeen === false, '手动模式 daemonRequired 应为 false');
  });

  await check('resume: daemon 第二次恢复成功后正常提交', async () => {
    let daemonCalls = 0;
    const result = await runPowerResumeTransaction(6, true, {
      resumeDaemon: async () => ++daemonCalls >= 2,
      completeResume: async () => ({ ok: true }),
      sleep: async () => {},
      daemonAttempts: 2,
    });
    assert(result.committed === true && result.daemonReady === true, '第二次 daemon 恢复应提交');
    assert(daemonCalls === 2, `daemon 尝试次数=${daemonCalls}`);
  });
  }

  fs.rmSync(TMP, { recursive: true, force: true });
  console.log('M2 自测全部通过 ✓');
}

main();
