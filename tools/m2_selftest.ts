// m2_selftest.ts — M2 端到端自测（硬规则：先脚本化自测，禁止编译→让用户测→再修）
// 通过 mock-shell 把 yeman.ts 接到真实 Node 实现：txt 真实磁盘往返 + schtasks /Query 真跑。
//
// 运行：npm run test:m2  （见 package.json script：esbuild 打包后用 node 跑）
import './mock-shell';
import * as yeman from '@/bridge/yeman';
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

  // 1-4: txt 真实磁盘往返
  await check('txt: saveTdp(ac,300) 写+读一致', async () => {
    await yeman.saveTdp('ac', 300);
    assert((await yeman.readTdp('ac')) === 300, 'readTdp(ac) !== 300');
    assert(fs.readFileSync(path.join(TMP, 'tdp-ac.txt'), 'utf8').trim() === '300', '文件内容!=300');
  });
  await check('txt: saveTdp(dc,35) 写+读一致', async () => {
    await yeman.saveTdp('dc', 35);
    assert((await yeman.readTdp('dc')) === 35, 'readTdp(dc) !== 35');
  });
  await check('txt: saveFps(ac,90) 写+读一致', async () => {
    await yeman.saveFps('ac', 90);
    assert((await yeman.readFps('ac')) === 90, 'readFps(ac) !== 90');
  });
  await check('txt: saveFps(dc,60) 写+读一致', async () => {
    await yeman.saveFps('dc', 60);
    assert((await yeman.readFps('dc')) === 60, 'readFps(dc) !== 60');
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
    const r = await yeman.taskExists('TDP-插电AC模式TDP调节');
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
  await check('powercfg: setHibernate 返回 RunResult', async () => {
    const r = await yeman.setHibernate(false);
    assert(typeof r.exitCode === 'number', 'setHibernate 形状错误');
  });

  // 13-14: createTask XML 映射校验（缺模板/缺文件应抛错）
  await check('task: createTask 无 XML 模板抛错', async () => {
    let threw = false;
    try {
      await yeman.createTask('TDP-开机启动野蛮快设TDP挡位');
    } catch {
      threw = true;
    }
    assert(threw, '无 XML 模板应抛错');
  });
  await check('task: createTask 有模板但文件缺失抛错', async () => {
    fs.writeFileSync(path.join(TMP, 'tdp-ac.txt'), '300'); // 确保 PC 目录存在
    let threw = false;
    try {
      await yeman.createTask('TDP-插电AC模式TDP调节'); // 模板 TDP-AC.xml 不存在
    } catch {
      threw = true;
    }
    assert(threw, 'XML 文件缺失应抛错');
  });

  // 额外：任务清单完整性（含「野蛮控制中心-开机启动」共 13 项）
  await check('task: TASKS 共 13 项且名字齐全', () => {
    assert(yeman.TASKS.length === 13, 'TASKS 数量 != 13');
    assert(
      yeman.TASKS.some((t: any) => t.name === '野蛮控制中心-开机启动'),
      '缺少「野蛮控制中心-开机启动」任务',
    );
  });

  console.log(`\n结果: ${pass} 通过 / ${fail} 失败`);
  if (fail > 0) {
    console.log('失败项:');
    fails.forEach((f) => console.log('  - ' + f));
    process.exit(1);
  }
  fs.rmSync(TMP, { recursive: true, force: true });
  console.log('M2 自测全部通过 ✓');
}

main();
