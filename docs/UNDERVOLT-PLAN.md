# YeManCC 通用降压（Undervolt）实现计划

> 结论先行：**可行，且成本极低。**
> 你的 `YeManTdpCtl.py` 已经具备 UXTU 降压所需的全部底层通道：
> - AMD：PawnIO + `RyzenSMU.bin` + MP1 邮箱直写（`_send_mp1` 现成）
> - Intel：`KX.exe /wrmsr`（TDP 已在用 `/wrmsr 0x610`，降压只是换成 `0x150`）
>
> **不需要任何新驱动、新依赖。** 本质上是给 YeManTdpCtl 加一个 `uv` 命令族 + 前端一个降压面板。

---

## 一、UXTU 降压机制解析（源码结论）

### 1.1 Intel 降压 = MSR 0x150（FIVR Voltage Offset）

UXTU `Intel_Management.cs → changeVoltageOffset(value, voltagePlane)`：

```csharp
ulong command = voltagePlane switch {
    0 => 0x80000011UL,   // Plane 0: CPU Core
    1 => 0x80000111UL,   // Plane 1: iGPU
    2 => 0x80000211UL,   // Plane 2: CPU Cache（必须与 Core 同值！）
    3 => 0x80000411UL,   // Plane 4: Analog I/O
};
// 偏移量编码：mV → round(mv * 1.024) << 21（32 位补码）
ulong data = Convert.ToUInt64(convertVoltageToHexMSR(value), 16);
ulong msrValue = (command << 32) | data;
WriteMsr(0x150, msrValue);
```

编码公式（`convertVoltageToHexMSR`）：
```
offset_raw = round(mV × 1.024)        # 例：-50mV → -51
data32     = (offset_raw << 21) & 0xFFFFFFFF   # -51<<21 → 0xF9A00000
MSR 0x150  = (0x80000011 << 32) | data32       # Core plane 写入
```

这就是 ThrottleStop / XTU 同款的 OC Mailbox 协议：
- 高 32 位 `0x80000?11`：bit63=busy，`?`=plane 序号，低字节 `0x11`=写电压命令
- 读回：低字节改 `0x10`（读命令）写入后，再读 MSR 0x150 得当前 offset

### 1.2 AMD 降压 = Curve Optimizer（SMU 邮箱命令）

UXTU `RyzenSmu.cs` 按 CPU 家族定义 CO 命令号，走与 TDP 完全相同的 MP1/RSMU 邮箱。
`applySettings("set-coall", value)` → `SendMp1(cmd, args)` → 邮箱写入。

**CO 命令号总表**（`true`=MP1 邮箱 / `false`=RSMU 邮箱）：

| 家族 (UXTU Socket) | 对应 CPU | set-coall | set-coper | set-cogfx |
|---|---|---|---|---|
| FT6_FP7_FP8 | **Strix Halo / Strix / Krackan / Phoenix / Hawk / Rembrandt / Mendocino** | MP1 `0x4C` / RSMU `0x5D` | MP1 `0x4B` / RSMU `0x53` | RSMU `0xB7` |
| AM5_V1 | Raphael / Granite Ridge（7950X/9950X 桌面） | MP1 `0x36` / RSMU `0x07` | MP1 `0x35` / RSMU `0x06` | RSMU `0xA7` |
| AM4_V2 | Vermeer / Cezanne 桌面 | MP1 `0x36` / RSMU `0x0B` | MP1 `0x35` / RSMU `0x0A` | — |
| FP6_AM4 | Renoir / Cezanne 移动 | MP1 `0x55` / RSMU `0xB1` | MP1 `0x54` / RSMU `0x52` | RSMU `0x53` |
| FT5_FP5_AM4 | Raven / Picasso 老 APU | RSMU `0x59` | RSMU `0x58` | RSMU `0x59` |

**MP1 邮箱地址**（YeManTdpCtl 现有的两组已覆盖主力机型）：

