# YMCC Windows Gaming Home

固定主页桥接器，不依赖安装 AnyFSE。采用 `windows.gamingApp` 扩展与 `Microsoft.appCategory.gamingHome_8wekyb3d8bbwe` 能力，由系统主页选择激活 `YeManGamingHome.exe`。参考项目源码在 `Build/V/XboxBoot22/xbox-source-review-20261022`，本组件独立实现，不包含第三方的 HID hook、ETW 追踪或 Xbox 结束服务。

启动顺序：验证当前包被选为主页、Windows 全屏 API 可用且处于全屏、同会话激活互斥；若固定路径的 YMCC 未运行，调用当前用户最高权限的无触发器任务；在原60秒预算内等待，YMCC 最多15秒；调用原BAT，或对已经存在的Steam窗口仅请求一次大屏；窗口交接完成后组件退出。取消/退出全屏不重试、不停主程序。确认进程存在不等于手柄就绪，窗口样式不等于Valve UI模式证据。

固定安装合同为 `C:/SOFT/YeMan/YeManCC/YeManCC.exe` 和 `C:/SOFT/YeMan/PowerControl/YeManSteam.bat`。外层BAT与 `.earlystart` 保持。内层BAT只增加YMCC已运行检查，主程序、InputHost、风扇、TDP及设备生命周期继续由现有YMCC负责。日志复用普通导出已收集的 `steam-launch.log`。

## 构建与软件验证

```powershell
pwsh -NoProfile -File tools/build-gaming-home.ps1 -OutputDirectory G:/YeManCC-Work/Build/V/XboxHome22/candidate -RunSelfTests
python -X utf8 tools/gaming-home/negative-controls.py G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC G:/YeManCC-Work/Build/V/XboxHome22/negative
```

此组件有独立构建交付入口；没有在每次完整YMCC构建中自动签发、安装或启用主页。构建使用SDK makeappx做结构校验，私钥仅保存在Build的受限目录，用户交付只有公开证书。包内块哈希、签名CMS与已签块映射/内容类型/代码完整性关联均只读验证，不将证书导入本机。

安装/检查/恢复源见 `tools/gaming-home`。注册采用持久写入日志，保留原选择、原值类型、原BAT及用户后续改动；失败仅回退本次写入。恢复不停止应用，因此暂留已注册包、公开证书和无触发器任务。

## 当前验证边界

软件测试覆盖生产控制器和注册事务的实际消费者；不能代替Windows首次注册能力授权、系统主页交接/回拉或26H2实机与实体手柄验收。与已有 `.earlystart` 同时触发的完整开机交错、输入宿主功能及风扇/TDP由用户现场测试；此次不新建监督器，也不接管原睡眠/设备任务。
