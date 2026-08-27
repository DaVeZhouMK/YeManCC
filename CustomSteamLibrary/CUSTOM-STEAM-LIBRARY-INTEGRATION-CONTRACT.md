# Custom Steam Library 接入契约（YeManCC protocol 1）

状态（2026-08-25）：protocol 1 运行时接入、三目录升级适配和 CustomSteamLibrary 独立隐藏启动健康握手已完成源码与临时包回归；正式安装目录和实体手柄窗口级验收仍未完成。独立发行目录为 `C:\SOFT\YeMan\YeManCC\CustomSteamLibrary`；YeManCC 已增加一级菜单 `自定义游戏库`、异步启动/关闭 IPC 和唯一手柄输入转发。本次只迁移 CustomSteamLibrary 包自身，YeManCC 现有固定桥接路径和升级器未在本轮修改。

## 1. 角色

- `CustomSteamLibrary.exe`：面向用户的窗口宿主、WebView2 界面、键盘和独立运行时的手柄适配。开发目录仍保留 `SteamLibraryWorkspace.exe` 作为兼容构建名；独立入口已嵌入多尺寸产品图标和 PE 产品信息。
- `SteamArtworkLab.exe`：后台 Worker，只接受命令行任务并输出扫描、识别、素材和配置结果；不是用户界面，直接双击不能作为 Custom Steam Library 打开。
- YeManCC：一级菜单和统一空间导航的拥有者；负责启动/激活宿主、关闭/返回和提供语义手柄动作。

## 2. 启动契约

最终接入时由 YeManCC 启动宿主：

```text
CustomSteamLibrary.exe --integration=YeManCC --protocol=1 --parent-pid=<pid> --input-owner=parent
```

独立运行保持：

```text
CustomSteamLibrary.exe --input-owner=host
```

独立运行仍使用 `host` 输入所有者。宿主只在参数同时满足 `integration=YeManCC`、`protocol=1`、有效 `parent-pid` 和 `input-owner=parent` 时接受父进程语义通道；父模式不会启动子程序自己的 XInput 定时器。发行包可以同时支持独立运行和父程序接入，清单的 `mainProgramIntegration=true` 表示运行时协议和三目录升级适配已具备。

## 3. 手柄语义动作

主程序与宿主之间只传递语义动作，不同时传原始方向键和 XInput 状态：

```text
navigate-left
navigate-right
navigate-up
navigate-down
accept
back
tab-previous
tab-next
edit
```

规则：

1. 同一时刻只能有一个输入所有者；
2. 游戏卡牌区域内，左右只在当前行移动，上下按可见网格列移动；
3. 弹出层打开时，方向键只在当前弹出层内移动，B 只关闭当前层；
4. 返回主程序由宿主发送一次 `back` 结果后交还焦点，不重复发送 Escape；
5. 子进程独立运行时由宿主把 XInput 转换成单一语义动作事件；隐藏接入时由 YeManCC 统一引擎转换并通过语义通道发送，宿主不启动第二套 XInput 轮询。
6. 输入裁决只属于 YeManCC 原生手柄循环：原生在启动前进入 `launching`，识别到匹配的子进程后进入 `child-active`，窗口消失后进入 `returning`，检测到中性释放后回到 `disabled`。网页桥不得维护第二套输入状态机。
7. 宿主窗口必须带 `YeManSteamLibrary.InputOwner` 标记：独立运行是 `host=1`，YeManCC 接管是 `parent=2`；标记不匹配时启动返回冲突码 `7`，不得激活或复用错误输入所有者的窗口。

## 4. 生命周期

- 重复点击一级菜单：激活已有宿主窗口，不启动第二个实例。
- 宿主启动失败：主程序显示失败原因，不假定 Worker 失败；缺少 UI/Worker/WebView2 要区分提示。
- 宿主返回：关闭或隐藏 Custom Steam Library 后，YeManCC 恢复焦点。
- 输入隔离：子窗口启动中、活动中和返回中，YeManCC 不执行 B 双击隐藏、LB+RB 呼出、Start+方向调节、选择键组合和父页面导航；子窗口关闭后等待中性/释放边界再恢复。
- 所有者冲突：主程序原生只接受窗口类、`CustomSteamLibrary.exe`/开发宿主名、`parent=2` 和当前 YeManCC PID 全部匹配的窗口；独立 `host` 窗口不会触发主程序屏蔽。
- 父进程退出：接入模式下宿主进入可定义的独立保留或安全退出策略，必须在发布前选择并测试。

## 5. 交付前验收闸门

以下内容全部通过后，才标记本阶段交付完成：

- 当前开发布局、数据根和备份恢复回归通过；
- 独立宿主编辑页、720P/800P、键盘和手柄通过；
- YeManCC 只保留一个手柄输入所有者；
- 启动、激活、返回、重复启动和异常退出通过；
- 发行包清单、图标、PE 信息和升级策略冻结；升级提交前必须完成 Custom 独立隐藏启动健康握手和 YeManCC 主程序握手，正式安装目录验收单独执行。

## 6. 本轮实施边界

- 主程序源码入口：`src/router.ts`、`src/views/CustomSteamLibraryView.vue`。
- 主程序原生桥：固定调用 `C:\SOFT\YeMan\CustomSteamLibrary\CustomSteamLibrary.exe`，启动命令不经过可编辑路径输入。
- 输入所有权：子程序运行期间由 YeManCC 原生 XInput 线程唯一消费；主程序前端引擎通过 `isChildInputActive` 让出 UI 操作，启动按键必须先释放后才允许转发。
- 默认安装目录当前有正在运行的旧实例时，不强制覆盖；需先正常关闭 `YeManCC.exe` 和 `CustomSteamLibrary.exe`，再部署最新包。