| 家族 | MSG | RSP | ARG |
|---|---|---|---|
| Strix Halo / Strix / Krackan（你现在的 `FAM_APU`） | `0x3B10928` | `0x3B10978` | `0x3B10998` |
| Phoenix / Hawk / Rembrandt / Mendocino | `0x3B10528` | `0x3B10578` | `0x3B10998` |
| 桌面 AM5 / AM4_V2（你现在的 `FAM_DESKTOP_AM5`） | `0x3B10530` | `0x3B1057C` | `0x3B109C4` |

**CO 值编码**（ryzenadj / UXTU 同源）：
- `set-coall`：全核偏移，负值用 **32 位补码**。例 `-20` → `0xFFFFFFEC`
- `set-coper`：`value = (core_id << 20) | (offset & 0xFFFF)`，offset 为 16 位补码
- 范围：一般 `-60 ~ 0`（掌机 APU 建议上限 `-40`，步进 1 = 约 3~5mV 等效）

### 1.3 UXTU 与你的差异

| | UXTU | YeManCC 现状 |
|---|---|---|
| AMD 通道 | PawnIO `RyzenSMU.bin`，用户态轮询邮箱 | **同款**（`_send_mp1` 一致） |
| Intel MSR | PawnIO `IntelMSR.bin` → `ioctl_write_msr` | KX.exe `/wrmsr`（**等价**，且你已有 `IntelMSR.bin` 可做备选） |
| 家族识别 | CPUID family/model 全表 | 型号名正则（够用，需微调） |

---

## 二、总体设计

```
Vue 前端 (TdpView.vue 新增降压区块 / 或新建 UndervoltView.vue)
   │  shell.run('YeManTdpCtl.exe', ['uv', 'set', '-20'])
   ▼
native main.cpp (无需改动 —— 复用现有 shell.run IPC)
   ▼
YeManTdpCtl.py  新增 uv 命令族
   ├─ AMD  : _send_mp1(cfg["coall"], to_u32(offset), cfg)   ← 复用现有邮箱函数
   └─ Intel: kx_run(["/wrmsr","0x150","0x80000011","0x<data>"]) ← 复用现有 KX 封装
       └─ 备选: PawnIO + IntelMSR.bin (ioctl_write_msr)，KX 被 HWiNFO 占用时兜底
```

持久化沿用你的「txt 真相源」模式：
```
C:\SOFT\YeMan\PowerControl\uv-offset.txt      # 当前降压值（AMD 负整数 / Intel 负 mV）
C:\SOFT\YeMan\PowerControl\uv-pending.flag    # 稳定性哨兵（见 4.3 防翻车机制）
```

---

## 三、Phase 1：YeManTdpCtl.py 扩展（核心，约 150 行）

### 3.1 家族表补充 CO 命令号

```python
FAM_DESKTOP_AM5 = {
    "msg": 0x3B10530, "rsp": 0x3B1057C, "arg": 0x3B109C4,
    "stapm": 0x4F, "fast": 0x3E, "slow": 0x5F,
    "coall": 0x36, "coper": 0x35,          # ← 新增 (UXTU AM5_V1)
}
FAM_APU = {           # Strix Halo / Strix / Krackan
    "msg": 0x3B10928, "rsp": 0x3B10978, "arg": 0x3B10998,
    "stapm": 0x14, "fast": 0x15, "slow": 0x16,
    "coall": 0x4C, "coper": 0x4B,          # ← 新增 (UXTU FT6_FP7_FP8)
}
FAM_APU_OLD = {       # Phoenix / Hawk / Rembrandt（RSP 地址不同！）
    "msg": 0x3B10528, "rsp": 0x3B10578, "arg": 0x3B10998,
    "stapm": 0x14, "fast": 0x15, "slow": 0x16,
    "coall": 0x4C, "coper": 0x4B,
}
```

> `detect_amd_family()` 需按型号名细分：`8840U/7840U/7735/6800U` 等 → `FAM_APU_OLD`；
> `AI 9/AI Max/AI 7/8945HX3D` 等 Strix 系 → `FAM_APU`。识别失败时先用现 RSP 探测，
> 邮箱读全 0/超时则切另一组地址（两组只差 RSP，探测成本一次读寄存器）。

### 3.2 AMD 降压实现

