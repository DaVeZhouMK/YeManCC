# GitHub 发布前检查与正式发布

普通快速发布不跑完整产品夹具，`fixtureValidation=NOT_RUN`；风扇另有定向发布检查。旧 FullAcceptance 结果及历史失败保留。

准备与正式工作流共用 `.github/actions/release-build/action.yml`，正式仅匹配版本tag触发，prepare没有Release、tag或main写入步骤。

## 只检查，不导出成品

`release-preparation/**`分支push和手动运行 `release-prepare.yml` 的默认模式均为check-only：

- 校对触发SHA、版本/notes、源资产/TDP清单，不消费本地忽略的编译产物。
- 校对实际提交里的已批准风扇归档、V2 载荷及共享依赖。未入 Git 的工作目录文件不能补齐发布输入。
- 用真实GitHub GET核对仓库、绑定main提交的版本、候选tag/可见Release占用和main规则；注解tag解析到commit。
- 在线核对官方Decky来源，检查runner MSVC/SDK入口及x86环境。需要.NET的MSBuild输出目录求值只在完整模式安装SDK后执行。
- 不安装Node/pnpm/.NET、不还原依赖、不做类型检查、不编译、不打包、不生成源码ZIP。只保存小型诊断回执。

手动运行勾选 `build_packages=true` 才执行完整 FastRelease 及附件检查，仍不发布。准备构建和正式构建都会先跑风扇唤醒接续、首次点击、旧通知失效和跨代恢复检查。跨代检查区分可作废的旧读回包与必须串行结算的已提交写入。正式模式硬性拒绝 check-only。只检查成功不代表完整构建或写权限通过，回执保留 `packageBuild=NOT_RUN`；公共 action 的 `packages-built` 输出区分是否实际构建。

本地使用PowerShell7，在有GitHub CLI身份的进程执行：

```powershell
& tools/github-release-preflight.ps1 -ProjectRoot <固定候选Git目录> -Mode prepare -Repository DaVeZhouMK/YeManCC -ExpectedCommit <提交SHA> -EvidencePath <仓库外回执路径>
```

主线仍为已发布0.0.37时，原样检查应报告VERSION_TAG_OCCUPIED/RELEASE_ALREADY_EXISTS；不能把版本已占用写成通过。下一版本和正式说明先在候选确定，本工具不涨版本、不移动旧tag。

### 打标签前核对风扇输入

从当前主线调用下列脚本，`-ProjectRoot` 指向实际待打标签的干净候选目录。即使候选基于旧发布点、里面还没有本检查，也由当前主线脚本核对它。必须看到 `FAN_RELEASE_INPUTS_OK` 对应该提交 SHA；缺失或失败时先补齐候选再打标签。

```powershell
& tools/verify-fan-release-inputs.ps1 -ProjectRoot <固定候选Git目录> -ExpectedCommit <提交SHA>
```

已批准配套包含 Win5 原请求的唤醒接续和 Win4 6800U Open 的失败回执。录入后继组件时，用同一提交更新风扇载荷校验、归档和原生恢复绑定，并核对两项修复仍在。出包后仍由 `fan_host_path_gate.ps1` 对最终 ZIP 的 V2 文件与该提交逐项读回；现场故障是否消失另看真机。

## 提前暴露正式发布阻塞

远端检查在重构建前执行，完整构建后、Publish前再读一次，防止构建期间tag/Release/main状态变化。当前版本已有公开Release或可见draft时拒绝覆盖；部分发布失败先核原run与远端身份，不能盲目重发同版本。

main删除、禁止强推、线性历史规则本身不阻止正常单文件提交；其他更新规则、经典保护或读取受限的保护状态要求先核actor。读取成功不证明contents:write，回执始终写writePermission=NOT_PROVEN_READ_ONLY。准备token不可见的draft不能宣称不存在。

工具只发GET。网络/5xx/429最多3次，已确认的业务阻塞不重试；失败保存BLOCKED及请求状态。轻量预检限10分钟、显式完整准备限60分钟，不启停产品、设备或游戏。

## 正式专属阶段

- 正式作业45分钟上限，完整FastRelease公共入口与prepare构建一致。
- Publisher要求四附件路径全部存在，显式使用校对tag，不覆盖同名附件。
- 发布后要求稳定公开Release、四附件各一份、uploaded且非空。下载有步骤上限和3次传输重试；SHA不符立即拒绝，不以重试掩盖身份错误。
- 四附件公开下载哈希及FanHost身份通过后，才读取当前main清单、防降级、按当前文件SHA提交；读写API错误显式终止。
- 诊断附件上传失败保留warning，不单独挡住已通过的业务门；真正编译、来源、身份失败仍停止发布。

只读准备不执行Release上传、公开附件回下载或main写入，不能替代正式时的实际回执。GitHub发布API对工作流文件差异可能另有workflow权限要求，需要按最终提交和正式token核对；本工具不通过写草稿、测试tag或main探权限。

## 历史入口

2026-11-03完整公共流程run41596757864通过、附件已下载核对，这是原固定提交的历史记录，不能给后续代码写PASS。旧037文档中的EnvironmentCheck/FullAcceptance保留为当时阶段，当前以本文件和实际workflow为准。
