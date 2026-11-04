# 游戏菜单专属参数概览：聚焦 UI 回放

仅显式选择原 `tools/lib/continuousPerformancePreview.mjs` 的 `topProfile + realNavigation` 入口，以及检查身份上下文所需的原 `draftHarness` dedicated 入口。不新增全局 test ID，不加入全页/发布父门，不改共享 helper 或原测试 owner 的文件。

运行（仓库根）：

```powershell
node tools/fixtures/game_profile_summary/run.mjs [EvidenceDirectory]
```

默认证据写 `G:/YeManCC-Work/Testing/YMCC/Evidence/game-profile-summary-<UTC时间>`。`results.json` 包含当轮实际结果、源码 SHA256 和截图，失败证据保留。

复用原预览的 Vue 编译、样式与正式 `src/gamepad/engine.ts` 导航/动作提取。adapter 在原 factory 的内存构建边界补充 profile 保存/应用结果、status 事件和身份事件端口；不会写 factory 文件，不接触真实应用、配置、进程、安装目录或硬件。实际编译 `gameCustomProfileTarget.ts`，捕获字段来自观测到的游戏；错误类取正式源码声明，不补假目标字段绕过 guard。主线程的权威性能调度 12 项回归不在本测试范围内。

1280×720 / 1280×800 共 26 个定向断言组：

- 保存成功自动展开，只渲染同一三列参数概览，无性能编辑控件，焦点到首个可达输入控件。
- 左右同行、边缘停止、上下到达页脚和返回、焦点完整可见；A 打开下拉，B 先关下拉，再折叠并返回入口。
- 手动折叠后刷新/供电变化不强制展开；AC/DC 的 TDP、频率、睿频、积极性从原组件更新，展示/导航不产生性能写入。
- 保存前失败 / 保存前失效保持收起；保存后应用失败 / 失效 / 无接管明确显示已保存并保持展开；不重试。
- 同 PID/birth 的 path / identityGeneration 变化通过原游戏/配置事件作废旧滑块编辑，之后方向动作不产生旧上下文写入。

原 `performance_compact_scroll_browser_selftest.mjs` 中“dedicated 概览必须隐藏”断言已与本需求冲突，本 helper 不运行或改写该 owner 的测试。此次是内存边界浏览器回放，未做实物手柄或安装版验收。