```python
def amd_uv_set(offset, dry=False):
    """全核 Curve Optimizer。offset: -60~0（0=还原）。"""
    if not (-60 <= offset <= 0):
        log("  CO 超范围(-60~0):", offset); return 2
    if dry:
        log("  [dry-run] set-coall MP1 arg=0x%08X" % (offset & 0xFFFFFFFF)); return 0
    p, rc = _amd_open()
    if rc != 0: return rc
    try:
        cfg = detect_amd_family()
        st = _send_mp1(p, cfg["coall"], offset & 0xFFFFFFFF, cfg)   # 补码编码
        log("  AMD set-coall(0x%02X) %d -> %s" % (cfg["coall"], offset,
            "OK" if st == 1 else "FAIL(status=%s)" % st))
        return 0 if st == 1 else 6
    finally:
        p.close()
```

### 3.3 Intel 降压实现

```python
MSR_VOLTAGE_CTL = 0x150
UV_PLANES = {"core": 0x80000011, "gpu": 0x80000111, "cache": 0x80000211}

def _uv_mv_to_data32(mv):
    """UXTU convertVoltageToHexMSR: round(mv*1.024)<<21, 32 位截断。"""
    return (int(round(mv * 1.024)) << 21) & 0xFFFFFFFF

def intel_uv_set(mv, dry=False):
    """mv: -150~0。Core 与 Cache 必须同值写入，随后写 iGPU（可选同值）。"""
    if not (-150 <= mv <= 0):
        log("  电压偏移超范围(-150~0mV):", mv); return 2
    data = _uv_mv_to_data32(mv)
    if dry:
        log("  [dry-run] wrmsr 0x150 core/cache data=0x%08X" % data); return 0
    for name in ("core", "cache"):          # Core+Cache 成对，缺一 CPU 只按较小值生效
        hi = "0x%08X" % UV_PLANES[name]
        r = kx_run(["/wrmsr", "0x150", hi, "0x%08X" % data])
        if not kx_ok(r):
            log("  Intel UV %s 写入失败" % name); return 6
        log("  Intel UV %s %dmV -> OK" % (name, mv))
        time.sleep(0.05)
    return 0

def intel_uv_get():
    """读回验证：写读命令(0x8000?010) 后 rdmsr 0x150。
    读回 0 而写入非 0 → 主板 OC Lock/CFG Lock 已锁降压（10 代后笔记本常见），
    前端应显示『本机已锁定降压(需 BIOS 解锁)』而不是报错。"""
    kx_run(["/wrmsr", "0x150", "0x80000010", "0x0"])
    r = kx_run(["/rdmsr", "0x150"])
    # 解析 Msr Data，data>>21 还原 mV（11 位补码），打印当前 core offset
    ...
```

### 3.4 CLI 命令面

```
YeManTdpCtl uv set -20            # 自动识别厂商：AMD→CO -20 / Intel→-20mV(core+cache)
YeManTdpCtl uv set 0              # 还原（AMD CO=0 / Intel offset=0）
YeManTdpCtl uv get                # 读当前值 + 锁定检测
YeManTdpCtl uv set -20 --dry-run  # 只算不写
```

复用现有：管理员自提权（`NEEDS_DRIVER` 加 `"uv"`）、`ensure_pawnio()`（仅 AMD）、
门忙重试、`--vendor` 覆盖、日志。**main() 里加一个 elif 分支即可。**

---

## 四、Phase 2：前端 UI（Vue）

### 4.1 集成点：`TdpView.vue` 新增「降压」卡片（不建议独立页，掌机屏幕寸土寸金）

```
┌─ CPU 降压 (Undervolt) ──────────────────────┐
│  状态: 已应用 -20        [本机已锁定降压]（Intel 锁定时）│
│  滑条  0 ────────●──────── -40   (AMD: CO 档 / Intel: mV) │
│  [应用] [还原]   □ 开机自动应用（通过稳定性验证后可勾选）  │
└─────────────────────────────────────────┘
```

### 4.2 `src/bridge/yeman.ts` 新增

