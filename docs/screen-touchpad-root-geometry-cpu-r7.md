# 触摸板根窗口几何缓存与 CPU 调用 R7（2026-10-15）

## 结论与现场边界

用户明确的对照是：YMCC 直接模拟 Mouse 不闪；虚拟 SteamDeck 由 Steam Input 合成鼠标时闪。本轮几何缓存减少触点外观路径的 User32 调用，不作为 SD 闪烁修复或现场改善结论。旧 policy9 归档目测无效的结果仍有效；policy10/11 只有隔离显示顺序证据，尚无新的现场视觉验收。

## 主线变化

- native/screen_touchpad_contact_visual.h：至多两个 owner-thread 根窗口 RECT 缓存。相同 HWND 且有效时复用；force 必须读取 GetWindowRect；读取失败清除有效性，隐藏该触点外观，并在下一次原有同步重试。shutdown 清空缓存。
- native/screen_touchpads.h：根窗口 WM_WINDOWPOSCHANGED、WM_DPICHANGED、WM_DISPLAYCHANGE、WM_SHOWWINDOW、WM_NCDESTROY、WM_STYLECHANGED、WM_ENABLE 通知使缓存失效。保留原有 DefWindowProc 和输入策略修复。根窗口/边框的可见性仍每次现场检查，不缓存。
- 双触点共享一个 PS 表面时仍每次 dispatch 只检查一次该表面。不改坐标转换、Core/报告、长按点击、触点 UP/cancel、震动、鼠标穿透或焦点。
- state 增加 contactGeometryCacheVersion=1 及 reads/hits/invalidations 累计量；cursorPolicyVersion 继续为11，cursorTransitionTrace.evidenceVersion 继续为1。

根 HWND 为无父 WS_POPUP，调用保持同一 UI 线程/DPI 上下文；当前源码未发现 SetThreadDpiAwarenessContext。GetWindowRect 的 DPI 虚拟化意味着将来引入混合线程 DPI 上下文时必须重新审查缓存。WM_DPICHANGED 的建议 RECT 不作为实际几何缓存来源。微软一手合同及源码路径详见证据目录 PRIMARY-REVIEW.md。

## 同夹具 Before / After

最终 tools/screen_touchpad_geometry_cpu_native_selftest.ps1 冻结实际头文件后编译，使用私有且从未显示的桌面、真实 WinRT 输入宿主和两个自有下层进程；nativeInputSink 事件和光标/前台观察是受控适配器，没有物理输入或设备输出。

| 项目 | Before / policy11 无跨事件缓存 | After / policy11 + geometry1 |
| --- | --- | --- |
| 同夹具断言 | 142项，恰好8项缓存复用失败 | 142/142 |
| 四尺寸各100次稳定 MOVE +100次根窗口移动后的 MOVE | 800次根窗口 GetWindowRect | 0次（8组均为0） |
| 光标查询 | 29 | 29 |
| 光标图像上传 / 接触光点上传 | 1 / 12 | 1 / 12 |
| 光标窗口定位 / position-only 提交 | 20 / 4 | 20 / 4 |
| 接触光点移动 / 边框上传 | 816 / 42 | 816 / 42 |

142项覆盖真实根窗口移动与缩放后正确定位/边界钳位、强制重读、根窗口及边框隐藏/再显示、读取失败不影响活动触点且下一次重试、重新落指与第二块板顺序、UP/cancel 和原生命中。DPI/display 用真实 HWND 接收人工通知，仅证明失效处理，不代表实际切换显示器或 DPI。实际 runtime/queue shutdown、UI worker退出、私有桌面关闭、下层进程退出、输入桌面保持均有成功回执。

额外回归：光标实际头文件/API替身模拟70/70；完整WinRT跨进程鼠标穿透64轮、1038断言、1600命中；输入边界源码12/12。首次源码检查误传只含两个文件的 Before/native 目录，因缺少输入宿主头文件直接失败；改用完整冻结 Delivery-Before/FrozenNative 后通过，未修改产品绕过检查。

没有测量 YMCC 真实进程 CPU 百分比。这些数据只证明消除了特定热路径的几何查询；实际收益取决于触点频率与进程其他负载。不增加轮询、刷新率、光标探针、计时器、线程或平滑。

## 复现与证据

证据根目录：G:/YeManCC-Work/Build/Analysis/Touchpad-Geometry-CPU-R7。

- Delivery-Before、Delivery-After：最终142项、冻结源码、来源哈希、编译日志及清理回执。
- Initial-Before、Candidate：早期126项隔离对照，保留原状。
- Before/native、After/native、production.diff、production-mutation-receipt.json、delivery-receipt.json：精确两文件增量及来源。
- Cursor-Regression、Native-Mouse-Pass、input-boundary-sourcecheck.json：最终相关回归。

入口：tools/screen_touchpad_geometry_cpu_native_selftest.ps1 -OutputDirectory <fresh-dir>；基线加 -BaselineDirectory <Before/native> -ExpectBaselineFailure。runner 严格要求仅8项已知复用失败，拒绝不完整队列/桌面清理。没有部署、替换安装包、重启Steam/游戏、修改用户设置或输入设备。
