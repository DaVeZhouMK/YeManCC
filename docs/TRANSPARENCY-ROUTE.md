# YeManCC 窗口透明技术路线（定稿）

> 更新时间：2026-08-01 10:10
> 状态：**已定稿并部署，用户验收通过（10:10）**——整体实体、可点击、
> 仅保留前端 rgba 3% 的极淡透出质感。

## 一、最终方案（当前部署）

**组合：普通 WebView2 Controller + 逐像素透明 + 前端分块实体底板 + zoom 命中修复**

```
Vue 前端（分块实体底板）
  ├── 根 .app-shell  background: transparent
  ├── 导航 .navrail   background: rgba(11,16,24,0.98)   ← 98% 不透明
  ├── 内容 .app-main  background: rgba(11,16,24,0.97)   ← 97% 不透明
  ├── 卡片 .card      background: var(--bg-panel)=rgba(13,19,28,0.97)
  ├── 输入/滑块/开关  #151e2b 100% 不透明
  └── 文字/图标       实色，不受整窗淡化影响
+ 原生壳 native/main.cpp
  ├── WS_EX_LAYERED
  ├── SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)  ← 整窗不淡化
  └── WebView2 DefaultBackgroundColor alpha=0（透明根，逐像素合成）
```

### 为什么是逐像素合成

- WebView2 `DefaultBackgroundColor alpha=0` + `WS_EX_LAYERED` 是微软官方
  透明窗口组合：每个像素按自身 alpha 合成到桌面。
- 空白像素（alpha=0）→ 透出桌面；
- 底板像素（rgba 0.97）→ 97% 遮罩、3% 透出；
- 文字/图标像素（alpha=255）→ 100% 不透明。
- **不需要** `SetLayeredWindowAttributes` 的整窗 alpha 参与淡化
  （已设 255 = 无淡化），透明观感完全由前端 rgba 提供。

## 二、点击命中（曾经的关键 bug）

- 缩放必须用 CSS `zoom`（`App.vue`：`zoom: uiScale`）——zoom 同时参与
  布局尺寸与鼠标命中测试。
- **禁止** `transform: scale()`——它只放大绘制层、不影响命中坐标，
  导致"看得到但点不到"，表现为只有导航左上角前两项能点击。
- 实测：zoom 修复后导航全项、内容区开关、退出按钮均正常。

## 三、透明度参数演进（用户验收记录）

| alpha | 整窗淡化 | 用户反馈 |
|-------|---------|---------|
| 128   | ~50%    | 能透桌面，但文字也淡 |
| 204   | ~20%    | 仍太透明 |
| 240   | ~6%     | 仍太透明 |
| 247   | ~3%     | 仍太透 |
| 252   | ~1%     | "这个可以"，但发现底板半透明额外透出 |
| 255   | 0%      | **定稿**：整窗不淡化 + 前端底板 97-98% 实体 |

> 结论：整窗 alpha 提到 255（不透明最小值）后，剩余"透明感"全部来自
> 前端 rgba 底板（3%），用户接受"只有一点点透明感"。

## 四、已放弃的路线（勿再走回头路）

- DWM `ACCENT_ENABLE_BLURBEHIND` / `ACRYLICBLURBEHIND`：不透桌面
- `DWMWA_SYSTEMBACKDROP_TYPE = Acrylic`：本机/此窗口样式下不透
- `DwmExtendFrameIntoClientArea`：灰色玻璃，非真透
- CSS `backdrop-filter: blur()`：只能模糊 WebView 自身内容，不能模糊桌面
- CompositionController + DirectComposition：曾出现深色页底、无桌面纹理，
  未走通；当前方案无需它即可逐像素透明
- `--disable-gpu-compositing`：导致启动可见性/稳定性异常

## 五、部署位置与流程

- 构建：`npm run build`（前端）→ `dist`；`native\main.cpp` 用完整 MSVC
  环境编译 → `native\YeManCC.exe`
- 同步三处：`native\YeManCC.exe`、`dist\YeManCC.exe`、
  `C:\SOFT\YeMan\YeManCC\YeManCC.exe`
- 正式目录还需同步：`index.html`、`assets/`、`icons/`、`gamepad-base.png`、
  `app.config.json`
- 部署规范：遇 exe 占用直接强杀进程；清除
  `C:\Users\DaVe\AppData\Local\YeManCC\EBWebView` 缓存
- 注：`npm run build` 的 postbuild 会复制当时的 native exe 到 dist，
  若 native 有更新需在 build 后再同步一次 exe