```ts
export const UV_MIN = -40, UV_MAX = 0;   // AMD CO 档；Intel 前端×1 当 mV 用，上限 -100
export async function setUndervolt(v: number): Promise<boolean> {
  const r = await shell.run(TDPCTL_EXE, ['uv', 'set', String(v)], 20000);
  if (r.code === 0) await fs.writeTextFile(join(PC_DIR, 'uv-offset.txt'), String(v));
  return r.code === 0;
}
export async function readUndervolt(): Promise<number> { /* 读 uv-offset.txt */ }
```

### 4.3 防翻车机制（必须做，降压和 TDP 不同——过头会死机/黑屏）

1. **两段式应用**：点击[应用] → 写 `uv-pending.flag`（含目标值+时间戳）→ 下发硬件 →
   前端 30 秒倒计时弹条「系统稳定吗？[确认保留]」：
   - 用户点确认 → 删 flag，值写入 `uv-offset.txt`
   - 30 秒无响应/死机重启 → 下次启动检测到 flag → **不重放该值**，弹「上次降压未确认，已还原」
2. **开机自动应用**默认关闭，只有某个值通过过一次「确认保留」后才允许勾选；
   实现：复用你现有计划任务体系（`TDP-开机启动` 同款 XML + bat，调 `uv set <val>`）
3. **限幅**：CLI 硬限 AMD `-60~0` / Intel `-150~0`；前端默认软限 `-40 / -100`
4. **睡眠唤醒重放**：Intel MSR 0x150 掉电不保持，S3/Modern Standby 唤醒后失效。
   在 native `PBT_APMRESUME` 处（你已有 SleepGuard 回调）追加一次 `uv set`（读 uv-offset.txt）。
   AMD CO 部分机型休眠后也会复位，统一重放即可，多写无害。

---

## 五、Phase 3：完善（可选迭代）

- **AMD 单核 CO**（`set-coper`，`(core<<20)|(off&0xFFFF)`）——进阶玩家用，优先级低
- **Intel iGPU 降压**（plane 1）——掌机 iGPU 降压收益可观，UI 加第二根滑条
- **PawnIO 直写 Intel MSR 备选路径**：HWiNFO 独占 KX 驱动文件时（你已在 `kx_ok` 里
  处理过 ErrorCode 32），改走 PawnIO + 现成的 `IntelMSR.bin`（`ioctl_write_msr`，
  UXTU IntelPawnIO.cs 同款），彻底消灭共存问题
- **稳定性压测按钮**：调用系统 `prime95`/内置简单 AVX 循环 2 分钟辅助验证

---

## 六、风险清单

| 风险 | 影响 | 对策 |
|---|---|---|
| Intel 笔记本 OC Lock / CFG Lock（10 代+普遍） | 写 0x150 无效或蓝屏 | `uv get` 读回验证，锁定则 UI 置灰并提示 BIOS 解锁 |
| 降压过头死机 | 系统冻结/重启 | 4.3 两段式 pending flag，重启不重放 |
| AMD 家族识别错 → 邮箱地址错 | 命令超时（无害）或 no-op | RSP 探测回退 + status!=1 即报 FAIL，不静默 |
| HWiNFO 占用 KX 驱动 | Intel 写入失败 | 已有 kx_ok 检测；Phase 3 PawnIO 兜底 |
| 桌面 AM4 老平台 CO 不支持（Zen1/Zen+） | 命令被拒 | status!=1 → 前端提示「本机不支持」 |
| 杀软误报（写 MSR） | 拦截 | 与现有 TDP 同通道，无新增面 |

---

## 七、实施顺序与验收

| 步骤 | 内容 | 验收标准 |
|---|---|---|
| 1 | YeManTdpCtl.py 加 `uv` 命令 + 家族表 | `uv set -10 --dry-run` 输出正确编码 |
| 2 | 真机 AMD 验证（你的 Strix Halo） | `uv set -10` status=1；HWiNFO 观察同负载功耗/频率变化 |
| 3 | 真机 Intel 验证（Intel 掌机/本） | `uv get` 读回 -10mV；锁定机型正确报锁 |
| 4 | PyInstaller 重打包 YeManTdpCtl.exe | 双目录（PowerControl/pawnio + dist）同步更新 |
| 5 | TdpView 降压卡片 + yeman.ts | 滑条应用/还原/状态回显正常 |
| 6 | pending flag 防翻车 + 唤醒重放 | 拔电重启后不重放未确认值；睡眠唤醒后 HWiNFO 读到 offset 仍在 |
| 7 | 开机自动应用（计划任务） | 重启后 30 秒内值生效 |

