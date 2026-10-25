# 任务栏常驻偏好：原消费者单叶回归

全局 descriptor：`lifecycle.tray-resident-consumer`，domain=`lifecycle`，profiles=`desktop/full`，active=`true`，joint=`false`，CPU 功耗=`null`。父端在现有同步 `getExtraAdapters()` 中展开 `getTrayResidentConsumerAdapters(repoRoot)` 返回的描述符数组；该入口没有 Promise/await。单数兼容入口 `getAdapter` / `getTrayResidentConsumerAdapter` 也保持同步；本模块不修改公共 runner、audit 或 registry。

范围是 tray 偏好的软件事务和末条 native 源码合同。它不证明物理任务栏/托盘、真实 Host/IPC、安装版、全部应用生命周期或 CPU 降功耗；不读写用户 settings、注册表、设备，不启动 YMCC/Host/Steam/App，不部署或睡眠。

## 原件和故障

`tools/tray_resident_selftest.mjs` 原字节执行，不改断言、fixture、队列或回滚。它实际通过原 esbuild `transformSync` 编译 `src/bridge/trayResident.ts`，用原 `vm.runInNewContext` 运行真实产品模块。原 VM 的 `./api` 和 `./settingsRepository` 仍返回 producer 创建的内存对象。观察层只转发这两个原 require，记录调用并恢复 require；不注入平行产品模型。

正例保留原 11 个 check；17 次 fixture 编译/VM 执行。最后一个 check 读取完整 `native/main.cpp`，核对 `tray.setResident` 返回 resident 状态；不编译或执行 C++。

唯一业务负例在本 case 的 `src/bridge/trayResident.ts` 将唯一 `if (applied !== v)` 替换成 `if(false)`。其余三个副本文件逐字节相同。必须前两项 PASS、第三项 opposite-state 的原 `assert.rejects` 在 producer 第 51 行以 `ERR_ASSERTION` / `Missing expected rejection.` 失败；应有 3 次成功编译/VM。退出码须为 1，不能以编译/缺源/缺工具/启动错误冒充业务检出。

## 身份和工具

`spec.json` 固定本轮 B48 的 producer、TypeScript、完整 native/main.cpp、package.json 和 Node 身份。未来更新这些原件需要审查并更新对应 pin 后重跑；旧回执不能批准漂移后的原件。producer 变更还须复核 11 个原名称、原断言位置、VM 次数和精确业务故障，不能改原断言来迁就适配器。

以静态相对 import 复用 `../bundled-node-tool-authority.mjs` 及其原 helper；执行 helper 的字节须逐个等于所选 repo 的四份原 helper，D 私有最小源码视图的 helper 副本单独绑定，合入时自动使用同 repo 原件。B48 使用外层显式设置的原 binding 路径和 SHA；绑定原 26 包、749 成员、Node 和元数据。所有原工具在执行前后重新哈希，并在 suite 结束重新锁定包图/别名。当前完整静态 artifactInputs 为 754 条；再计 22 份生成代码为 776，保持既有 3072 上限。node_modules 只作为外部工具，不写入产品源码 sourceHashes。

每个 case 新建原子 `sourceview/repo`，只含三个产品/producer 输入和必要的 package.json。其 node_modules 只有 esbuild 一个 junction，目标必须是原绑定包真实路径；其他 alias、同名字节复制的包、产品额外文件及路径转发一律拒绝。原 repo 和工具不被复制或修改。全部运行输出位于新建 case 中。

原 esbuild main.js 的 CJS 编译字节和加载身份被记录。`ESBUILD_WORKER_THREADS=0`，`ESBUILD_BINARY_PATH` 只指原 binary。只有原 transform 调用期间，精确原 binary、`--service=0.21.5`、原 cwd/隐藏窗口/有界二进制 stdin 等合同才允许同步执行；其他 process API 全部拒绝。每次记录真实 compilerexec 的输入/输出摘要与完成状态。另以精确 binary 的 CLI transform 独立编译同一份源，逐字节核对原 transformSync 输出；VM 输入必须就是该输出。观察层不改编译语义。

