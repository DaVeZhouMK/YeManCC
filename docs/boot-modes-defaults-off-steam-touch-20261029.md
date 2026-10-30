# 四模式虚拟手柄默认关闭，Steam 模式使用独立触摸专用键

2026-10-29 用户要求：四个开机模式的虚拟手柄默认均关闭；模式 3（Windows + Steam 大屏）和模式 4（Xbox 全屏 + Steam 大屏）的触摸专用按键默认选择“开启Steam全部”。这是默认预设调整，已保存的自定义仍优先。

## 实现

`createBootModeDocument` 新建预设时将全局 input.persona 与 outputTarget.persona 设为 disabled，buttonMappingEnabled 设为 false；原运行配置作为 activeSnapshot 保留，不因浏览或选择立即改变。原运行鼠标为 SteamDeck 路由时，新的关闭虚拟手柄默认不继续引用该路由。其他普通鼠标设置保留。

Xbox、Steam 和组合模式的默认 input 均跟随全局，不另存一个固定关闭的场景覆盖。因此用户以后修改全局或明确在某个模式选择独立手柄设置仍然有效。

Steam 和组合模式的触摸默认只配置 disabled profile：以原无虚拟手柄触摸默认为基础，masterEnabled=true，standaloneSpecialMode=steamdeck，对应页面“开启Steam全部”。触摸板布局保持 off，specialMask=0，specialEnabled=false；不以 SteamDeck profile 的特有按键掩码启用虚拟手柄。

没有修改普通触摸板各手柄档位的出厂值、独立触摸专用按键的原生实现或 Steam／Xbox 启动准备。

## 升级

controllerDefaultsVersion 从 1 升到 2。仅对无版本／旧版本中的完整标准旧推荐模块做一次迁移：删除精确匹配旧 SD 推荐的 input 覆盖，回到共享全局；模式 3、4 中精确匹配旧 SD 双触摸板推荐的 touch 模块改为无虚拟手柄档位的 Steam 独立专用键。

已保存的全局、自定义完整模块、修改过的触摸参数和版本 1 明确选择的跟随全局不会被批量重置。版本 2 后用户主动选择 SD 也不会在下次读取时被改回关闭。由于旧版本没有区分“自动标准推荐”和“用户原样选择了标准推荐”的标记，旧版本中内容完全等于标准推荐的模块按旧出厂值迁移；修改过的模块保留。

## 验证范围

复用 settings.boot-modes-runtime、settings.boot-modes-file-ui、settings.boot-modes-browser 原登记及预算。覆盖四模式默认 OFF、四种当前运行手柄下创建新默认而不修改原状态、旧版本标准默认迁移、自定义／跟随保留、首次选择无需另存、实际落盘与模拟新 Windows 启动、720P／800P 真实组件及正式手柄引擎。原有显式 SD → 模式 1 OFF 隔离用例仍执行，改为明确设置 SD，避免用已退役的默认值代替用例前置条件。

证据：`G:/YeManCC-Work/Testing/YMCC/Evidence/BootMode-DefaultsOff-SteamTouch-20261029/RESULT.json`。浏览器回放及隔离模拟启动不等于真实 Windows 重启或实物手柄验收；本轮未打包、未部署、未修改现场用户配置。

文件夹具两次触及原 60 秒时限，失败证据保留。移除同一轮中逐场景重复的相同运行时编译：仅在显式共享输出目录时复用编译产物，每次核验完整编译输入与产物 SHA256，场景执行仍用新的 ESM 实例和实际文件。没有复用测试通过结果、减少断言或放宽预算。