**工作量评估**：Phase 1 一个下午；Phase 2 一天；Phase 3 按需。
最大的不确定点只有一个：**你手上 Intel 测试机是否锁了 0x150**——第 3 步先测这个，
锁了也不影响 AMD 侧交付。

---

## 八、四档预设按钮方案（关闭 / 安全 / 平衡 / 风险）

### 8.1 推荐数值表

设计原则：**「安全档」要求 ≈99% 的芯片开箱即稳**（可默认推荐给小白）；
「平衡档」多数芯片稳定，但要求走过一次 4.3 的确认流程；
「风险档」明确告知可能死机，必须通过两段式确认 + 建议压测。

| 档位 | AMD（Curve Optimizer 计数） | Intel（Core+Cache 偏移 mV） | 预期收益 | 翻车概率 |
|---|---|---|---|---|
| 关闭 | `0` | `0 mV` | — | 无 |
| 安全 | `-8` | `-25 mV` | 同功耗性能 +2~3%，或同频功耗 -2~4% | 极低（<1%，可默认推荐） |
| 平衡 | `-14` | `-45 mV` | +3~6% / -4~8% | 很低（约 3~5% 芯片需回退） |
| 风险 | `-24` | `-75 mV` | +6~10% / -8~12% | 低中（15~20% 芯片不稳，仍需两段式确认） |

换算说明（按 **1 CO ≈ 3~5mV** 取中间值 ~3.2mV 等效）：
- `-8 × 3.2 ≈ -25mV`、`-14 × 3.2 ≈ -45mV`、`-24 × 3.2 ≈ -75mV`，两边档位收益对齐
- 这组保守数值的意义：安全/平衡档几乎不会翻车，用户敢长期开；风险档也从「彩票」降到「大概率能过」

补充规则：
- 掌机 APU（15~30W 低功耗区间）降压收益比桌面更明显，但低负载/空闲态反而是 CO 最容易翻车的场景（电压本来就低）——保守数值正好规避
- Intel 12 代+混合大小核机型，风险档前端再砍到 `-60mV`（E-core 电压裕度小）
- 数值做成 `config/uv-presets.json` 可配置，方便你后续按机型微调：

```json
{
  "amd":   { "off": 0, "safe": -8,  "balance": -14, "risk": -24 },
  "intel": { "off": 0, "safe": -25, "balance": -45, "risk": -75 }
}
```

CLI 对应：`YeManTdpCtl uv preset safe`（内部按厂商查表转成 `uv set`）。

### 8.2 「本机是否支持降压」检测（uv probe）

前端首次进入降压卡片时调用 `YeManTdpCtl uv probe`，返回 JSON，按结果渲染按钮状态：

```
YeManTdpCtl uv probe
→ {"vendor":"intel","supported":false,"reason":"oc_locked","current":0}
→ {"vendor":"amd","supported":true,"family":"FT6_FP7_FP8","current":-10}
```

**Intel 检测流程**（三步，全部现成 KX 调用）：
1. **读 OC Lock 位**：`rdmsr 0x194`（FLEX_RATIO），bit 20 = OC Lock。
   置 1 → BIOS 已锁超频/降压 → `supported:false, reason:"oc_locked"`（10 代后笔记本常见）
2. **写-读回验证**（OC Lock 为 0 时做最终确认）：
   写读命令 `wrmsr 0x150 0x80000010 0x0` → `rdmsr 0x150` 得当前 Core offset；
   若当前已有非 0 offset（ThrottleStop 等外部工具设的）→ 直接判定支持并回显该值
