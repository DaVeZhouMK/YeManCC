# YeManFanHost 正式授权记录

```text
authorizationScope: YeManCC-mainline-fan-api
hardwareWriteAuthorizationGranted: true
hardwareWritesAuthorized: true
allMappedFanRoutesAuthorized: true
authorizationMode: production-gated
approvedHostExeSha256: A3CE266B880F67045D3F895E2FC68B1FE81F641D5F1A08A01F0DE19D2458F733
approvedHostDllSha256: 5D097826A864CD18BEC9AA8E2662DA82F80381340BAA447CF6E7FEED47F4CC74
approvedHcSha256: 70E27FD4D73A5CA3E3E750DE2736B5E1C3B126D716DD9F4F5794C84DA88C6415
baselineId: R5-v9
previousBaseline: R5-v8-archived
implementationState: r5-v9-mainline-minimal-fan-host-frozen
```

本记录仅由正式 Host 启动参数引用。Host 仍要求独立的
`--allow-hardware-writes`，以及与本次随机 loopback 会话令牌一致的确认值；
主程序任一 Gate 关闭时不会启动 Host。未被 HC 风扇矩阵识别的设备、载荷哈希
或安装 ACL 失败、身份 Gate 失败、lease 失效或 OEM restore 失败均保持
fail-closed，不执行风扇写入。
