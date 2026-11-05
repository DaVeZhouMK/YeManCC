# YMCC 全家族充电能力复核（2026-11-05）

逐类解析继承链、充电声明、最终固件判断方法和 YMCC 精确身份规则。候选仍需运行时验证，不等于硬件支持或实测通过。本文件在唯一 capabilities 总库内，只记录审计，不提供另一套运行注册。完整方法、身份、行号、哈希、条件和档位见同名 JSON。

当前运行规则 42 组，协议 12 条；联网 HC 提交 db93cadd55cb9f199beb1cb1dff016bc0150048e，完整设备源码 104 文件。

## OneX 优先结论

| 型号 | 当前充电路径 / 档位 | 来源与剩余限制 |
| --- | --- | --- |
| OneXPlayer2 | 未登记 | HC 无充电实现 |
| OneXPlayer2Pro | 未登记 | HC 无充电实现 |
| OneXPlayer3 | oxp3-wmi-ec，50/55/60/65/70/75/80/85/90/95/100% | YMCC reviewed newer HC WMI banked 0x04A3 override is independent of frozen false gate; 0x04A4 modes 0x00/0x01/0x0B are not product-exposed |
| OneXPlayerApex | oxp-x1-amd-ec，50/55/60/65/70/75/80/85/90/95/100% | HC/current YMCC inherits X1 direct EC16 0x04A3 and component gate; signed OEM 0.10.4-fix3 exact APEX JavaScript profile initializes 0x04E5/0x04E6/0x04E7. Native Windows access/transaction not recovered; Linux ACPI8 0xE5 is separate; field rejected before provider, no register/gate change |
| OneXPlayerG1AMD | 未登记 | HC 无充电实现 |
| OneXPlayerG1Intel | 未登记 | HC 无充电实现 |
| OneXPlayerMiniAMD | 未登记 | HC 无充电实现 |
| OneXPlayerMiniIntel | 未登记 | HC 无充电实现 |
| OneXPlayerMiniPro | 未登记 | HC 无充电实现 |
| OneXPlayerOneXFly | 未登记 | HC 无充电实现 |
| OneXPlayerOneXFlyF1Pro | 未登记 | HC 无充电实现 |
| OneXPlayerX1AMD | oxp-x1-amd-ec，50/55/60/65/70/75/80/85/90/95/100% | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Intel | oxp-x1-intel-ec，50/55/60/65/70/75/80/85/90/95/100% | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Mini | oxp-x1-amd-ec，50/55/60/65/70/75/80/85/90/95/100% | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Pro | oxp-x1-amd-ec，50/55/60/65/70/75/80/85/90/95/100% | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX2 | 未登记 | 上游明确 false，不继承启用 |
| OneXPlayerX2MiniPro | 未登记 | 上游明确 false，不继承启用 |
| OneXPlayerSuperX | oxp-superx-dual-ec，75–100% | 独立已审查来源；沿原双传输消费者，实机尚未证明 |

## APEX Windows 原厂配置补证

已保全 OneXConsole 0.10.4-fix3 签名安装包及包内 JavaScript；原记录签名为 Valid，全部 13 项引用的大小/SHA 已核。精确 APEX 配置将 0x04E5/0x04E6/0x04E7 送入 BatteryService.initEc，与 YMCC 当前默认 0x04A3 不同。

该证据只证明原厂配置及来源身份。实际 native EC/Battery/PawnIO 方法仅显示异常占位，未恢复访问宽度、地址编码或整事务合同；不是硬件通过，也不放开 EC0.12 门、不替换寄存器、不调用 OEM 接口。

原生限制记录：G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\observations\HC-CODEX-080-ONEX-REQUIREMENTS-CONSUMPTION-AUDIT\OEM-NATIVE-RECOVERY-LIMIT.json；待修正项：G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\observations\HC-CODEX-080-ONEX-REQUIREMENTS-CONSUMPTION-AUDIT\APEX-PROFILE-CORRECTION.json。

## 可读的原厂辅助原生合同

同一签名安装包另附 DeviceComm.dll；其真实 IL 已恢复 Windows root\WMI / SuRwECRegInterface 的 ReadECReg、WriteECReg 地址打包和八字节回执规范。它也明确了 RXU 1A86:FE00 的 MI_02 与 B3 版本查询帧；这些是辅助工具合同，不是固件操作或新能力准入。

