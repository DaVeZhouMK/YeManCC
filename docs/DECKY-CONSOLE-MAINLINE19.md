# YMCC 控制台 — Mainline19：首启环境、独立页、真实来源接线与风扇挡位

日期：2026-10-09（Asia/Hong_Kong）。源码仅维护主线，证据位于 `G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Sidebar\validation`。

## 1. 用户只有 Steam 的首次启用

- 完整 YMCC 分发包已包含固定 SHA256 的 Windows Loader EXE、已打包的前端插件和许可证；本插件无 Python 后端，不要求用户另外安装 Decky、Python、Node/npm 或开发工具。
- 主线 Steam 设置页默认进行**只读环境检查**。控制台仍保持明确启用开关：第一次开启时自动准备所需 Steam 本地调试标记，保存原扩展启用状态并与 Steam 联动。
- 若标记缺失，只有验证 Steam 安装、Loader 校验和插件资源之后才以 `CREATE_NEW` 创建空标记；已有标记内容不覆盖，异常目录/重解析标记拒绝处理。
- Steam 已运行时标记不会立即生效，明确提示用户方便时正常重启；不强关游戏，不重启或结束用户 Steam 进程。
- 完整包缺 Loader/插件、校验不通过、Steam 未安装、目录权限不足或已有 Loader 端口冲突均明确失败；不执行远程安装脚本、不下载未校验新版、不抢占其他 Loader。
- 关闭控制台不删除其他插件或已有 Steam 标记。原控制台/游戏/风扇配置 owner 不迁移、不另建目录。
- 14 项 C++ Windows 文件操作测试通过。**测试是在 Build 的模拟 Steam/资源目录完成，校验回调注入用于单元测试；不是“纯 Steam 新机器完整安装”验收。**真实 Loader 文件 SHA 另由产物构建检查验证。

实现：`native/decky_sidebar_setup.h`、`native/main.cpp` 的原扩展启动增量，以及 `src/bridge/deckySidebar.ts`、`src/views/SteamView.vue`。

## 2. 独立、直接显示的 YMCC 控制台

- 不再让用户先进入 Decky 插件列表再选择一个内部名称为 `ymcc-sidebar` 的条目。
- 注册顶部独立标签 **YMCC 控制台**（ID 100019），进入该标签即显示调节区。原 Decky 入口（999）保留在原位置给其他插件。
- 真实 Steam 标签顺序：`100019,0,3,4,5,7,6,999`；100019 在通知上方。
- 返回给 Decky 列表的 content 为 null，真实接口记录 `selectedPlugin=null`、`hasDeckyListContent=false`，避免第二个 Content/镜像客户端实例。
- 独立页不可安装时保留普通插件页作为兼容回退，不抢占第三方相同 ID，不修改 Loader 二进制。
- 使用 Loader 原标签注册和可见性 Provider，保留原页、其他标签和其他补丁；有界弱收据达到安全上限时整体撤销自己的页，避免在原 Loader 计数机制中留下半移除数组。
- 9 项精确生产 helper＋原版 TabsHook/DFL patch-stack 检查通过；另在真实 Windows Steam 证明无需选中 Decky 插件即可直接显示。

## 3. 真实数据接线的范围与尚未完成项

- 生产宿主继续调用原 `performanceSchedule`/独立 EXE 配置、原游戏确认和 CPU 拓扑、原全局 FanHost；没有在交付插件中放示例游戏或替代硬件宿主。
- 新增主进程来源元数据：真实 native `deckySidebar.mirrorAttach` 返回 `owner=YMCC-native` 与当前进程 PID；原宿主通过原 IPC 验证并推送 `provenance`，页面展示来源。缺少/异常元数据不伪称真实接管。
- “方案值”与“硬件回读”仍明确区分；来源 PID 本身也**不是** TDP/Fan 下发成功的证明。
- 使用真实安装路径 `C:\SOFT\YeMan\PowerControl\game-custom.json`、完整原配置模型做只读验证，文件指纹未变；当前真实 EXE 方案表为空。没有持久化全局 section 的项目只显示原代码工厂默认值，并明确标记其来源，未伪造用户保存项。
- **本轮没有启动产品 YMCC EXE，也没有做真实原进程→Steam→硬件的闭环验收。**运行原主程序可能执行已保存的 CPU 自启、风扇恢复等原行为，因此已向用户请求启动许可；在确认前不擅自执行。
- 真实 Steam 的本轮 UI 会话仍采用明确标注的内存业务边界，并显示来源未确认。不能把这张截图或真实文件读取当成真实进程接管完成。

