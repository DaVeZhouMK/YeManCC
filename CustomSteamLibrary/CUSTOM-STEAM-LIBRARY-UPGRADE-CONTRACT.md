# Custom Steam Library 升级契约（与 YeManCC 升级器对齐）

状态（2026-08-25）：YeManCC 三目录 `green-child` 升级适配、CustomSteamLibrary 独立隐藏启动健康握手、完整事务回滚和临时包模拟回归已完成；尚未对 `C:\SOFT\YeMan\YeManCC` 正式安装目录执行真实升级。`mainProgramIntegration=true` 同时表示 protocol 1 运行时接入和当前主程序升级器已具备子包适配，但不代表实体手柄窗口验收已完成。本轮只更新 CustomSteamLibrary 包自身的默认目录，不修改 YeManCC 升级器。

这份文档解决三个容易混淆的问题：

1. `CustomSteamLibrary.exe` 是用户入口，`SteamArtworkLab.exe` 是后台 Worker，不能把 Worker 当成入口升级。
2. C 盘程序文件和 D 盘配置、缓存、图片必须分开处理，程序升级不能覆盖数据。
3. YeManCC 当前的 `app.installUpdate` 使用 `update-manifest.json` 接受并列的 `YeManCC`、`PowerControl` 和 `CustomSteamLibrary` 根目录；Custom 子程序仍按独立清单和受管路径更新。

## 1. 当前资源盘点

| 层级 | 当前位置 | 角色 | 升级处理 |
| --- | --- | --- | --- |
| 源码 | `C:\SOFT\YeManCC-Work\SteamArtworkLab` | 宿主、Worker、Web UI、验证脚本和任务文档 | 不进入用户升级包 |
| 开发构建 | `SteamArtworkLab\build` | `SteamLibraryWorkspace.exe`、`SteamArtworkLab.exe` 等测试产物 | 只选择经过验证的两个构建文件 |
| 绿色程序包 | `C:\SOFT\YeMan\YeManCC\CustomSteamLibrary` | `CustomSteamLibrary.exe`、Worker、UI、图标和清单 | 只覆盖受管程序文件 |
| 持久数据 | `D:\YeMan\CustomSteamLibrary\data` | 配置、游戏信息、图片、缓存、任务和状态 | 永不随程序包覆盖或删除 |
| YeManCC 主程序源码 | `C:\SOFT\YeManXX\YeManCC4\YeManCC3` | 一级菜单、统一手柄、主程序升级器 | 三目录升级适配和子程序健康握手已启用 |
| 主程序更新暂存 | `%LOCALAPPDATA%\YeManCC\update` | YeManCC 自己的下载、暂存和回滚 | 不能直接作为子程序数据目录 |

当前实际数据根标记为 `dataLayoutVersion=2`，并明确声明图片、配置和游戏元数据可迁移，游戏本体文件不存放在这里。旧目录 `D:\YeMan\Steam大屏` 只作为历史路径记录，不自动合并。

## 2. 包身份和目录结构

Custom Steam Library 的独立升级包采用独立版本号和独立包身份：

```text
packageId: custom-steam-library
packageType: green-child
packageVersion: x.y.z
entryPoint: CustomSteamLibrary.exe
worker: SteamArtworkLab.exe
mainProgramIntegration: true
```

安装后的目录边界：

```text
C:\SOFT\YeMan\YeManCC\CustomSteamLibrary\
├─ CustomSteamLibrary.exe          # 受管程序文件
├─ SteamArtworkLab.exe             # 受管 Worker
├─ workspace-ui\                   # 受管 UI 文件
├─ assets\custom-steam-library.ico # 受管图标
├─ package-manifest.json           # 受管包清单
├─ run-workspace.bat               # 便捷启动文件，可覆盖
├─ backups\upgrades\              # 子程序升级器的程序回滚副本
└─ .upgrade\                      # 升级暂存，成功或失败后清理

D:\YeMan\CustomSteamLibrary\data\
├─ config\                         # 用户配置
├─ games\                          # 游戏元数据
├─ artwork\                        # 封面和壁纸
├─ cache\                          # 网络、任务和 WebView 缓存
├─ state\                          # 扫描、识别、恢复状态
├─ backups\                        # 用户数据快照和删除恢复包
└─ data-root.json                   # 数据根身份和布局版本
```

程序包不能携带一份会被自动覆盖的 `data`。如果 D 盘不可用，宿主才使用程序目录下的 `data` 作为便携回退；如果 D 盘和回退目录同时存在，D 盘优先，但不做无提示的双向合并。

## 3. 文件所有权

### 3.1 升级器可以覆盖的路径

只有 `package-manifest.json` 的 `managedPaths` 允许覆盖：

- `CustomSteamLibrary.exe`
- `SteamArtworkLab.exe`
- `workspace-ui\index.html`
- `workspace-ui\app.js`
- `workspace-ui\styles.css`
- `assets\custom-steam-library.ico`
- `run-workspace.bat`
- 发布契约和任务文档

更新宿主、Worker、UI 时必须作为一组提交，不能只更新其中一个，否则会出现协议版本和界面版本不匹配。

### 3.2 必须保留的路径

以下路径禁止被程序包覆盖、清空或删除：

- `D:\YeMan\CustomSteamLibrary\data\config`
- `D:\YeMan\CustomSteamLibrary\data\games`
- `D:\YeMan\CustomSteamLibrary\data\artwork`
- `D:\YeMan\CustomSteamLibrary\data\cache`
- `D:\YeMan\CustomSteamLibrary\data\state`
- `D:\YeMan\CustomSteamLibrary\data\backups`
- 回退场景下的 `<appRoot>\data`
- 程序目录中未被清单声明的用户文件

