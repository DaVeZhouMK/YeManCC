# 监控渲染器纯外观更新跳过尺寸读取 — 2026-10-17

已更新主线 src/bridge/monitorHudRenderer.ts；未部署、未启动或重启本机产品/游戏。

## 审查结果与改动

当前生产模板在数值变化时已复用布局/观察器。用实际 Vue renderer/SFC/CSS 在 headless Edge 中回放 13 个模板，每个 30 次更新：FPS 117→111、核心图块和帧时间曲线真实发生 DOM 更新，但没有额外 getBoundingClientRect/getComputedStyle、ResizeObserver 重建、布局回执或图片 src 重设。探针有字段开关的正向控制，证明不是数据根本没更新或计数器失效。

仍存在一处可去掉的工作：只调整背景透明度、图片透明度、饱和度、圆角时，renderer.update 原本也会 nextTick(measure)，发起尺寸读取与 onSize 回执。13 模板各十次这类调整，合计 130 次读取/回执。

现在颜色、静态图片透明度等不影响尺寸的更新只更新 Vue 外观。原有 placement/layout/scale/gap/fields 比较，加上 FPS 突出显示与 hideUnavailable，仍触发明确测量；viewport 变化和已有 ResizeObserver 路径保留。没有把所有外观变动一概视为尺寸不变。

## 验证

- 新浏览器回归 monitor_hud_paint_geometry_selftest.mjs：13 模板、390 次数值更新实际改变文字/曲线/16 核图块；常规数值更新额外布局读取、观察器重建、布局回执均为 0。
- 13 模板各 10 次纯外观更新：合计 130 次额外尺寸读取和回执降为 0。实际圆角变为 14px、背景图透明度变为 0.55，板块宽高保持不变；没有靠忽略外观更新得到零调用。
- 字段开关仍发生布局测量。另禁用 ResizeObserver，以 capsule 验证 scale、FPS 突出显示、自动隐藏关闭/开启四类明确测量；全部通过。
- 两个文档使用同一生产 renderer 的现有一致性回归通过；缩放回归 40 项/114 次尺寸检查、游戏 LOGO 9 项/13 布局以及 vue-tsc --noEmit 均通过。
- 游戏 LOGO 备用测试 PNG 的 IDAT CRC 原先错误，新的独立输出目录暴露了该问题。只修复该测试常量的四字节校验值；未修改生产图片校验或图像导入代码。修复后备用 PNG 的真实浏览器解码/灰度/裁边/WebP 流程通过。

这里只是浏览器中合成测试数据的行为/工作次数，不是实际 Steam、WebView2、游戏叠加层或整进程 CPU 验收。没有新增采集、轮询或定时器，也没有改变 FPS 进入/消失、统一数据有效期或 HWiNFO 设置。