不使用 Node `--experimental-permission`，也不授予 `--allow-child-process`。`--no-addons` 加精确程序观察合同拒绝额外文件字节读取、所有业务 FS 写、设备/raw open、网络、worker、native imports/bindings、任意进程和退出/目录控制；只有观察器保存自己的指定证据。该合同面向固定的可信 producer/compiler，不声称构成对恶意任意 JS 的 OS 安全沙箱，也不声称观测到全部 OS DLL。维护探针必须在真正系统调用之前被拒绝。

实际加载声明区分：原 producer/TS/native 的实际读取、工具 JS 的实际读取/CJS 编译、原 compiler 的实际执行、生成 CJS 的实际 VM 执行。package.json 和其余静态工具是声明/校验输入；未观察到的文件不冒称实际加载。

## 调用

使用绑定的 Node v22.14.0，设置父轮给出的 `YMCC_BATTERY_UI_NODE_TOOLS_BINDING` 和 `YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256`，然后：

```powershell
node tools/ymcc_sim/global/tray-resident-consumer/output-evidence.mjs `
  --repo <完整固定源码目录> `
  --evidence-dir <新的case目录>/tray-resident `
  --fixed-run-binding <父轮B48-fixed-run-input-bindings.json> `
  --current-root <只读当前主线目录>
```

`--repo` 和 `--evidence-dir` 必需；其余两项可选。`--fixed-run-binding` 是可选分支：从当次真实 manifest 的 `files.length` 读取源文件数，并对照固定输入声明；包数/工具成员数来自当次 binding graph，须与已锁定 authority、完整 graph SHA 和 binding/manifest SHA 一致。该分支不硬编码 B48 或 7916；未传时返回 null，不读取可选证明。B48 原 capture/binding 原件不改签，本叶也不重新证明整份源码快照。`--current-root` 只记录当前四输入的适用性；native/main.cpp 与 B48 不同会明确保留 `matchesConsumedSource=false`，不因此批准当前源。

`result.json` 的 sourceHashes 仅使用所选 repo 相对源码身份；工具放 artifactInputs。独立 `output-evidence.json` 的 sourceHashes 只含本 case 内的绝对输出身份（含 sourceview、参考编译代码、原始 stdout/stderr、trace、plan、child、receipt、result）。原公共 checker 已有的 owned-case 路径合同可直接验证两份结果，无需改公共代码；output-evidence 自身由父 runner 指纹，不做递归自哈希。

每次目录必须全新。子进程以自然 exit 收尾，不能 kill/补写旧 PASS；超时、信号、错误、禁行尝试、输入/实现/工具/生成物哈希或成员漂移都会拒绝。业务失败保留原 stdout/stderr 和断言栈。

## 维护

```powershell
node tools/ymcc_sim/global/tray-resident-consumer/contract.test.mjs `
  --repo <同一固定源码目录> `
  --baseline <刚完成的tray-resident目录> `
  --evidence-dir <新的维护证据目录>
```

维护检查原正反结果、精确单文件替换、源和生成物漂移、源/输出额外成员、未授权 alias、同名字节工具转发、工具声明变化、缺 compiler/source/startup 不得业务检出，并实际探测 FS、设备、网络、进程、worker、native 和未授权 CJS 的前置拒绝。所有篡改只发生在 D 私有 owned 副本，记录前后 SHA 后恢复原字节；不改原 repo、工具或用户状态。

父端完成注册和相应公开入口验证前，本交付仅报告单叶 READY。既有父轮原 82 项、其它 helper、HC 24、TDP/performance simulation/onex/coordinator 都不在本模块执行范围内。联合验收始终 false，功耗读数始终 null。

同步接口修订只做接口/静态 helper/工具与 Node pins/动态计数来源的定向检查，再跑原 25 项维护回归；不重跑原 11 项行为场景。若维护使用旧实现的行为基线，可显式传 `--baseline-implementation-root <旧八文件实现归档目录>`：只将历史实现身份在内存验证视图中映射到原 SHA 的归档字节，原报告/receipt 不写、不重新签发，不把历史 11 PASS 声称为当前实现的新行为回执。维护中的精确故障、观察器探针及漂移拒绝仍执行当前模块。