现有 Intel OXP3 WMI 读取器的逗号解析与原厂允许的连续十六进制回执有源码兼容差异，原案例的独立候选验证另行保全。APEX 充电后端是否走该辅助 WMI 合同仍未证明；主后端 EC/Battery/PawnIO 方法及该机固件准入未恢复，不替换地址、不借用 OXP3 WMI、不改变运行路线。

辅助合同：G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\observations\HC-CODEX-084-ONEX-NATIVE-SOURCE-RECOVERY\WINDOWS-AUXILIARY-CONTRACT-REVIEW.json。


YMCC 当前继承的 OneX X1 AMD/Mini/Pro/Apex 门：EC major>=1 且 minor>=3；Intel：major>=0 且 minor>=67，拒绝未知255。APEX 当前沿用此门不等于其新原厂协议已核验；条件来自HC原式，不擅自改成版本号字典序。

OneXPlayer 3 的独立 WMI 协议已适配，当前已不是 10 月 16 日的“HC 禁用”；其 0x04A4 旁路模式与 X1 不同。Super X 的 75–100% 双传输路线是已有源码工作，本轮核对并补齐总库状态，不重复算新增。

## 本次现场

压缩包命名 YeManCC-logs-20261105-093203-OP3-震动失败-专用按键失败-虚拟手柄失败-充电失败.zip；实际 DMI：ONE-NETBOOK / ONEXPLAYER APEX / ONEXPLAYER APEX。客户端 1 次结果，操作 {"get":1}；电池存在，但全部在固件条件前置检查失败，没有写入证据。

错误：当前 EC 或 BIOS 版本尚未通过充电接口校验。最新会话 2026-11-05T09:30:04.9811288+08:00，PID 10692，构建 Nov  5 2026 09:08:24；BIOS=0.12，EC=0.12。EC 主版本 0 不满足 >=1，次版本 12 满足 >=3；providerOpened=false，未进入 EC 读写。该版本不被现有 X1 规则接纳，不能因此推断 APEX 固件过旧或应该刷成 X1 版本。旧会话结果单独保存，不累加为新构建请求。

HC/APEX当前沿用X1 EC16 0x04A3；原厂Windows JavaScript的精确APEX配置0x04E5已证，Linux ACPI8 0xE5另记。实际Windows原生EC事务未恢复；当前现场仍是provider打开前的准入拒绝，未改门/寄存器，实机失败保持未修复。

## 全家族边界

- ROG / MSI：用户反馈基本可用，保留现有 ASUS 回执语义与 G3E 独立 AP 回读；本轮不扩大成全部型号实机通过。
- Lenovo：固定 80/100%；AYANEO：CEc 与 CEii 软件阈值 50–95%，100% 释放，不能称固件保存阈值。
- Steam Deck：精确板号/固件和驱动门，单字节阈值；GPD：仅 Mini 2025 HX370 精确 BIOS/EC，Win5 仍无 HC 充电协议。
- SuiPlay：继承源码仍未开放 Windows；X2/X2MiniPro 明确禁用；其他无声明机型不能因家族相近补寄存器。
- 本报告的已登记/候选均是源码层级；下方每个类列出来源和运行边界。

## YMCC 锁定 HC

工厂可识别 80 类（含 2 兜底）；充电声明 34 类，明确禁用 0 类，待运行时探测候选 34 类。

