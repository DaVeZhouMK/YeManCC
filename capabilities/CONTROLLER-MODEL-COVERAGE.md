# 虚拟手柄全机型消费覆盖表

本页已按 **2026-10-25 23:25 新导出**重新核对：102 行及18条共享OEM关系未改变，JSON中的来源快照与锚点已刷新；最新三机支持和FanHost／虚拟InputHost身份区分见[修复支持说明](CONTROLLER-REPAIR-SUPPORT.md)。

2026-10-25 来源检查快照。机器可读：[CONTROLLER-MODEL-COVERAGE.json](CONTROLLER-MODEL-COVERAGE.json)；任务入口：[CONTROLLER-REPAIR-SUPPORT.md](CONTROLLER-REPAIR-SUPPORT.md)。按 JSON 的 `models[].name/deviceClass` 读取具体行；不必每次加载整个历史。

本表包含 **92 个冻结源码类（其中 80 个 factory 类）和 10 个已审查扩展类，共 102 条源码记录**。包含基类、通用类和桌面扩展，不能称为 102 款已支持掌机。`factoryInstantiable` 来自冻结源码，不证明当前 DLL、机型识别或设备存在。

表内 OEM/profile 来自当前 `native/main.cpp` 实际选择器语句的静态检查，使用每行明确列出的 family 作为条件；没有运行产品消费者、夹具或硬件。`nativeResolverMentionsClass` 仅为解析函数中的文本出现，不证明某组 DMI 能到达它。完整源码哈希及四个选择器锚点保存在 JSON；这些是检查时快照，执行方修改后需要重新核对。

## 共同消费边界

- 标准输入通过 `HcControllerInputCapabilityCatalog.h::bindStandardInput` 绑定实际 `xinput` / `xinput-any-connected` / `msi-dinput`。XInput 要当前连接 mask；MSI DInput 要现有 MSI 会话。DMI 和下表都不能给外接手柄授予 OEM 能力。
- OEM 通过 `HcOemInputCapabilityBinding.h::InputBindingCache` 调用原选择器。总库未命中的审查扩展继续传原 class，不能添加 `model != nullptr` 总门。表有键不代表已取得 hook/raw/嵌入式 reader、代次或按键释放所有权。
- 体感列只表示配方表登记。Windows 传感器、控制器传感器、选择、采样新鲜度、校准与最终提交分别取证。Legion controller adapter 的 `implemented=false` 不能被解释为 MSI/GPD/ROG 的 Windows 体感均不可用。
- 物理反馈通过 `HcControllerFeedbackCapabilityCatalog.h` 按当前实体 provider 选 XInput/MSI writer；游戏马达还需目标解码、实际 selector/receipt 和当前软件准入，不能按整机型号宣布完成。
- MSI 四型的 DInput 背键准备复用 `HcControllerConfigurationCapabilityCatalog.h` 和真实固件/端点准入。XInputKeyboard 为 record-only，不能因此启用新的配置写入。
- 前台隔离列的 msi/rog/gpd 只表示候选家族，仍需 `nativeForeground::machineProfile(manufacturer, board)`、VID/PID、同根容器、接口与排除虚拟源。其他家族 unsupported 仅指该专属隔离策略，不阻断原通用虚拟输出。ROG 1B4C HID 刷新变更仍隔离。
- 所有行硬件验收均未由本次支持签署。源行数、拥有目录的行数、配方行数均不能作为虚拟手柄修复百分比。

## 共享消费类与 CPU 子型

另核对了 **18 条冻结 factory 子型的原生身份合并关系**。JSON 的 `nativeIdentityProjection` 同时列出原 HC factory、当前 native 选择器、板号条件和共享消费类。它表示现有 OEM 消费关系，不是总库的新全局 alias，也不是硬件接入完成证明。