## 4. 全局风扇：开关＋三挡

- 恢复软/均衡/暴力三个原全局预设的离散滑块，复用原已保存曲线和原生命周期，不独立记一套侧栏配置。
- 开关关闭：控件置灰、回调不发请求；镜像宿主额外拒绝 `fan.setPreset`，不暗中保存或启动风扇。
- 开关开启且宿主可控：仍经原 `createFanMirrorActions`/FanHost owner 保存与下发。原主界面允许关闭时编辑曲线的业务 API 不改，此限制只加在镜像入口。
- 真实 Steam 的关闭状态置灰已验证；开启后的原生命周期路径由原 no-IO 集成测试覆盖，**真实风扇开启/挡位写入仍未验收**。

## 证据

- `MAINLINE19-SETUP-TEST.json`：14 项 Windows 模拟首启检查。
- `MAINLINE19-PLUGIN-PRESENTATION.json`：16 项生产 TSX 绑定/禁用/菜单检查。
- `MAINLINE19-QAM-VISIBILITY.json`：14 项真实锁定 API 可见性 Hook 与生产客户端惰性检查。
- `MAINLINE19-QAM-PLACEMENT.json`：9 项独立页生命周期、单 Content、原生标签与 patch-stack 检查。
- `MAINLINE19-OWNER-METADATA.json`：4 组惰性元数据/关闭时 Fan 请求拒绝检查；不是实进程来源证明。
- `MAINLINE19-REAL-CONFIG-READONLY.json`：实际文件→原模型读取，0 写入，不是 live app/Steam 验收。
- `MAINLINE19-QAM-DIRECT-ACCEPTANCE.json` / `MAINLINE19-QAM-DIRECT.png`：真实 QAM 中直接显示；业务仍是内存测试。
- `mainline19-web-regression.txt`：完整主线回归、类型检查、Vite 和最终插件新鲜度检查。
- `MAINLINE19-NATIVE-BUILD.txt`：主线 native compile/link 成功，候选 EXE 没有执行。
- `MAINLINE19-NATIVE-ADOPTION.json` / `MAINLINE19-SOURCE-PRESERVATION.json`：剥离 12 段镜像增量后的当前原业务体校验；原业务基准包含并发窗口定位改动，未回退到旧源码。

## 保持、资源与交付边界

- 73 个选定真实配置指纹未变，真实 Steam 已有调试标记未改；没有替换安装版。
- 观察到并发窗口定位相关原源码/前端修改，单列保持，未覆盖或回退；不能声称全部主线文件均未变化。
- 真实 UI fixture 受期限清理，控制器退出码 0、watchers 0。Steam 另正常退出，最终进程和运行时解包残留均为 0；不把 fixture 的期限清理当作本轮 Steam 退出观察器验收。
- 外部仍 1 个 Loader EXE、0 个新 DLL/辅助 EXE；最终插件脚本 18,815 字节，6 个外部交付文件合计 14,055,943 字节。
- 主线候选 native EXE 为 5,493,248 字节，未执行。其变化还含并发窗口定位工作，不能把整个 EXE 大小变化全归于控制台。
- 截图使用的 payload 为 18,826 字节；最终少 11 字节的差异仅扩展旧 snapshot 无 provenance 时的未确认提示，最终契约与完整回归均再跑通过。
- 没有制作正式发布 ZIP；真实主进程接管、物理手柄、真实睡眠、CSSLoader 完整共存仍待验收。
