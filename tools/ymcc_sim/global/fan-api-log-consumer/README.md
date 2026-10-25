# fan-api schema2 日志到消费者

独立入口已实现并在自有无硬件夹具中执行：未修改的 `fanApi.ts` / `fanDiagnostics.ts` 生成投影，新增解析器提取安全字段，再调用原 `replayFanSignals`，由原 `FanHostLifecycle` / `HttpFanApiAdapter` 消费。当前未接普通 ZIP、log-corpus、Python ZIP 读取器或共享 CLI/registry；这些文件没有在本模块中修改。此模块不宣称整体能力覆盖完成。

## API

```js
import { normalizeFanApiRecords, consumeFanApiRecords } from './index.mjs';
const normalized = normalizeFanApiRecords(records, { sourceIdentity, limits });
const consumed = await consumeFanApiRecords(records, { repoRoot, sourceIdentity, limits });
```

`records` 是 JSON 数据记录数组，或 UTF-8 JSONL 的字符串/Buffer。正式夹具记录形如 `{atMs,event,details}`。只识别 `details.observationSchemaVersion === 2`，不把外层 `schemaVersion` 当成版本替代。数组身份为 `sha256(JSON.stringify(records))`；JSONL 身份为原字节的 SHA-256。`sourceIdentity` 必须有匹配的 `sha256`，`kind` 为 `source-derived-fixture` 或 `synthetic-json`。它不接受会话身份作为关联依据。

`normalizeFanApiRecords` 返回 `{events,diagnostics,scope}`。诊断只含固定代码、自有行序号和级别；任何拒绝都会清空整批事件，防止有效前缀掩盖坏记录。不支持但格式完整的观察保持 INCOMPLETE。`consumeFanApiRecords` 对拒绝/零事件输入不调用消费者；对可连接事件调用原消费者，并仅返回允许的回执字段及实际源码身份。

当前连接的事件：

- `api.response`、POST `/api/handshake`：要求真实 `receipt` 根字段和 `response` 投影共同存在、类型正确、重复事实一致，检查 HTTP 计算结果、receiptValid 和已观察到的完整响应体。根事实始终来自 receipt；不会把嵌套 state 的 supported/ok 提升为根事实。
- `api.transport-failure`、GET `/api/state`：只接受现有正式夹具实际发出的 WinHTTP 12029 / send 形状，要求 responseReceived=false 且没有响应/HTTP状态。只提取固定错误类别，错误全文不返回。其它错误保持未接，不伪装成这个错误。收到响应后发生的 api.request-failure 有独立未接诊断，不会按错误文本改造成传输失败；根失败真实投影也由正式夹具覆盖。

输出所有重构事件均标为 `source-derived-fixture`，使用自有事件序号。`response` 的原位置不能由投影证明，因此保留 `root-or-state-projection-unproven`；不根据时间相邻、请求号或 generation 建立原现场会话/因果。不会创建 fan.intent、fan.power 或控制曲线。普通 API 响应、HTTP200、accepted 不被提升为物理写入/恢复证明。

## 字段与隐私

保留经过类型检查的根布尔、能力布尔、数值协议版本、时间/generation及少量已审阅常量。FanRoute 仅取当前生产枚举 GenericDuty、GenericRpmTarget、ProfileCurve 和明确 EC 路线。已审阅固定 profile 类别用于保持当前真实 gate 分支；带私有身份的这些 profile 记录保持未接，避免删去制造商字段后绕过原 gate。其它动态设备身份不返回。未观察到的协议版本继续缺席，不补默认2；未知模式/路线保持 INCOMPLETE。

凭据、原 request/session/member/事件 ID、token/lease、游戏、路径、错误 free text、reason、原 deviceIdentity/EC对象及所有未列字段均不返回。消费者回执也重新投影，不直接透传原 replay 对象。源码哈希中的路径是已执行实现的固定 repo-relative 名称，不是日志中的用户路径。

## 预算

硬上限：输入1 MiB、单行16 KiB、1024行、128事件、60秒跨度、64条诊断；JSON树深度10、每记录1024节点/每对象128字段。调用者只能收紧预算。超限明确拒绝，不静默截断成成功。JSONL重复键（包括转义后同名）、重复观察、倒序时间或 generation 都会拒绝。输入必须是 JSON 数据，不调用 getter/toJSON，不接受循环对象。

## 自有夹具执行

```text
node tools/ymcc_sim/global/fan-api-log-consumer/run.mjs --fixture --evidence-dir <新的绝对证据目录>
```

夹具复用 `createFanSignalFixture`，仅调用真实 lifecycle.start 与 getState、既有 NoIO传输失败开关。原生产日志模块真实发出版本2字段，原字节以 JSONL 归档到当前证据目录。无原生Host、外网、设置、硬件或真实睡眠。不会读用户 ZIP。

独立 JSONL 输入需显式 `--input-jsonl <绝对路径.jsonl> --source-sha256 <原字节SHA> --evidence-dir <新目录>`。输入不被原样复制到输出报告，仅自有合成夹具才归档原投影。此入口不代替普通ZIP接线。

`PASS` 表示这些输入的解析与软件消费者链已执行，并不表示每个观察到的操作成功：正例中握手接受，12029仍为拒绝，写入次数0、物理收据0。源派生重构及来源缺口在 scope 中保留。

66项测试覆盖真实投影、原消费者、根/投影矛盾、缺版本/类型错误、不完整/未知、HTTP200空证据、重复/倒序、敏感值、JSONL重复键及预算。另在D副本删除根/投影相等门，原断言真实失败（NORMALIZED 而非 REJECTED），编译/启动错误不充当业务反例。原目录及原断言未为故障测试修改。

## 后续接线

由环境主线程审查本独立API、实际源身份及证据后再接普通日志/ZIP入口。共享读取器、覆盖登记、现有白名单均保持各自所有权；本模块不会自行把“普通ZIP仍未接”改为完成。生产 schema、投影字段或设备门变动时，原来源回执过期，需要复核映射和定向重验。