| 冻结子型组 | 行数 | 当前原生 OEM 消费类 | 保留的区别 |
|---|---:|---|---|
| GPDWin4 的 2023/2024 CPU 子型 | 5 | GPDWin4 | 板号 G1618-04；CPU 仍决定运动矩阵；部分源码类直接继承 IDevice，不能以名称或继承链替代 factory 核对 |
| GPDWinMax2 的 AMD CPU 子型 | 6 | GPDWinMax2 | G1619-04/05；HX370 矩阵独立；G1619-03 的 Intel 类保持独立 |
| GPDWinMini 的 CPU 子型 | 3 | GPDWinMini | G1617-01/02 与 CPU 共同决定矩阵 |
| AYANEO2021Pro | 1 | AYANEO2021 | native 中 2021/Pro 字符串共用已有键消费 |
| AYANEONEXTLite4500U | 1 | AYANEONEXTLite | NEXT LITE 原机型门；不得借此覆盖 NEXT2 |
| LokiMax6600U / LokiMax6800U | 2 | AynLoki | 现有 LOKI MAX 路由；不把非 factory 基类改成新 factory 型号 |

这 18 项在当前静态选择器中选到相同 OEM profile/keyTable，因此“完整 CPU 子类名未直接出现在 resolver”不等于 OEM 漏接。原冻结身份、Fan dispatch、CPU/板号运动配方仍分别保留；标准虚拟输入仍看实际 provider。`inputHidKey` 是元数据提示，不能解析它来推断实体 VID/PID、协议就绪或隔离许可。

## 家族索引

| 家族 | 冻结类 | 审查扩展 | 行数 |
|---|---:|---:|---:|
| AOKZOE | 4 | 0 | 4 |
| ASUS | 5 | 0 | 5 |
| AYANEO | 24 | 6 | 30 |
| Ayn | 5 | 0 | 5 |
| MSI | 4 | 0 | 4 |
| Generic | 3 | 0 | 3 |
| GPD | 21 | 0 | 21 |
| Zotac | 1 | 0 | 1 |
| Lenovo | 5 | 0 | 5 |
| Minisforum | 1 | 0 | 1 |
| OneXPlayer | 17 | 3 | 20 |
| Valve | 1 | 1 | 2 |
| SuiPlay | 1 | 0 | 1 |

## 逐行入口

F=冻结 factory；B=冻结非 factory/基类；E=审查扩展（不在旧冻结表）。OEM 为 `profile / keyTable`，省略统一前缀；“无”只指该选择器没有 OEM 表。体感列为配方登记种类；Legion 协议与 MSI 背键准备另列。各项实际消费和边界仍以 JSON/原消费者为准。

