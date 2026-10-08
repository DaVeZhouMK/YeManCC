# YeManFanHost 正式授权记录

```text
authorizationScope: YeManCC-mainline-fan-api
hardwareWriteAuthorizationGranted: true
hardwareWritesAuthorized: true
allMappedFanRoutesAuthorized: true
authorizationMode: production-gated
```

本记录仅由正式 Host 启动参数引用。Host 仍要求独立的
`--allow-hardware-writes` 和固定确认口令；主程序任一 Gate 关闭时不会启动
Host。未被 HC 风扇矩阵识别的设备、依赖缺失、身份 Gate 失败、lease 失效或
OEM restore 失败均保持 fail-closed，不执行风扇写入。