`data\cache\webview` 可以保留。若 UI 版本变化导致缓存不兼容，只能按缓存键失效或清理对应缓存子目录，不能连带删除配置、游戏信息和图片。

### 3.3 两种备份不能混用

- `appRoot\backups\upgrades`：只存程序文件回滚副本，由升级器管理。
- `data\backups`：只存用户配置、游戏、素材和状态快照，由 Custom Steam Library Worker 管理。

升级失败时先回滚程序文件；数据快照只在数据迁移或用户明确恢复时使用。升级器不得把整个 D 盘数据目录当成程序回滚目录。

## 4. 与 YeManCC 当前升级器的适配边界

当前 YeManCC 主程序升级器在安装前读取 `YeManCC\update-manifest.json`，要求更新包的根目录与清单完全一致。当前发布包为：

```text
YeManCC\
PowerControl\
CustomSteamLibrary\
```

随后生成专用 PowerShell 安装脚本，按清单执行覆盖、停止、回滚和提交。因此当前状态下：

- `CustomSteamLibrary` 必须与 `YeManCC`、`PowerControl` 同处 ZIP 根目录；
- 子程序 `package-manifest.json` 由升级器校验身份、入口、Worker、受管文件和 SHA-256 索引；
- 未声明的子程序未知文件和数据目录保留，不参与覆盖；
- 覆盖完成后，升级器以一次性 token 启动 `CustomSteamLibrary.exe --update-health-only`；
- 子程序必须在 WebView2 导航成功、Worker/UI 存在、数据根可写后原子写入独立健康 marker；
- Custom 健康握手和 YeManCC 主程序握手均成功后，才写入 `phase=committed`；任一失败都回滚整笔事务。

旧的两目录包仍按 legacy 分支处理，不触发 Custom 健康握手；新三目录包缺少 Custom 清单或健康握手失败时不能伪装成升级完成。

## 5. 当前升级流程

当前安装流程是：

1. YeManCC 下载子包并校验整个 ZIP SHA-256。
2. 校验子包根、包类型、版本、所有文件哈希和路径不能逃逸目标目录。
3. 请求 Custom Steam Library 关闭，等待目标目录内的 `CustomSteamLibrary.exe`、`SteamArtworkLab.exe` 退出；宿主通过正常 `WM_DESTROY` 关闭 WebView2，升级器不按进程名强杀其他 WebView2 实例。
4. 在 `CustomSteamLibrary\.upgrade\staging\<version>` 解压，不直接在工作目录覆盖。
5. 把受管程序文件备份到 `backups\upgrades\<timestamp>`。
6. 原子替换受管文件；不触碰两个数据根、不清空未知文件。
7. 以一次性 token 启动隐藏健康模式，检查入口进程路径、包身份、Worker、UI、WebView2、protocol 1、数据根和 marker PID。
8. 健康 marker 成功后关闭探测进程，再启动新版本 YeManCC 做主程序握手。
9. 两个握手都成功才提交；任一失败则恢复程序文件、PawnIO 和其他声明根，并保留失败原因。

健康 marker 至少包含 `phase`、`token`、`pid`、`packageId`、`packageVersion`、`protocol`、`worker`、`workspaceUi`、`webview2`、`dataRootWritable` 和 `dataRoot`。Marker 使用临时文件原子替换，升级器不会接受错误 token、错误 PID 或非目标目录进程。

## 6. 数据根冲突处理

| 情况 | 处理 |
| --- | --- |
| D 盘存在且 `D:\YeMan\CustomSteamLibrary\data` 有身份标记 | 使用 D 盘，程序包升级不动它 |
| D 盘存在，只有程序目录 `data` 有数据 | 不自动合并；先备份并生成迁移报告，用户确认后再迁移 |
| D 盘不存在，程序目录 `data` 存在 | 使用程序目录回退数据，升级仍不能删除它 |
| 两边都有数据但身份/布局不一致 | D 盘优先，阻止自动覆盖，报告冲突 |
| 只存在旧 `D:\YeMan\Steam大屏` | 不导入；允许用户重新真实刮削，旧路径仅写入诊断报告 |
| D 盘整个目录丢失 | 从本地回退配置恢复；图片缓存缺失时重新抓取，不伪造旧缓存 |

## 7. 验收门槛

在主程序接入前，子程序包必须通过：

- 包清单版本、包身份和所有受管文件 SHA-256 校验；
- C 盘程序移动后仍能解析 D 盘数据；
- D 盘数据存在时程序升级前后文件清单完全一致；
- 删除程序目录后，D 盘配置、图片和游戏信息仍可恢复；
- D 盘不可用时回退到程序目录 `data`，且升级不清空回退数据；
- 升级中断、宿主未退出、启动握手失败时只回滚程序文件；
- CustomSteamLibrary 独立健康握手失败时整笔事务回滚，不能只提交 YeManCC；
- `SteamArtworkLab.exe` 直接启动仍只作为 Worker，不产生用户主界面；
- 健康探测使用 `inputOwner=host` 的独立模式，不启动用户手柄轮询或网络任务；
- YeManCC 当前升级器不会误把子包当作主程序包安装。

## 8. 当前结论

当前可交付的是“三目录升级包 + 清单约束 + 数据保护 + 独立子程序健康握手 + 安装回滚适配 + 模拟回归”。正式安装目录升级和实体手柄窗口级验收仍需单独执行，不能用临时包回归替代。