| 机型类 | 来源 | OEM profile / table | 配方登记 | 控制器体感 | MSI背键准备 | 前台隔离候选 |
|---|---|---|---|---|---|---|
| AOKZOEA1 | F | Aokzoe / kAokzoeKeys | Legacy | — | — | unsupported |
| AOKZOEA1Pro | F | Aokzoe / kAokzoeKeys | Legacy | — | — | unsupported |
| AOKZOEA1X | F | Aokzoe / kAokzoeKeys | Legacy | — | — | unsupported |
| AOKZOEA2 | F | Aokzoe / kAokzoeKeys | Legacy | — | — | unsupported |
| ASUS | F | None / 无 | — | — | — | rog |
| AYANEO2 | F | AyaneoCec / kAyaneoCecKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEO2021 | F | Ayaneo2021 / kAyaneo2021Keys | Legacy | — | — | unsupported |
| AYANEO2021Pro | F | Ayaneo2021 / kAyaneo2021Keys | — | — | — | unsupported |
| AYANEO2S | F | AyaneoCec / kAyaneoCecKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEOAIR | F | AyaneoAir / kAyaneoCecKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEOAIR1S | F | AyaneoCec / kAyaneoCecKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEOAIRLite | F | AyaneoAir / kAyaneoCecKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEOAIRPlus | B | AyaneoCec / kAyaneoCecKeys | — | — | — | unsupported |
| AYANEOAIRPlusAMD | F | AyaneoCec / kAyaneoCecKeys | Legacy | — | — | unsupported |
| AYANEOAIRPlusAMDMendocino | F | AyaneoCec / kAyaneoCecKeys | Legacy | — | — | unsupported |
| AYANEOAIRPlusIntel | F | AyaneoCec / kAyaneoCecKeys | Legacy | — | — | unsupported |
| AYANEOAIRPro | F | AyaneoAir / kAyaneoCecKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEODevice | B | AyaneoCec / kAyaneoCecKeys | — | — | — | unsupported |
| AYANEODeviceCEc | B | AyaneoCec / kAyaneoCecKeys | — | — | — | unsupported |
| AYANEODeviceCEii | B | AyaneoCec / kAyaneoCecKeys | — | — | — | unsupported |
| AYANEOFlip1SDS | F | AyaneoFlip1S / kAyaneoFlip1SdsKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEOFlip1SKB | F | AyaneoFlip1SKb / kAyaneoFlip1SKbKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEOFlipDS | F | AyaneoFlipDs / kAyaneoFlipDsKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEOFlipKB | F | AyaneoCec / kAyaneoCecKeys | Reviewed, Legacy | — | — | unsupported |
| AYANEOKUN | F | AyaneoKun / kAyaneoKunKeys | Legacy | — | — | unsupported |
| AYANEONEXT | F | AyaneoNext / kAyaneoNextKeys | Legacy | — | — | unsupported |
| AYANEONEXTLite | F | AyaneoNext / kAyaneoNextKeys | Legacy | — | — | unsupported |
| AYANEONEXTLite4500U | F | AyaneoNext / kAyaneoNextKeys | — | — | — | unsupported |
| AYANEOSlide | F | AyaneoCec / kAyaneoCecKeys | Reviewed, Legacy | — | — | unsupported |
| AynLoki | B | Ayn / kAynKeys | Legacy | — | — | unsupported |
| ClawA1M | F | Msi / kMsiClawKeys | Private | — | DInput | msi |
| ClawA2VM | F | Msi / kMsiClawKeys | Private | — | DInput | msi |
| ClawBZ2EM | F | Msi / kMsiClawKeys | Private | — | DInput | msi |
| ClawCG3EM | F | Msi / kMsiClawG3EKeys | Private | — | DInput | msi |
| DefaultDevice | F | None / 无 | Generic | — | — | unsupported |
| GPDWin2 | F | None / 无 | Default | — | — | gpd |
| GPDWin3 | F | GpdLegacy / kGpdLegacyKeys | Default | — | — | gpd |
| GPDWin4 | F | GpdLegacy / kGpdLegacyKeys | Legacy, Legacy | — | — | gpd |
| GPDWin4_2023 | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWin4_2023_7640U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWin4_2024 | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWin4_2024_8640U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWin4_2024_HX370 | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWin5 | F | GpdWin5 / kGpdWin5Keys | Legacy | — | — | gpd |
| GPDWinMax2 | B | GpdLegacy / kGpdLegacyKeys | Legacy, Legacy | — | — | gpd |
| GPDWinMax2Intel | F | GpdLegacy / kGpdLegacyKeys | Legacy | — | — | gpd |
| GPDWinMax2_2022_6800U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWinMax2_2023_7640U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWinMax2_2023_7840U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWinMax2_2024_8640U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWinMax2_2024_8840U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWinMax2_2024_HX370 | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWinMini | F | GpdLegacy / kGpdLegacyKeys | Legacy, Legacy, Legacy | — | — | gpd |
| GPDWinMini_7640U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWinMini_8840U | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GPDWinMini_HX370 | F | GpdLegacy / kGpdLegacyKeys | — | — | — | gpd |
| GamingZone | F | Zotac / kZotacKeys | Legacy | — | — | unsupported |
| IDevice | B | None / 无 | — | — | — | unsupported |
| LegionGo | B | None / kLegionGoKeys | — | — | — | unsupported |
| LegionGoSZ1 | F | None / kLegionSKeys | Legacy | GoS | — | unsupported |
| LegionGoSZ2 | F | None / kLegionSKeys | Legacy | GoS | — | unsupported |
| LegionGoTablet | F | None / kLegionGoKeys | Legacy | Tablet | — | unsupported |
| LegionGoTablet2 | F | None / kLegionGoKeys | Legacy | Tablet | — | unsupported |
| LokiMax6600U | F | Ayn / kAynKeys | — | — | — | unsupported |
| LokiMax6800U | F | Ayn / kAynKeys | — | — | — | unsupported |
| LokiMiniPro | F | Ayn / kAynKeys | Legacy | — | — | unsupported |
| LokiZero | F | Ayn / kAynKeys | Legacy | — | — | unsupported |
| MinisforumV3 | F | None / 无 | Reviewed, Legacy | — | — | unsupported |
| OneXAOKZOE | B | None / 无 | — | — | — | unsupported |
| OneXPlayer2 | F | OneX2 / kOneX2Keys | Legacy | — | — | unsupported |
| OneXPlayer2Pro | F | OneX2 / kOneX2Keys | Legacy | — | — | unsupported |
| OneXPlayerApex | F | OneXApex / kOneXApexKeys | Legacy | — | — | unsupported |
| OneXPlayerG1 | B | None / 无 | — | — | — | unsupported |
| OneXPlayerG1AMD | F | None / 无 | Reviewed, Legacy | — | — | unsupported |
| OneXPlayerG1Intel | F | None / 无 | Reviewed, Legacy | — | — | unsupported |
| OneXPlayerMini | B | OneXMini / kOneXMiniKeys | — | — | — | unsupported |
| OneXPlayerMiniAMD | F | OneXMini / kOneXMiniKeys | Reviewed, Legacy | — | — | unsupported |
| OneXPlayerMiniIntel | F | OneXMini / kOneXMiniKeys | Reviewed, Legacy | — | — | unsupported |
| OneXPlayerMiniPro | F | OneXMini / kOneXMiniKeys | Reviewed, Legacy | — | — | unsupported |
| OneXPlayerOneXFly | F | OneXFly / kOneXFlyKeys | Legacy | — | — | unsupported |
| OneXPlayerOneXFlyF1Pro | F | OneXFly / kOneXFlyKeys | Legacy | — | — | unsupported |
| OneXPlayerX1 | B | OneXX1 / kOneXKeys | — | — | — | unsupported |
| OneXPlayerX1AMD | F | OneXX1 / kOneXKeys | Legacy | — | — | unsupported |
| OneXPlayerX1Intel | F | OneXX1 / kOneXKeys | Legacy | — | — | unsupported |
| OneXPlayerX1Mini | F | OneXX1 / kOneXKeys | Reviewed, Legacy | — | — | unsupported |
| OneXPlayerX1Pro | F | OneXX1 / kOneXKeys | Legacy | — | — | unsupported |
| ROGAlly | F | Rog / kRogKeys | Legacy | — | — | rog |
| ROGAllyX | F | Rog / kRogKeys | Legacy | — | — | rog |
| SteamDeck | F | None / kSteamDeckKeys | — | — | — | unsupported |
| SuiPlay0X1 | F | SuiPlay / kSuiPlayKeys | Legacy | — | — | unsupported |
| XboxROGAlly | F | Rog / kRogXbox2025Keys | Private | — | — | rog |
| XboxROGAllyX | F | Rog / kRogXbox2025Keys | Private | — | — | rog |
| AYANEO3 | E | Ayaneo3 / kAyaneoNext2Keys | Reviewed | — | — | unsupported |
| AYANEOAIR2 | E | AyaneoCec / kAyaneoCecKeys | Reviewed | — | — | unsupported |
| AYANEOAIRPlusNxx | E | AyaneoCec / kAyaneoCecKeys | Reviewed | — | — | unsupported |
| AYANEOAIRPro1S | E | AyaneoCec / kAyaneoCecKeys | Reviewed | — | — | unsupported |
| AYANEOFlip11 | E | AyaneoFlip1SKb / kAyaneoFlip1SKbKeys | Reviewed | — | — | unsupported |
| AYANEONEXT2 | E | AyaneoNext2 / kAyaneoNext2Keys | Reviewed | — | — | unsupported |
| OneXPlayer3 | E | OneXX1 / kOneXX2Keys | Reviewed | — | — | unsupported |
| OneXPlayerX2 | E | OneXX1 / kOneXX2Keys | Reviewed | — | — | unsupported |
| OneXPlayerX2MiniPro | E | OneXX2MiniPro / kOneXX2MiniProKeys | Reviewed | — | — | unsupported |
| SteamMachine | E | None / 无 | Default | — | — | unsupported |

