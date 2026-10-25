# 屏幕触摸板鼠标点击穿透交付（2026-10-11）

## 本次用户澄清的范围

用户明确要求：只需要在触摸板覆盖区域内，用鼠标直接点击下面的窗口；此前触摸板会挡住点击。
本次普通鼠标点击交付不以“静止双指同时操作鼠标”的人工硬件对照为前置条件。
旧的静止多触点诊断及失败记录保留，未运行的硬件测试仍不能被称为通过。

不启用无虚拟手柄的 SteamDeck/PS5 原生触控映射，不改变功能键独立动作或触觉输出，
不改变 BUS/InputHost 单写者、HWND/UI 线程所有权、电源策略、权限、UIAccess 或驱动。

## 根因和接线

既有原生输入 Surface 已能让鼠标命中下层窗口，但标准构建默认没有编入该能力，
owner 的正常启动也只在显式 candidate 参数存在时启用。因此普通启动仍使用挡鼠标的旧输入 HWND。
修改前的对照使用当前 production owner 代码：显式原生输入可点击，下层裸窗口可点击，
默认启动和显式 legacy 都挡点击；证据保留在 `before-default`。

现在：

- `native/build_native.bat` 默认编入独立输入源；仅环境值 `YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE=0` 退出为旧构建。
- `native/screen_touchpad_owner.h` 正常启动默认启用；显式 `--screen-touchpads-native-input-legacy` 或互相冲突的参数仍走旧路径。
- Touch/Pen 由受控输入源接收；Mouse 不受其控制，输入层和独立边框视觉层都不阻挡普通鼠标命中。
- 不装全局 hook、不转发、不重放鼠标点击，不靠仅同线程的模拟命中结果宣称跨进程通过。
- 显式 candidate 参数保留兼容已有调用；无需该参数即可在正常构建中使用。
- 共享工作区新增 recovery API 的使用点早于 `getKnownFolder` 声明，重建时补了同签名前置声明。
  仅修正编译声明顺序，没有更改 recovery / Sleep Guard 行为；其他任务改动未回滚。

## 验证

### 普通点击，实际跨进程下层窗口

`tools/screen_touchpads_mouse_clickthrough_selftest.cpp` 使用自有子进程作为实际下层窗口，
初始化正式 `initializeForOwner` 路径，向经所有权及前台复检的自有区域注入鼠标测试输入。
以子进程实收的 DOWN/UP 计数为准，不把 WindowFromPoint 或自测 exit 0 当作功能通过。

64 项检查全部通过：

| 路径 | 检查数 | 实收下层点击 |
|---|---:|---:|
| 无覆盖层正对照 | 1 | 1 |
| 显式 legacy 负对照 | 9 | 0（预期被挡） |
| 显式原生输入正对照 | 9 | 9 |
| 默认单板 | 9 | 9 |
| 默认双板（Mouse / WASD） | 18 | 18 |
| 默认路径尺寸调整后 | 9 | 9 |
| 默认路径 owner 恢复重建后 | 9 | 9 |

每板检查中心、四边、四角；每个成功点击均只有一个 DOWN 和一个 UP。
测试无触摸输入注入、无绑定输出、无虚拟目标、无产品部署；所有自有子进程正常结束。
这证明普通鼠标点击路由，不声称真实硬件静止双指已通过。

### 正式主 EXE 和保留触摸行为

- 标准 native 构建成功，实际 `G:\YeManCC-Work\Mainline\Build\App\Native\YeManCC.exe`：
  default / opt-in / explicit-legacy / conflict 的 owner 提前返回自测各 10 项通过，合计 40 项。
  default 和 opt-in 在启动及重启后均 active=true，后两种回退均 active=false。
- owner 接线/触摸/释放/恢复/重启夹具：原生和无原生编译路径各 35 项通过，合计 70 项。
- 完整 `verify-screen-touchpads.ps1 -WorkspaceRoot G:\YeManCC-Work\Mainline` 通过：
  Core 106、WM_POINTER 437、触觉 326、光标 104、视觉缓存 20（冗余上传 0）、
  浏览器 131、InputHost 八个自测和 native→Host 报文夹具，Vue typecheck / SFC 校验通过。
- 回归来源记录已补上 owner / input surface / native build recipe 的哈希和构建新鲜度检查。
- 普通点击验收还核对正式主 EXE 的默认分支，避免夹具绿色但正常启动仍走 legacy。

## 使用和复现

正常使用新构建，不需要额外的 candidate 启动参数。现有已运行旧 EXE 不会被测试自动替换；
未部署安装版，也未自动启动正常产品或游戏。

```powershell
# 仓库根目录，先完成标准 native 构建
pnpm run test:screen-touchpads-mouse-clickthrough

# 单独指定已构建的正式主 EXE
& .\tools\verify-screen-touchpads-mouse-clickthrough.ps1 `
  -AppExe G:\YeManCC-Work\Mainline\Build\App\Native\YeManCC.exe
```

证据：

- `G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\mouse-clickthrough\before-default\mouse-clickthrough-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\mouse-clickthrough\accepted-default\mouse-clickthrough-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\mouse-clickthrough\accepted-default\mouse-clickthrough.json`
- `G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\mouse-clickthrough\owner-lifecycle\owner-lifecycle-verification.json`
- `G:\YeManCC-Work\Mainline\Build\Validation\ScreenTouchpads\verification.json`

此前同日送达审计的默认 legacy 和完整静止多触点 gate 是当时状态；本次用户澄清后的普通点击交付以上述新证据为准。