3. 无历史 offset 时写一个**极小测试值 -5mV**（任何芯片都稳）→ 读回对比：
   一致 → 支持，随后立即写回 0；读回仍为 0 → `reason:"write_ignored"`（部分 BIOS 静默丢弃）

**AMD 检测流程**（两步）：
1. 家族识别：Zen1/Zen+（1xxx/2xxx，Raven/Picasso 除外）无 CO → `reason:"family_unsupported"`
2. **发送 no-op 探测**：`set-coall 0`（CO=0 是无副作用写入）看邮箱 status：
   - `1` (OK) → 支持
   - `0xFE` (UnknownCmd) / `0` (超时) → 该固件无此命令 → `reason:"smu_rejected"`
   - 邮箱读全 0 → 先切备用 RSP 地址重试（见 3.1），仍失败才报不支持

**前端渲染规则（不支持 = 整块隐藏，不置灰）**：
| probe 结果 | UI 表现 |
|---|---|
| `supported:true` | 显示降压卡片，四个按钮正常，高亮当前档（按 current 反查最近档位） |
| `oc_locked` / `family_unsupported` / `smu_rejected` / `write_ignored` | **整个降压卡片不渲染**（v-if=false），界面干净无残留 |
| probe 超时/异常 | 同样隐藏（宁可不给功能，不给废按钮） |

隐藏而非置灰的理由：掌机屏幕小，置灰控件是纯噪音；probe 结果写入
`PowerControl\uv-probe.json` 缓存（含 CPU 名指纹），CPU 不变则启动时直接读缓存，
无需每次开卡片都探测硬件。设置页给一个「重新检测硬件支持」按钮清缓存重测即可。

```json
// PowerControl\uv-probe.json
{ "cpu": "AMD Ryzen AI MAX+ 395", "vendor": "amd", "supported": true,
  "family": "FT6_FP7_FP8", "probedAt": "2026-07-28T23:30:00" }
```

### 8.3 「降压是否真的生效」数据反馈（uv verify）

按钮应用后，除了 8.2 的读回验证，再给用户一个**看得见的证据**：

- **Intel**：`rdmsr 0x198`（IA32_PERF_STATUS）bits 47:32 = 当前核心 VID，
  `电压 = VID / 8192 V`。应用前后各采样 3 次（间隔 200ms）取中位数，
  前端显示「核心电压 0.985V → 0.938V（-47mV）✓ 降压已生效」。
  注意空闲态电压波动大，采样时让前端起一个 1 秒的单线程忙循环（Worker 里 while 空转）稳定负载。
- **AMD**：复用现有 `amd_get()` 的 PM 表读取——APU 表偏移 `0x9C`（CPU 电压，
  不同代际略有差异，按 family 表配置）；或者更稳妥的间接证据：
  同 TDP 满载下对比**平均有效频率**（PM 表 / WMI 都能拿），CO 生效 = 同功耗频率上抬。
  前端显示「25W 满载频率 4.12GHz → 4.31GHz（+4.6%）✓」。
- verify 失败（前后无变化）→ 提示「指令已接受但硬件未响应，可能被固件钳制」，
  并把该档按钮标记黄色感叹号，不影响使用但让用户知情。

---

## 九、整合 UXTU「关闭 CCD」方案

### 9.1 先说破：UXTU 的关 CCD 不是硬件关闭

逐行核对 UXTU 源码（`RyzenSmu.cs` 全部 Socket 命令表）后确认：
**SMU 邮箱层没有任何 CCD/关核命令**（只有 `disable-feature`/`per-core-oc-clk`，与关核无关）。
真正的硬件级 CCD 下电只能在 BIOS（`CCD Control`/`Downcore Control`）做，任何 Windows 工具都做不到。

UXTU 的「CCD 模式」实际是 `Services/CpuAffinityUtility.cs` 的**全局进程亲和性**方案：

```csharp
// Mode 0 = all CCDs, 1 = primary CCD, 2 = secondary CCD
private static ulong BuildMask(int mode) {
    int logical = (int)GetActiveProcessorCount(ALL_GROUPS);
    int half = logical / 2;
    return mode switch {
        0 => (1UL << logical) - 1,                          // 全核
        1 => (1UL << half) - 1,                             // 低半掩码 = CCD0
        2 => ((1UL << logical) - 1) ^ ((1UL << half) - 1),  // 高半掩码 = CCD1
    };
}
// 应用：遍历所有进程 SetAffinity + WMI Win32_ProcessStartTrace 监听新进程即设
```