## 三机日志轮次对照

### 首轮：未重启

首轮直接读取用户给出的 2026-10-25 21:51–21:54 三包。用户说明未重启，不能把历史进程/事务状态签成当前源码的全新验收。可重查原 ZIP 的 SHA256、成员与行号见 `Testing/YMCC/Evidence/CapabilitySupport-VirtualController-20261025/whole-library/THREE-MACHINE-LOG-SUPPORT.json`。

| 机型 | 本地 switch 最终回执 | 隔离/归还区别 | 当前支持结论 |
|---|---|---|---|
| ClawCG3EM | 前四次 steamdeck、dualsense-edge、elite、disabled 为 success；21:50:19.839 最后 steamdeck 为 failure，`original-admission-deadline-expired` | native 1901 与虚拟 MSI DInput 1902 属于不同工作阶段，不能仅 PID 不同就改库 | 交修复方核对未结束 ownership/handoff 与准入期限；不改已有 M1/M2 侧别 |
| GPDWin5 | 五次为 `local-switch-verification` success | 五条 refresh-failed，helper exit 6，自报 `Stage=disable/DisableResult=23/EnableCallEntered=false`；末 handoff 明写 `external-input-unverified` | 可确认刷新失败记录；本地成功不能证明实体屏蔽，无依据时不得扩大成所有 GPD 都失败 |
| XboxROGAllyX | 五次为本地 success，OFF 含 `disabled-cleanup-and-rog-xinput-observed` | `rog-hid-game-node-refresh-unvalidated` deferred | 保留 1B4C HID 刷新隔离；本地 XInput 观察不等于外部游戏验收 |

