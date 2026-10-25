# 触摸板专用键同步中止恢复

入口：global `touch.button-abort-recovery`。复用正式 `screen_touchpad_settings_reveal_selftest.py`、`screen_touchpad_button_abort_native_selftest.ps1` 与退出回归，不复制产品算法。

现场 2026-10-23 02:01:52.373：Steam Deck 专用键 permittedMask=6，input/artworkMask=4，error=0x80004004，inputTeardown=false，autoHidden=false。关/开后 mask 恢复 6。后续正常一分钟自动隐藏单独记录，不当作同步失败。

本入口执行内存边界正反用例、真实私有桌面 WinRT dispatcher 中止及退出晚到回调检查；真实 HWND/控制源均由夹具创建并销毁，不触碰安装版、不写设备、不注入系统输入、不切换可见桌面。

触摸板是持续功能。本轮证明有界临时故障恢复和报告内退出边界；完整真实睡眠/物理手柄、强行挂起 COM 关闭、游戏消费仍未验收，`fullLifecycleAcceptance=false`。本入口不替代全局生命周期缺口，不把原失效归为驱动或监控问题。