要点：
- **对半切掩码**（假设两 CCD 均分逻辑核）+ **常驻监听**新进程启动即刻设亲和
- 效果：调度器无法把线程放到被排除的 CCD → 该 CCD 深度 C-state 睡眠 → 省电/减少跨 CCD 延迟，游戏场景接近 BIOS 关 CCD 的收益
- 局限：非硬件下电（睡眠 CCD 仍有微量漏电流）；受保护进程设不上（静默忽略）；>64 逻辑核不支持（掌机无此问题）

### 9.2 YeManCC 三层关核体系（整合方案）

你已有的 `3-Core.ps1`/`8-Core.ps1`（给最大内存进程设 0xFFF 掩码）就是这个思路的单进程版。
整合后按强度分三层，做成与降压同风格的按钮组：

```
┌─ CPU 核心控制 ────────────────────────────────┐
│  [全核]  [仅 CCD0]  [仅 CCD1]  [省电停驻]        │
│  状态: 仅 CCD0 (0-15 线程)   生效进程: 217 个     │
└──────────────────────────────────────────┘
```

**显示条件（无多 CCD = 不显示，与降压卡片同哲学）**：

native 启动时做一次拓扑检测（`cpu.topology` IPC），返回：

```json
{ "logical": 32, "l3Domains": 2, "ccdMasks": ["0x0000FFFF", "0xFFFF0000"] }
```

前端渲染规则：
| l3Domains | UI 表现 |
|---|---|
| `>= 2` | 完整显示 [全核] [仅CCD0] [仅CCD1] [省电停驻] |
| `1`（绝大多数掌机 APU / Intel 非双环形总线机型） | **CCD 按钮不渲染**，只显示 [全核] [省电停驻] 两钮；若认为两钮价值不足可整卡片折叠进设置页 |
| 检测失败 | 只显示 [省电停驻]（powercfg 永远可用，零风险） |

**CCD 切换后的验证机制（与 uv verify 同思路，给用户看得见的证据）**：
1. 应用掩码后延迟 2s，重新快照全部进程亲和性，统计命中掩码的进程数 →
   状态栏显示「生效进程: 217/230 个」（分母为可枚举进程总数，差值即受保护进程）
2. 命中率 < 50% → 视为失败（权限异常/杀软拦截），自动回退全核并提示
3. 持续验证：轮询线程每次设完新进程后更新计数，UI 实时刷新——
   用户能直观看到「新开的游戏也被限制在 CCD0」
4. 硬证据（可选）：HWiNFO 风格的每核负载条——被排除 CCD 的核占用应≈0%，
   前端用 `PerfCounters`/WMI 每 2s 采样 `% Processor Time` 画 32 根迷你条即可

**层 1：全局 CCD 亲和（UXTU 同款，主推）**

实现位置：**native main.cpp**（你的壳本来就常驻，比 UXTU 的 WMI 监听更轻）：
- 模式切换：遍历进程 `OpenProcess(PROCESS_SET_INFORMATION)` + `SetProcessAffinityMask`
- 新进程捕获：壳内起线程轮询进程快照（1~2s 间隔，`CreateToolhelp32Snapshot` 对比新 PID）
  ——比 WMI `ProcessStartTrace` 省资源且无 WMI 服务依赖
- 白名单跳过：`csrss/wininit/services/smss` 等系统关键进程（设不上也无妨，直接 continue）
- IPC：加 `cpu.setCcdMode {mode:0|1|2}` 命令，前端按钮直调；模式持久化进 `app.config.json`

**掩码计算——比 UXTU 做对一点**：
UXTU 对半切在 X3D 非对称 CCD（8+16）机型上是错的。用
`GetLogicalProcessorInformationEx(RelationProcessorPackage/RelationCache L3)` 按 **L3 缓存域**
分组才是真 CCD 边界（每个 CCD 独享一块 L3）。单 CCD/单 L3 机型（多数掌机 APU）→
按钮只显示 [全核] [省电停驻]，隐藏 CCD0/CCD1。

