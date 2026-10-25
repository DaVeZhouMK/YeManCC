# 四模式 MP4 预览接入 · 2026-10-25

四个固定开机模式的内页封面和编辑器小预览共用 `BootModePreview.vue`，现使用用户提供的本地静音 H.264 MP4。模式选择、保存、下次启动、Steam/Xbox/手柄配置的业务逻辑没有在此次媒体任务中变更。

| 顺序 | mode ID | MP4 | 静态封面 |
|---|---|---|---|
| 1 | global | /boot-modes/mode-01-global.mp4 | /boot-modes/mode-01-global.jpg |
| 2 | xbox | /boot-modes/mode-02-xbox.mp4 | /boot-modes/mode-02-xbox.jpg |
| 3 | steam | /boot-modes/mode-03-steam.mp4 | /boot-modes/mode-03-steam.jpg |
| 4 | xbox-steam | /boot-modes/mode-04-steam-xbox.mp4 | /boot-modes/mode-04-steam-xbox.jpg |

素材源：用户的 `四模式_MP4_接入包.zip`，SHA256 `9f673fc0dd9cb4395367daf5ab4da31ebfab4d5389fd8f28feaf93ea9b2e13dd`。第一模式沿用接入包选定的绿色 X11 素材。所有视频为 960×540、24fps、无音轨；4段MP4共753157字节，4张封面共49874字节。编码信息来自素材清单，实际浏览器核验尺寸、时长、解码帧数、播放推进和循环。文件体积与编码规格不能当作CPU/GPU实测结论。

## 播放与退出

- `src/bridge/bootModePreview.ts` 是唯一四模式素材映射。文件在 `public/boot-modes/`，Vite正常复制到前端产物。
- 沿用现有预览框、`object-fit:cover` 和非交互媒体区域。组件根节点保持 `position:relative`，不能改成脱离编辑器缩略图的整屏定位。
- 使用原生 video 的 autoplay、muted、loop、playsinline、preload=metadata；没有播放器依赖、播放控件或新的手柄焦点。
- 内页打开编辑器时显式禁用后面的预览。弹窗、内嵌手柄编辑器打开时也停止被遮挡的预览。
- 复用正式 uiLifecycle 接收文档、原生窗口和电源可见性。IntersectionObserver负责滚出可见区；Vue KeepAlive停用/恢复和卸载均有释放处理。
- 停用时 pause，删除src并load，取消过时的播放请求；重新可见时再加载。没有定时轮询、后台四路视频或自动重复失败重试。
- 自动播放拒绝、HTTP失败或解码失败显示对应JPG；JPG也失败时保留模式文字。切换模式会清除上个模式的失败状态。
- 原生 `guessMimeType` 与 `webViewFallbackMimeType` 均补充 video/mp4；默认WebView目录映射仍复用原路径。浏览器验证包含完整200响应下的播放及循环，未声称完成安装版WebView2实机解码验收。

## 持续演示

项目保存在 `G:\YeManCC-Work\YMCC演示`。原开发入口 `development/boot-mode-ui-mainline-20261023/build.mjs` 重建实际主线组件，并自动复制映射引用的8个媒体文件到 `site/boot-modes`。`SOURCE.json`记录组件与媒体哈希，后续重建可继续复用。现有演示服务已支持video/mp4，本轮无需更改或重启服务。

## 有界验收

沿用原 `settings.boot-modes-browser` 与 `settings.boot-modes-file-ui`；不扩建新父门。浏览器使用实际组件、原始手柄引擎和正式uiLifecycle，文件与设备操作走隔离端口。720P/800P覆盖自然方向、A/B、下拉、滑块和返回；新增媒体真实解码/循环、互斥播放、窗口/文档/路由隐藏释放、缺失素材与拒绝播放负例。

本次证据目录：`G:\YeManCC-Work\Testing\YMCC\Evidence\BootMode-MP4-20261025`。保留初次夹具失败及修正后的结果，不把浏览器回放当成实物手柄、安装版或硬件解码验收。原生完整生产语法、两个生产MIME函数的编译执行、TypeScript检查、Vite构建及独立演示素材哈希分别记录。

## 用户追加：缩短黑场

原包保留，本次媒体成品按24fps精确裁帧并重新编码为H.264 Main / yuv420p / 无音轨，faststart。全局裁掉前32帧（1.333秒），Xbox裁掉前67帧（2.792秒），Steam裁掉前2帧（0.083秒），保留约0.1–0.25秒的起始暗场过渡。第4段保留前86帧，移除末尾约1.417秒纯黑，缩短循环等待。未删动画中的Steam最终标志；宽画幅黑场检测会把面积很小的有效标志判成暗场，因此结合逐帧亮度与缩略图人工复核。

四段新时长分别为65/24、174/24、142/24、86/24秒。对应JPG从清晰动画帧重新生成，避免加载回退显示暗帧。原包哈希和原始统计仍是输入身份，最终成品大小、SHA256、裁切范围和编码命令以本次证据的trim/TRIMMED-ASSETS.json为准。
