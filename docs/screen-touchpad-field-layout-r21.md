# R21：匹配现场布局、坐标参数默认值与 selector 纠偏

2026-10-16。目标仍是屏幕触摸板经虚拟 SD / Steam 合成鼠标时闪烁；直接 Mouse 路径正常。没有新现场 ZIP，本轮使用此前明确失败的13:46包与14:34包，补出它们未导出的真实 stored layout 字节，追到该鼠标模式的默认设置。没有安全生产修复依据，未修改输入/显示/配置，不把静态证据当作闪烁已改善。

证据根：`G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R21-20261016`。维护入口已读，Steam audit为13 areas / 395 candidates / 0 missing / 0 unmapped；只是索引检查。

## 现场 stored layout 的字节身份已恢复

两份归档 `steam-desktop-bindings.json` 都记录同一 desktop autosave body：17477B，SHA256 `d1e7cf9364a7e108ab62110efa592e4a9eecfa8e36ad76b8f251e9299b84e4f6`；两次body读取相同。只读查本机现有 `D:/Game/Steam/steamapps/common/Steam Controller Configs/89250578/config/413080/controller_neptune.vdf`，大小与SHA完全匹配，再读一次仍相同。

这不再是“另一份看起来相似的默认配置”。可用这些完全相同的字节补上原导出省略的mode/setting；没有读取运行中Steam、切换配置或修改文件。只在Build研究目录冻结原文，主线文档不复制原始绑定命令、个人描述或其它无关内容。

| 已存 preset | 右触摸板 active 引用 | group 的字面 mode | group 中 mouse_smoothing |
|---|---|---|---|
| 0 | group14 | absolute_mouse | 未显式设置 |
| 1 | group19 | absolute_mouse | 未显式设置 |

完整文档也没有显式 `mouse_smoothing`，root settings仅有left_trackpad_mode/right_trackpad_mode（均为0）；不将这些root字段误读为软件sourceMode开关。action_layers为空。新增数据投影的全部group IDs、两个preset的pad/stick引用分别与两份原导出摘要一致（13条/4条）。

来源及核对：`ModeSettingsReview/stored-layout-hash-match.json`、`field-identical-layout-projection.json`、`field-layout-consistency.json`。旧包13:46归档SHA为 `8280bfb15634cb7e5618188629bbf9bf437bf109f297eb2aac1a9eb739f8aa05`，14:34包为 `d8d1a64ab506ce8210c159037167a29079dd7ef069a3e41c4598ae59fccecc3e`。

此证据严格属于stored layout。原导出没有activePresetKnown/Steam已消费回执，13:46日志Loaded Config文字也不是逐帧配置身份。条件结论为：**如果使用这份文件中任一已存preset，右pad的mode都为absolute_mouse**。不据此声明每个闪烁时刻一定使用它，也不把当前本机Steam状态套到微星现场。

## 配置名称和默认值已接上

固定Windows Steam binary沿用1791918634官方缓存，steamclient64.dll为26544280B，SHA256 `e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e`；不宣称它是当前最新版本或微星loaded image。

从原生protobuf元数据的(ptr,length,value)行读取31个source mode、166个setting，所有名字前缀及数值唯一；再与独立VDF key dictionary对照：

- EControllerSettingMouseSmoothing=43，字面VDF key为`mouse_smoothing`。R20触摸板节点的查询参数EDX=0x2b，因此参数名称不再靠猜测。
- EControllerSourceMode_AbsoluteMouse=4，VDF字面`absolute_mouse`同样对应4；RelativeMouse=5对应`relative_mouse`。这是字典映射，不把它们自动等同于鼠标最终SendInput的ABSOLUTE标志。
- CAbsoluteMouseMode构造器`0x8aaee0`在`0x8ab624..0x8ab685`注册key43，值对象为`this+0x4f0`。其当前int值和默认int值均初始化为20；另外两个int字段为0/40。本轮不仅凭字段排列给后者附加UI范围语义。
- 该值对象使用整数vtable `0x12dc9e0`，slot0 getter `0x62a4d0`明确读取`int32[this+8]`；default getter读`this+0xc`。owner setting-map getter `0x66c080`查询key后通过这个virtual getter取得数值。只有在实际对象仍使用这些构造值时才能推出运行值20。
- 已核触摸板节点将成功读出的43值除以40；20对应0.5。取不到setting时的数值fallback也为0.5。但文件未写override不等于现场runtime没有继承、缓存或其他覆盖。