三机 `physicalReturnVerified=false/gameInputVerified=false` 的 final 仍需按 scope 阅读。Windows metadata 观察：G3E 上次 gyro/accel 枚举为 0，导出时 context 已不稳定；Win5 上次各 1 个但导出 context 已不稳定；ROG 上次各 1 个且 contextStableAtCheck=true。这些都是元数据观察，不能证明读得出新样本或游戏已消费。

### 第二轮：用户报告重启后，新增内容单独比对

新包（22:09–22:11 导出）保留首轮历史。按原始日志行排除首包已有内容，再读取新行；没有把累计事件数重复算作本轮。来源见同证据目录 `THREE-MACHINE-REBOOT-LOG-SUPPORT.json`、`REBOOT-ROUND-COMPARISON.json`。

| 机型 / 新进程 | 新增观察 | 总库支持判断 |
|---|---|---|
| G3E / PID 10600 | 22:05:18.883 OFF 为 `disabled-local-cleanup-unproven`，电源 ready；22:08:23.507 最后 Steam Deck 超期，电源已 suspended，inspection 为 expected-pause | 两项分开交修复方；不能只因超期就判机型/绑定缺失。22:07:01.427 实际总库背键准备已取得写入回执，详见下文 |
| Win5 / PID 9404 | 新增三条 helper exit 6、self-reported disable result 23；仍有 `external-input-unverified` / `physical-refresh-unverified-terminal-helper` | 保留物理刷新未证实状态；本地切换成功不能补成屏蔽验收 |
| ROG / PID 11272 | 新增六个本地 final success、三条 `rog-hid-game-node-refresh-unvalidated` deferred | 保留既有 ROG 刷新隔离；不套用 Win5 设备操作 |

G3E 的实际消费：第二包 `native-lifecycle.log:2248/2249` 的 `capabilityId=controller.configuration.msi.rear.dinput-buttons`、`modelId=hc.device.ClawCG3EM`、`firmware=1041`，目录哈希与当前生成表 `rearCatalogSha256` 相同。M1/M2/sync 各 64 字节 API 回执成功；`mappingVerified=false`，readback 未尝试。此前 line2051 为 `command-interface-not-found`、`lastOpenError=5`、firmware=0，不能把发现失败时的 0 当新固件并放宽库准入。完整回执与哈希在 `MSI-LIBRARY-CONSUMPTION-RECEIPTS.json`。

## 持续协作状态

本侧维护 class/alias/模型差异、现有绑定入口和缺口。虚拟手柄修复方维护 native/Host/驱动/归还实现；同一执行文件保持单写者。已把三机差异、102 行覆盖、OneX/AYA fallback 和 ROG 隔离边界交给“虚拟手柄”任务。当前联合目标仍 ACTIVE，不因本表交付而宣称修复完成；需要当前源码软件验证、包身份和分别标记的物理验收。

已读取修复方 R5 `COORDINATOR-DELIVERY.json` 并独立核对其来源及回执哈希（具体匹配项见证据目录 `R5-DELIVERY-REFERENCE.json`）。该交付仅为 GPD native helper writer 跟踪的软件范围；它明确保留 first-hide ledger、native→virtual/OFF 强退归还、实际设备恢复及 OneX/AYA 物理协议缺口，且未导出。R5 不能被总库覆盖表升级成全家族验收。最新推进优先级沿用用户交给修复方的 ROG、微星优先，Win5 问题保留。