| 类 | HC 充电实现 | YMCC 充电规则 | 差异 / 边界 |
| --- | --- | --- | --- |
| AOKZOEA1 | 无充电声明/协议 | 未登记 |  |
| AOKZOEA1Pro | 无充电声明/协议 | 未登记 |  |
| AOKZOEA1X | 无充电声明/协议 | 未登记 |  |
| AOKZOEA2 | 无充电声明/协议 | 未登记 |  |
| ASUS | 无充电声明/协议 | 未登记 | 兜底类，不是机型 |
| AYANEO2 | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEO2021 | 无充电声明/协议 | 未登记 |  |
| AYANEO2021Pro | 无充电声明/协议 | 未登记 |  |
| AYANEO2S | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIR | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIR1S | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIRLite | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIRPlusAMD | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| AYANEOAIRPlusAMDMendocino | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| AYANEOAIRPlusIntel | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| AYANEOAIRPro | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlip1SDS | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlip1SKB | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlipDS | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlipKB | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOKUN | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEONEXT | 无充电声明/协议 | 未登记 |  |
| AYANEONEXTLite | 无充电声明/协议 | 未登记 |  |
| AYANEONEXTLite4500U | 无充电声明/协议 | 未登记 |  |
| AYANEOSlide | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| ClawA1M | msi-legacy-wmi | msi-claw-wmi |  |
| ClawA2VM | msi-legacy-wmi | msi-claw-wmi |  |
| ClawBZ2EM | msi-legacy-wmi | msi-claw-wmi |  |
| ClawCG3EM | msi-legacy-wmi | msi-claw-cg3em-ap | YMCC uses field-verified selector-0 Get_AP/Set_AP with independent readback; HC inherits legacy Get_Data/Set_Data |
| DefaultDevice | 无充电声明/协议 | 未登记 | 兜底类，不是机型 |
| GPDWin2 | 无充电声明/协议 | 未登记 |  |
| GPDWin3 | 无充电声明/协议 | 未登记 |  |
| GPDWin4 | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2023 | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2023_7640U | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2024 | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2024_8640U | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2024_HX370 | 无充电声明/协议 | 未登记 |  |
| GPDWin5 | 无充电声明/协议 | 未登记 | G1618-05 无协议；不能套 Mini 2025 |
| GPDWinMax2Intel | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2022_6800U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2023_7640U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2023_7840U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2024_8640U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2024_8840U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2024_HX370 | 无充电声明/协议 | 未登记 |  |
| GPDWinMini | 无充电声明/协议 | 未登记 |  |
| GPDWinMini_7640U | 无充电声明/协议 | 未登记 |  |
| GPDWinMini_8840U | 无充电声明/协议 | 未登记 |  |
| GPDWinMini_HX370 | 无充电声明/协议 | gpd-win-mini-2025 |  |
| GamingZone | 无充电声明/协议 | 未登记 |  |
| LegionGoSZ1 | lenovo-fixed-conservation | lenovo-go-conservation |  |
| LegionGoSZ2 | lenovo-fixed-conservation | lenovo-go-conservation |  |
| LegionGoTablet | lenovo-fixed-conservation | lenovo-go-conservation |  |
| LegionGoTablet2 | lenovo-fixed-conservation | lenovo-go-conservation |  |
| LokiMax6600U | 无充电声明/协议 | 未登记 |  |
| LokiMax6800U | 无充电声明/协议 | 未登记 |  |
| LokiMiniPro | 无充电声明/协议 | 未登记 |  |
| LokiZero | 无充电声明/协议 | 未登记 |  |
| MinisforumV3 | 无充电声明/协议 | 未登记 |  |
| OneXPlayer2 | 无充电声明/协议 | 未登记 |  |
| OneXPlayer2Pro | 无充电声明/协议 | 未登记 |  |
| OneXPlayerApex | onex-direct-ec-threshold-and-bypass | oxp-x1-amd-ec | HC/current YMCC inherits X1 direct EC16 0x04A3 and component gate; signed OEM 0.10.4-fix3 exact APEX JavaScript profile initializes 0x04E5/0x04E6/0x04E7. Native Windows access/transaction not recovered; Linux ACPI8 0xE5 is separate; field rejected before provider, no register/gate change |
| OneXPlayerG1AMD | 无充电声明/协议 | 未登记 |  |
| OneXPlayerG1Intel | 无充电声明/协议 | 未登记 |  |
| OneXPlayerMiniAMD | 无充电声明/协议 | 未登记 |  |
| OneXPlayerMiniIntel | 无充电声明/协议 | 未登记 |  |
| OneXPlayerMiniPro | 无充电声明/协议 | 未登记 |  |
| OneXPlayerOneXFly | 无充电声明/协议 | 未登记 |  |
| OneXPlayerOneXFlyF1Pro | 无充电声明/协议 | 未登记 |  |
| OneXPlayerX1AMD | onex-direct-ec-threshold-and-bypass | oxp-x1-amd-ec | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Intel | onex-direct-ec-threshold-and-bypass | oxp-x1-intel-ec | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Mini | onex-direct-ec-threshold-and-bypass | oxp-x1-amd-ec | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Pro | onex-direct-ec-threshold-and-bypass | oxp-x1-amd-ec | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| ROGAlly | asus-rsoc-ack-only | asus-ally-acpi |  |
| ROGAllyX | asus-rsoc-ack-only | asus-ally-acpi |  |
| SteamDeck | steamdeck-firmware-threshold | valve-deck-firmware | YMCC writes one threshold byte; HC setter builds an Int32 byte array |
| SuiPlay0X1 | ayaneo-cec-software-bypass | 未登记 | SuiPlay Windows 未开放 |
| XboxROGAlly | asus-rsoc-ack-only | asus-ally-acpi |  |
| XboxROGAllyX | asus-rsoc-ack-only | asus-ally-acpi |  |

