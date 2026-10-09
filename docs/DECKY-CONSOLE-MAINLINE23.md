# YMCC 控制台 Mainline23 — 独立 AC/DC 锁帧与全局触摸板

日期：2026-10-09（香港时间）。所有改动在主线；产物与测试输出隔离到 `G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Console23`。

## 1. 当前 AC/DC 独立锁帧

- 接主线 `frameRateLimits.ts` / `frameRateModel.ts`，不从性能组合的 `fpsTarget` 取当前帧率，不新增第二个锁帧 owner。
- 按主线电源事件只显示当前侧：**插电锁帧 AC** 或 **电池锁帧 DC**。未知供电时不猜 AC，也不允许提交另一侧。
- 一行帧率滑条＋一行上限下拉，与原 `FrameRatePair.vue` 同语义：不锁帧 / 30 / 60 / 90 / 120 / 200 / 300 FPS。滑条拖动只更新界面草稿，完成后提交；取消锁帧保留原最后非零帧率，重开按原上限恢复/夹取。
- 没有确认游戏时保存原全局 AC/DC；有确认游戏时保存原 EXE RTSS 表，并通过原 `applyIndependentFrameRates` 应用。首次专属仍整套记忆，已有专属性能/输入字段和另一侧帧率保持。
- 性能档位切换不改独立锁帧；不在控制台复制 RTSS 写文件、加载、进程调速或浮动优化算法。失败明确区分“已保存但应用待确认”与真正应用成功。
- 帧率已从性能方案标签剥离，避免标签上的旧 FPS 与独立数值相冲突。

## 2. 触摸板：原全局入口，无游戏专属

主页面四个主下拉：**屏幕触摸板、YMCC 呼出、专用按键、背部按键**。

- 关闭布局仍保留四个主入口；单触摸板显示原单映射，双触摸板显示原左/右映射。额外映射属于同一全局模块，不是新建的游戏专属。
- 从原全局 `input.outputTarget` 选择原触摸板人格配置槽。故意不使用当前游戏的 `padPersona` 或 `gameOverride`；切游戏不能把全局触摸板设置写到专属表。
- 复用 `screenTouchpadsGet` / `screenTouchpadsSet` 的原 native 持久化入口。Mask 和 enabled 联动、PS/SteamDeck 能力禁选、PS4 兼容、布局切换的本机映射回退与原页面一致。
- 不改原缩放、透明度、鼠标灵敏度；缺能力/未知来源/保存失败时不可假称成功。
- 所有新增下拉沿用已有统一宽度、与上方触发器左右对齐、同 QAM 向下展开、取消后返回原控件焦点。

## 协议和安全边界

新增两个明确的镜像命令 `frame.setField`、`touchpad.setField`，同时更新原 C++ broker 白名单、TypeScript relay 和前端 client；旧安装版不支持新增命令，不能只换前端却声称已完成部署。

- Relay 的 frame/touchpad 来源快照留在服务端 WeakMap，不出现在公开 snapshot、不让客户端传入真实配置来源。
- 档位/字段和值采用允许名单；沿用原进程身份、来源修订、私有源数据、共享快捷动作锁、电源代际和重放去重。
- 帧率提交前重读原 RTSS/全局数据；触摸板提交前重读原全局人格槽和配置，拒绝源变化，不自动重试到另一游戏或另一人格。
- 不增加定时轮询；更新来自主线 frame、game、power、input 和 screenTouchpads 事件。

## 验收与构建

证据目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Console23\validation`。

- `MAINLINE23-REAL-UI-ACCEPTANCE.json`：实际 Steam / DFL 鼠标与键盘输入，10 步通过；当前 AC120、拖动松手后提交 AC90、上限/不锁帧/重开、四主下拉及映射、无 EXE 触摸板写入、事件切换 DC60→45、返回 AC 保持60、无游戏仍能操作全局触摸板、统一宽度和取消焦点。
- 该实机界面测试的 **业务数据和供电/游戏切换都是明确的内存 fixture**；5 次内存 EXE 帧率保存、5 次内存 limiter 应用、7 次内存全局触摸板保存，0 次真实业务配置写入或硬件写入。
- `MAINLINE23-FRAME-TOUCHPAD.json`：21 项精确生产 host/relay/adapter 加原配置模型的隔离回归，覆盖私有 source、AC/DC、全局/专属、能力与 mask、源竞争、进程身份、锁和电源拒绝。
- `REGRESSION-FINAL.txt`：27 组控制台/原配置/镜像/原 owner/native loopback 回归通过。旧测试仅更新对主线新增独立 RTSS 规范化迁移的预期，不改产品的迁移规则。
- `independent-frame-rate/service-results.json`：原独立锁帧服务/常驻 owner 的 31 项回归。
- `independent-frame-rate/browser`：原 Vue 独立控件 17 项浏览器回归，无页面错误。
- 原生 5 组 source-built fixture：27+26+33+32+28=146 项通过。
- `WEB-BUILD.txt`：类型检查与 Vite 构建通过。已有大 chunk / 混合静态动态导入警告保留，不掩盖也不当成编译失败。
- `native-link-mainline15.txt`：主线 native compile/link 成功，产物在 `Build\App\Native\YeManCC.exe`，**没有执行这个产品候选**。
- 最终插件 payload 新鲜度通过，前端编译产物在 `Build\App\Web`。

## 未完成项，不能算作已通过

本轮没有替换安装版，没有操作真实 RTSS、触摸板、风扇、变速或 LosslessScaling。Steam 在验收前处于桌面模式，使用已运行实例的正常 `steam://open/bigpicture` 入口进入大屏；没有关闭或重启 Steam。

前轮真实 native bootstrap 的加载器降权错误 5、真实 YMCC 接管仍未闭环。本轮成功的是主线代码、构建、原 owner 隔离回归和实际 Steam 界面验收，不以普通权限内存测试冒充管理员接线或真实硬件验收。