主线程新增选定指令块7块251B，每块保留RVA、二进制、SHA与完整指令边界，见`ModeSettingsReview/accepted-block-index.json`；不是执行构造器或getter。映射与默认值收据分别为`validated-mode-and-setting-map.json`、`vdf-mode-key-table-candidates.json`、`absolute-mouse-default-smoothing.json`。

**将mouse_smoothing设为0不等于关闭R20那段坐标处理。** 已核分支中，归一化参数a=0仍会构成10000的后续参数，并继续既有处理；没有在这里因0直接跳过滤波的条件。不能据名字添加一个声称“关闭Steam滤波”的开关，或未经核验写用户配置。

官方Steamworks Input Source Modes文档把鼠标模式与Smoothing列为可配置能力；它不说明本次closed native实现细节或现场运行值。页面及读取摘要保存在`ModeSettingsReview/official-document-receipt.json`，原生数字/默认值结论仍依据固定机器字节。

## 必须纠正 R20 的 selector 命名

R20中暂把T+0xb8叫mode，不能据此套用上述SourceMode枚举。R21独立静态审阅已接上：

`Q+0x20 → O+0x224 → source工厂第二参数 → 构造器第5参数 → T+0xb8`。

工厂还有source缓存路径；当前O字段不等于既有T创建时值。Q字段的枚举域仍未确定，既没有证明它是EControllerSourceMode，也没有证明它是ControllerStyle。pressure selector另接受100/130也不能单独证明另一枚举域。

因此，**“stored absolute_mouse=4”和“T+0xb8 selector允许4”是两个独立事实，数字相同不能连成现场进入滤波分支的证明。** 之前对用户的编号关联在本轮明确纠正；主线R20文档加注，R20已封存的原始机器证据和独立review保持不变。

新增构造审阅还确认：T+0x2c4构造值0.5f；参数覆盖门关闭时，F构造中的两对参数分别回退为50.0f和10000.0f。早期向量种子之后还会被全局值覆盖；这些都是条件构造事实，不是运行态参数。来源：`FilterConfigurationReview/FILTER-CONFIGURATION-REVIEW.md`及EVIDENCE.json；新7个完整单元2704B、37份文件摘要已封存。

## 单轴相同的 hold 条件与门值限制

根中59B指令块`0x672b9d..0x672bd8`的谓词为：

`hold = enabled_global && touch && (currentX == previousX || currentY == previousY)`。

是同X **或** 同Y，不是要求两轴同时重复。这对屏幕触摸的水平/垂直移动及重复BUS样本值得继续关联，但它依赖全局门。指针槽0x180fe50、参数覆盖槽0x1820e60等位于PE零填充虚拟区，磁盘不含运行指针/value，不能用读取到的零或临近区段数据代替初始化后的真实值。

二进制还含`sc_kalman_repeated_sample`及6个其他kalman配置名，暂未建立name→此gate指针→当前值的完整链。独立SteamCMD命令dump（固定repo commit `ef82ed60c01d2ea664f808f4fb8ddacd7e3f1178`，其版本文件1791918946）将该名称列为0；它不是1791918634客户端/微星现场门值证明。此前搜索得到的另一个gist实际API请求404，仅搜索缓存不能算已获取原文。完整范围见`independent-console-dump-receipt.json`。

这组证据要求保持克制：不能默认门开，也不能以OR条件存在就给坐标加jitter、提高发送率或改Steam全局变量。本轮没有执行console命令。

## 本轮未改动生产行为

只读分析并整理研究文档；没有测试、数值仿真、启动Steam/游戏、读取运行中实例、设备/输入操作、设置变更、构建、打包或部署，没有新增运行时CPU工作。一次离线提取的边界错误已通过固定最大长度替换其发现稿（旧861942851B→11156B），不用于结论；已接受的指令/元数据和stored layout证据均有独立范围与摘要。

额外探索了与field字面mode对应的CAbsoluteMouseMode入口；`AbsoluteMousePath/`中root由chained pdata拆分，带间接switch的候选明确保留未解析。没有把R20的RelativeMouseMode叶替代本次布局的整条消费链，也未声称已经接通到SendInput出口。

当前确证的是现场stored bytes、实际字面鼠标mode、参数名称及条件默认值，外加selector来源纠偏。仍缺同一实际实例的selector/模式对象、有效设置与覆盖门、最终坐标/rangeguard结果，才足以判断Steam内部清touch是否解释闪烁。**没有把这些未证边界改写为“数据正常”或“闪烁已解决”。整体任务继续，生产修复保持hold。**
