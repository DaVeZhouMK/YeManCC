# T22 BUS-P101：ROG 通用采集终止与 current-source 静态回归（2026-09-06）

状态：`STATIC-REGRESSION-PASS / GENERIC-COLLECTION-STOPPED / TARGETED-RUNTIME-INTERVENTION-REQUIRED`

## 1. 本轮范围

本轮承接 P100 的两次用户运行：

1. 静止—转动—静止：已证明 ROG WinRT gyro/accel provider 存在并有真实非零样本到达采集链。
2. 打开后手动关闭：已证明同一 run/epoch 的 Host-local release 以及虚拟 target PnP disappearance。

本轮不重新运行 InputHost、HIDMaestro、HidHide、Steam、游戏或真实设备；只做主线文档回写、RF00 source/provenance 冻结、T20/T21/T14 对账和静态 selftest。

## 2. RF00 / T20 / T21 / T14 结果

```text
RF00 manifest = T22-RF00-T10-I-CURRENT-SOURCE-20260906-manifest.json
manifestId    = T22-RF00-20260906-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = A1273D8DFB3AC609D3A1CD6521F90C9132E27702695011F10A71AC01F322EC47
keyFiles      = 74
YMCC porcelain= 489
HC worktree   = clean
runtimeOperation = false
buildOrTest      = false

T20 = 21 historical claims / 4 current-authority / 13 historical-reference / 4 superseded
T21 = 9 current claims, all runtimeUpgrade=false, same manifestId/sourceDigest
T14 = DGF-01…DGF-15 only; no DGF-16
```

RF00 仅证明静态 provenance；T20/T21/T14 不改变 T11–T18 的 runtime 状态。

## 3. 静态回归命令与结果

以下命令均在 `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC` 执行，未触碰设备或外部消费者：

| 命令 | 结果 | 证据/边界 |
|---|---|---|
| `pnpm run test:gyro-virtual-mainline-status` | `RUNTIME_BLOCKED`（预期） | 主线仍拒绝把静态工件升级为 runtime |
| `pnpm run test:hc-parity-drift-ledger` | `PASS` | T14/T19 ledger selftest |
| `pnpm run test:hc-parity-ledger-audit` | `PASS / entries=15` | DGF 编号范围与 ledger 结构 |
| `pnpm run test:gyro-virtual-sidecar-audit` | `PASS` | 侧车资产/主线边界 |
| `pnpm run test:persona-descriptor-parity` | `PASS` | T17 descriptor parity 静态审计；不等于 raw readback |
| `pnpm run test:rog-hidhide-contract` | `PASS` | T13 ROG/HidHide 负例合同 |
| `pnpm run test:rog-gyro-protocol` | `PASS` | ROG 协议静态 selftest |
| `pnpm run test:a1-gyro-release-boundary` | `PASS` | 正式包禁止 HidHide；测试包与正式包分离 |

## 4. 采集策略裁决

已有 P100 数据足够支撑：

- provider presence；
- 真实 gyro/accel 非零样本进入 collector；
- Host-local release；
- 虚拟 target PnP gone。

因此，重复的通用“静止—转动—静止”采集**暂时终止**。继续生成同类包不会补上当前最小缺口，反而会重复证明已有事实。

下一阶段只允许窄范围、字段导向的测试包：

1. T16：same-provider pair、matrix/calibration、steady、first paired sample、sleep/rearm。
2. T17：DS4 descriptor、report ID/length/offset/endianness、独立 decoder/raw readback、battery producer。
3. T18：P-HID/P-XINPUT/P-OWNER、Steam/game/XInput/GameInput consumer count、恢复/崩溃路径。

## 5. 仍未闭合与介入点

```text
pair/matrix/calibration/UI output        = UNENCLOSED
DS4 descriptor/IMU/battery producer      = UNKNOWN / RUNTIME-BLOCKED
Steam/game/XInput/GameInput consumer     = UNPROVEN
P-HID/P-XINPUT/P-OWNER full isolation    = UNENCLOSED
runtimeUpgrade                           = false
```

只有在下一阶段需要真实 ROG、Steam/游戏或 DS4 consumer 观察时，才需要用户介入。届时必须使用当前 source digest 生成窄范围包，并回传同一 runId/epoch 的完整证据；在此之前不继续重复通用采集，也不凭现有数据猜测 HC matrix、battery byte、profile 或消费者隔离。