**层 2：核心停驻（powercfg，UXTU PowerPlans.cs 同款通道）**

「省电停驻」按钮 = 限制活跃核心百分比，调度器把负载收拢到少数核，其余核深度停驻：

```
# 解锁隐藏项（一次性）
powercfg -attributes SUB_PROCESSOR 0cc5b647-c1df-4637-891a-dec35c318583 -ATTRIB_HIDE
# CPMAXCORES = 最大活跃核百分比（50 = 只留一半核）
powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR ea062031-0e34-4ff1-9b6d-eb1059334028 50
powercfg /setdcvalueindex SCHEME_CURRENT SUB_PROCESSOR ea062031-0e34-4ff1-9b6d-eb1059334028 50
powercfg /setactive SCHEME_CURRENT
```

- 与你现有 PowerControl 计划任务体系同通道（bat/ps1 即可，无需驱动）
- 相比亲和性：对**所有**进程生效（含受保护进程）、掉电续航收益更直接；
  但由调度器决定收拢到哪些核，不能精确指定 CCD
- 还原 = 写回 100

**层 3：真硬件关核（只做引导，不做实现）**

设置页放一行说明：「彻底关闭 CCD 需在 BIOS 的 CCD Control / Downcore 设置，
本工具的软件方案已可获得 90% 的收益」。诚实比装能做到强。

### 9.3 与降压/TDP 的联动（掌机杀手锏）

三个能力同一套按钮哲学，可组合成场景预设（对齐你 TdpView 的档位思路）：

| 场景预设 | TDP | 降压 | 核心 |
|---|---|---|---|
| 续航模式 | 8~12W | 安全档 | 省电停驻 50% |
| 老游戏/独显直连 | 15W | 平衡档 | 仅 CCD0（避免跨 CCD 延迟） |
| 性能全开 | 上限 | 平衡档 | 全核 |

### 9.4 风险与注意

| 风险 | 对策 |
|---|---|
| 全局亲和误伤系统进程 | 白名单 + 失败静默跳过（UXTU 同策略，实践安全） |
| 游戏反作弊对 SetAffinity 敏感 | 目前无实锤案例（亲和性是公开 API）；保守起见白名单可加反作弊服务进程 |
| X3D 非对称 CCD 掩码切错 | L3 域检测替代对半切（9.2 层 1） |
| 忘记还原停驻导致性能诡异 | 开机时若 config 无停驻标记则强制写回 100（自愈） |
| 亲和性在进程启动瞬间的空窗 | 轮询 1s 间隔内新进程短暂全核运行，无实质影响 |

### 9.5 实施顺序（接第七节步骤 8 起）

| 步骤 | 内容 | 验收 |
|---|---|---|
| 8 | native: L3 域拓扑检测 + `cpu.topology`/`cpu.setCcdMode` IPC + 轮询设亲和线程 | 双 CCD 机上任务管理器可见新进程只跑半边；单 CCD 机返回 l3Domains=1 |
| 9 | powercfg 停驻 bat + 还原自愈 | HWiNFO 观察停驻核 C-state 驻留率上升 |
| 10 | 前端按钮组（按拓扑条件渲染）+ 生效进程计数验证 + app.config.json 持久化 | 单 CCD 机不显示 CCD 按钮；双 CCD 机显示「生效进程 N/M」；重启壳后模式恢复 |
| 11 | 场景预设联动（TDP+UV+核心 一键） | 三值同时生效 |

---

## 附：本计划信息来源

- UXTU master 分支（2026-06）`Scripts/Intel Backend/Intel_Management.cs`、`IntelPawnIO.cs`、`Scripts/AMD Backend/RyzenSmu.cs` 源码逐行核对
- 你的 `YeManTdpCtl.py` v2（MP1 直写已验证 Strix Halo 可用）
- CO 编码与 ryzenadj `api.c` 交叉验证
