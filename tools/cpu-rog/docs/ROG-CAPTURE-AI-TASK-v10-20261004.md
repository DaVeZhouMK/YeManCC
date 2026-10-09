# ROG采集AI执行书 · 正文版本v10.1 · 2026-10-04

本文件名保留v10以兼容现行manifest必需文档检查，实际工具身份以10.1.20261004的manifest为准；不是原v10重新验收。可供任何客户端AI执行，无源码/Node/SDK依赖。

## 0. 当前排程与角色

R6已经启动CPU优化＋错误修复，由本机另一执行AI实施产品；本聊天修工具。这里不恢复“等风扇/手柄全部结束”的HOLD，也不再要求一轮CPU开工批准。

ROG无源码只运行可信工具/产品、事实采集和回写。先交已产生的原v10/v9安全原始JSON，不为补文件再做产品动作。可以独立做本修正版工具完整性/严格离线夹具验证；真正产品/CPU步骤按稳定A/B交接执行，尽量一次集中，不阻挡本机先修bug。

## 1. 工具补正与证据

原v10 ROG文字报告runner138/140，wildcard/ADS两例因PS5 GetFullPath在描述符门前抛异常。v10.1修字面量前置/安全错误映射及排序回归，保留权限/可信身份/场景/隐私门。原v10 validate的raw result已收到；collector/runner两份raw validation仍待交。本机新验证和ROG新验证分开，计数以本修正版报告为准。

按OPERATOR-COMMANDS核ZIP/manifest，validate、离线runner/collector写新输出目录，逐项保存实际exit与报告路径；合法策略阻断如实报，不绕策略。工具PASS不签产品或CPU下降。

## 2. 正式批次事实链

绑定用户/本机执行方选定完整产品和A/B身份，不以ROG自发现hash授权。实际路径/hash/PID＋UTC创建时间、owner、权限、生命周期、Host身份/readback、场景开始结束闭合。

preflight/status默认只读且无需事先hash/生辰；确需权限按同名动作授权一次有限同用户RunAs。无UAC提示与实际完整性分开。观察/采样、启停、功能仍要求可信身份与原门；预检成功不自动串联动作。不force/按名批量kill，不换计划任务/SYSTEM/服务/ACL绕拒绝，不做未知EC/OEM/驱动/部署操作。

Fan AI专用mock先核协议/session/owned Host与零物理写；手柄/gyro用产品方真实客户端和能力，不能因为UI/目录/设置就断言有或没有功能。不手填active JSON。gyro可能共享本体/InputHost，不能要求独立gyro.exe。

## 3. 最少但有效的ROG性能验证

本机筛选和落主线后集中ROG同机同条件A/B，优先可证全off及受影响真实热点场景；已有同版本有效基线可以复用，只补差集。每选定场景每版本至少3个有效窗口，建议warmup15–30秒、稳定60–120秒，A/B交错重复。固定电源模式/AC/DC/页面/窗口/曲线/输入频率/日志开关。mock与physical分册，gyro依赖手柄如实写；缺活动证据不签场景。

## 4. 全进程计量和日志

总量=YMCC＋全部owned WebView2 browser/renderer/GPU/utility/crashpad＋FanHost＋InputHost＋确属本实例的辅助exe。唯一PID＋creationTime去重，gyro共享PID只计一次。排除另一实例；断链、拒读、PID复用标UNKNOWN/invalid，禁止漏算补0。

cores=ΣΔ累计CPU秒/Δ实际monotonic wall秒；oneCore%=100×cores；machine%=100×cores/逻辑处理器数。逐时间样本先求和再算整体分位数，不能相加各组件p95。保存原始差值、实际间隔、实际/期望样本、missed/jitter/覆盖、成员变化与collector自耗；计数差不直接等于掉样。

分列measurementValid、scenarioVerified、physicalFeatureEvidence、comparisonEligible。日志显式最多32个元数据/open-close，错误内容只按另有授权的白名单脱敏摘录；同内置日志/任务管理器对照须同窗同口径，保留峰值/启停成本。工具不能替产品修内置日志。

## 5. 回写与验收

只交本次安全白名单：工具/产品身份、完整runId、实际权限/worker proof与退出、动作requested/attempted/applied、功能/生命周期、samples/process表/summary/manifest、计量质量/日志清单、失败和未验项。不只Markdown，不修改结果为PASS；策略加载前失败只交控制台/exit。

排除worker-request、token/lease/authorization/confirm、原始秘密握手、settings备份、凭据、WebView profile、私密命令行。localToolValidation、rogToolRegression、rogProductRegression、rogCpuAB按修正版/批次分别写，未跑仍NOT_RUN；最终CPU胜负由裁决AI审查，不自行签。
