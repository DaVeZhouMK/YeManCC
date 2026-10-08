# YMCC / RTSS 模板桥与自动安装

## 随 YMCC 保留的文件

完整程序包必须将以下文件放在 `YeManCC.exe` 同目录，不要只把 DLL 放进 RTSS 后删除原文件：

- `YMCCOverlayBridge.dll`：x86 RTSS Client 插件，RTSS 自己加载。
- `YMCCRtssProfileHelper.exe`：独立 x64 配置助手；第三方 RTSSHooks 不加载到 YMCC 主进程。

正式目录示例：

- 原始桥文件：`C:\SOFT\YeMan\YeManCC\YMCCOverlayBridge.dll`
- RTSS 中的副本：`C:\Program Files (x86)\RivaTuner Statistics Server\Plugins\Client\YMCCOverlayBridge.dll`

RTSS 实际安装路径由进程路径、标准目录和卸载注册表探测，不只支持上述默认目录。

## 自动安装入口

1. **启动 YMCC**：主窗口 renderer 在 `app.powerControlDir()` 解析完成后，后台调用 `ensureRtssOverlayInstalled()`。未安装 RTSS 则跳过；实际复制/权限/配置失败写入调试日志 `rtss.startup-install`，不阻塞主界面。
2. **开启监控或切换模板**：仍调用 `rtss.prepareOverlay`，可修复 YMCC 启动后被删除的文件。与模板、FPS、缩放配置共用串行队列。
3. **开机脚本启动 RTSS**：`PowerControl\RTSS-start.ps1` 在启动 RTSS **之前**复制缺失的桥、更新不同版本的桥、补齐模板、启用插件；RTSS 已运行时不更新已存在的 DLL，也不重启。原子更新受文件锁/权限阻挡时保留旧文件，更新延后，不强制卸载。

实际安装由 `native\rtss_client.h` 的 `nativeRtssPrepareOverlay()` 执行：

- 从 YMCC 程序目录原始 DLL 复制，使用临时文件加原子替换。
- **已加载且协议兼容的桥优先使用**：先对路径匹配的 RTSS 桥做 200ms 有界 Ping。成功后不比较/替换 DLL、不重写插件启用项；二进制哈希不同或更新源缺失都不阻断实时切换。文件维护延后至 RTSS 正常退出后的下一次准备/启动。
- RTSS 未加载桥时才安装/更新 DLL；相同内容不重复替换，不会强制卸载正在使用的插件。
- 真正安装失败返回错误码和目标路径；桥不响应或协议不兼容有独立错误，不会当成文件占用。
- 为四种布局补齐缺失的 `.ovl`，不覆盖已存在的用户编辑模板。
- 通过 INI API 写入 `Profiles\Config` 的 `[Plugins]`：
  - `OverlayEditor.dll=1`
  - `YMCCOverlayBridge.dll=1`
- 不改变 `OverlayEditor.cfg` 当前 Layout、Global 锁帧/缩放或其他插件设置。
- 原始 DLL 始终保留在 YMCC 目录，RTSS 重装或插件删除后可再次安装。

## 加载与安全边界

复制文件和启用配置 **不等于** 已运行的 RTSS 进程加载了桥。RTSS 已启动后才首次安装的插件，会等待 RTSS 下一次正常启动；不能通过注入 DLL、合成热键或强制重启游戏/RTSS 来绕过。

切换时单次桥通信最多等待 200ms，完整加载确认最多轮询约 4 秒；前端 IPC 同样有超时，失败/超时会释放 UI busy 状态并恢复串行队列，不自动重发 Load。超时表示结果未确认，不宣称切换成功。

模板切换通过 RTSS 中的桥调用官方 `PostOverlayMessage("Load", "", layout)`，等待完整 rebuild 后的完成应答，再保存 Layout；模板切换本身不重启 YMCC/RTSS。

## 构建与发布保障

- `native\build_native.bat` 构建并保留主程序、配置助手和桥 DLL。
- `tools\build-workspace.ps1` 将 DLL 列入必须存在的构建产物。
- `tools\package-release.ps1` 复制 DLL 到发布程序目录，并作为必需文件和构建新鲜度检查项。
- `tools\deploy-installed.ps1` 部署 DLL 到 YMCC 目录并核对构建哈希。

完整发布/更新必须同时更新主程序、前端和这些依赖；不要只交付前端或 RTSS 副本。

## 回归验证

- `pnpm run test:rtss-overlay`：启动入口、未安装 RTSS、错误恢复、串行切换、发布文件保留。
- `pnpm run test:rtss-overlay-native`：实际 C++ 安装函数、私有空目录首次安装/删除后恢复、保持用户配置、已加载旧桥/随包新版 DLL、协议不兼容、桥不响应、加载超时后恢复、100 次 mock 模板切换；同时调用开机脚本安装/升级/文件锁自测。
- `tools\rtss_overlay_install_selftest.ps1`：调用实际开机脚本 `-RtssDirectory <private-fixture> -InstallOnly`，测试文件/配置准备，不启动任何 RTSS 进程。需已构建桥 DLL。

这些测试只操作指定 Build 验证目录，不删除或替换本机真实 RTSS 插件，不重启真实 YMCC、RTSS 或游戏。