## 联网固定的上游 HC

工厂可识别 90 类（含 2 兜底）；充电声明 43 类，明确禁用 2 类，待运行时探测候选 41 类。

| 类 | HC 充电实现 | YMCC 充电规则 | 差异 / 边界 |
| --- | --- | --- | --- |
| AOKZOEA1 | 无充电声明/协议 | 未登记 |  |
| AOKZOEA1Pro | 无充电声明/协议 | 未登记 |  |
| AOKZOEA1X | 无充电声明/协议 | 未登记 |  |
| AOKZOEA2 | 无充电声明/协议 | 未登记 |  |
| ASUS | 无充电声明/协议 | 未登记 | 兜底类，不是机型 |
| AYANEO2 | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEO2021 | 无充电声明/协议 | 未登记 |  |
| AYANEO2021Pro | 无充电声明/协议 | 未登记 |  |
| AYANEO2S | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEO3 | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIR | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIR1S | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIR2 | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIRLite | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIRPlusAMD | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| AYANEOAIRPlusAMDMendocino | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| AYANEOAIRPlusIntel | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| AYANEOAIRPlusNxx | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| AYANEOAIRPro | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOAIRPro1S | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlip11 | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlip1SDS | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlip1SKB | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlipDS | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOFlipKB | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEOKUN | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEONEXT | 无充电声明/协议 | 未登记 |  |
| AYANEONEXT2 | ayaneo-cec-software-bypass | ayaneo-cec-bypass |  |
| AYANEONEXTLite | 无充电声明/协议 | 未登记 |  |
| AYANEONEXTLite4500U | 无充电声明/协议 | 未登记 |  |
| AYANEOSlide | ayaneo-ceii-software-bypass | ayaneo-ceii-bypass |  |
| ClawA1M | msi-legacy-wmi | msi-claw-wmi |  |
| ClawA2VM | msi-legacy-wmi | msi-claw-wmi |  |
| ClawBZ2EM | msi-legacy-wmi | msi-claw-wmi |  |
| ClawCG3EM | msi-legacy-wmi | msi-claw-cg3em-ap | YMCC uses field-verified selector-0 Get_AP/Set_AP with independent readback; HC inherits legacy Get_Data/Set_Data |
| DefaultDevice | 无充电声明/协议 | 未登记 | 兜底类，不是机型 |
| GPDWin2 | 无充电声明/协议 | 未登记 |  |
| GPDWin3 | 无充电声明/协议 | 未登记 |  |
| GPDWin4 | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2023 | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2023_7640U | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2024 | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2024_8640U | 无充电声明/协议 | 未登记 |  |
| GPDWin4_2024_HX370 | 无充电声明/协议 | 未登记 |  |
| GPDWin5 | 无充电声明/协议 | 未登记 | G1618-05 无协议；不能套 Mini 2025 |
| GPDWinMax2Intel | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2022_6800U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2023_7640U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2023_7840U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2024_8640U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2024_8840U | 无充电声明/协议 | 未登记 |  |
| GPDWinMax2_2024_HX370 | 无充电声明/协议 | 未登记 |  |
| GPDWinMini | 无充电声明/协议 | 未登记 |  |
| GPDWinMini_7640U | 无充电声明/协议 | 未登记 |  |
| GPDWinMini_8840U | 无充电声明/协议 | 未登记 |  |
| GPDWinMini_HX370 | 无充电声明/协议 | gpd-win-mini-2025 |  |
| GamingZone | 无充电声明/协议 | 未登记 |  |
| LegionGoSZ1 | lenovo-fixed-conservation | lenovo-go-conservation |  |
| LegionGoSZ2 | lenovo-fixed-conservation | lenovo-go-conservation |  |
| LegionGoTablet | lenovo-fixed-conservation | lenovo-go-conservation |  |
| LegionGoTablet2 | lenovo-fixed-conservation | lenovo-go-conservation |  |
| LokiMax6600U | 无充电声明/协议 | 未登记 |  |
| LokiMax6800U | 无充电声明/协议 | 未登记 |  |
| LokiMiniPro | 无充电声明/协议 | 未登记 |  |
| LokiZero | 无充电声明/协议 | 未登记 |  |
| MinisforumV3 | 无充电声明/协议 | 未登记 |  |
| OneXPlayer2 | 无充电声明/协议 | 未登记 |  |
| OneXPlayer2Pro | 无充电声明/协议 | 未登记 |  |
| OneXPlayer3 | onex3-wmi-ec-threshold-and-bypass | oxp3-wmi-ec | YMCC reviewed newer HC WMI banked 0x04A3 override is independent of frozen false gate; 0x04A4 modes 0x00/0x01/0x0B are not product-exposed |
| OneXPlayerApex | onex-direct-ec-threshold-and-bypass | oxp-x1-amd-ec | HC/current YMCC inherits X1 direct EC16 0x04A3 and component gate; signed OEM 0.10.4-fix3 exact APEX JavaScript profile initializes 0x04E5/0x04E6/0x04E7. Native Windows access/transaction not recovered; Linux ACPI8 0xE5 is separate; field rejected before provider, no register/gate change |
| OneXPlayerG1AMD | 无充电声明/协议 | 未登记 |  |
| OneXPlayerG1Intel | 无充电声明/协议 | 未登记 |  |
| OneXPlayerMiniAMD | 无充电声明/协议 | 未登记 |  |
| OneXPlayerMiniIntel | 无充电声明/协议 | 未登记 |  |
| OneXPlayerMiniPro | 无充电声明/协议 | 未登记 |  |
| OneXPlayerOneXFly | 无充电声明/协议 | 未登记 |  |
| OneXPlayerOneXFlyF1Pro | 无充电声明/协议 | 未登记 |  |
| OneXPlayerX1AMD | onex-direct-ec-threshold-and-bypass | oxp-x1-amd-ec | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Intel | onex-direct-ec-threshold-and-bypass | oxp-x1-intel-ec | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Mini | onex-direct-ec-threshold-and-bypass | oxp-x1-amd-ec | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX1Pro | onex-direct-ec-threshold-and-bypass | oxp-x1-amd-ec | YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed |
| OneXPlayerX2 | 最终判断返回 false | 未登记 |  |
| OneXPlayerX2MiniPro | 最终判断返回 false | 未登记 |  |
| ROGAlly | asus-rsoc-ack-only | asus-ally-acpi |  |
| ROGAllyX | asus-rsoc-ack-only | asus-ally-acpi |  |
| SteamDeck | steamdeck-firmware-threshold | valve-deck-firmware | YMCC writes one threshold byte; HC setter builds an Int32 byte array |
| SteamMachine | 无充电声明/协议 | 未登记 |  |
| SuiPlay0X1 | ayaneo-cec-software-bypass | 未登记 | SuiPlay Windows 未开放 |
| XboxROGAlly | asus-rsoc-ack-only | asus-ally-acpi |  |
| XboxROGAllyX | asus-rsoc-ack-only | asus-ally-acpi |  |

## 复现

复用 tools/PcCapabilityAudit 生成 locked/upstream-inventory.json；审计不加载 HC、不开设备。

```powershell
node tools/battery-hc-coverage-audit.mjs --evidence G:/YeManCC-Work/Testing/YMCC/Evidence/BatteryCapability-20261105 --out G:/YeManCC-Work/Docs/Tasks/HC/Capability-Library-TraeCode-CN/Codex/observations/HC-CODEX-084-ONEX-NATIVE-SOURCE-RECOVERY/audit-with-auxiliary --date 2026-11-05 --stem BATTERY-CAPABILITY-AUDIT --field-evidence G:/YeManCC-Work/Docs/Tasks/HC/Capability-Library-TraeCode-CN/Codex/observations/HC-CODEX-084-ONEX-NATIVE-SOURCE-RECOVERY/field-battery-with-windows-auxiliary-contract.json
```

