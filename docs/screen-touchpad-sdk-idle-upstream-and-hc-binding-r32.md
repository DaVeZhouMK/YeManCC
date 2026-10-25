# R32：SDK raw 中立帧缺陷的上游确认与 HC 实际版本

2026-10-16，Asia/Hong_Kong。用户持续反馈只有 SD 触摸板→Steam 鼠标闪烁，直接屏幕 Mouse、虚拟手柄及摇杆鼠标正常。上一轮 R31 为 progress，完成静态边界及对照校正；目标仍未完成。

**本轮取得第一方 SDK 缺陷证据，并补到本机 HC 真正携带的 native 版本。没有新的行为修补，也没有证据宣称闪烁已经改善。**

## SDK 上游确认的生产端问题

HIDMaestro 官方 v1.10.0（2026-10-02 发布）明确修复 raw 消费者与 idle 默认State混写：旧idle重发_lastSubmitted，raw不更新该状态，可在消费者触摸报告之间插入中立帧。其上游条件中，4ms输入下237份报告有75份被读成释放；这是上游实验，不是本机或微星复现。

固定tag commit b2c53fea9a5f17676a12071c00d08ddbfe6cc273 的实际 HMController.cs 也核到 _submitLock、RememberRawFrame、RepeatLastFrame：提交与重复共用锁，重复raw的完整原字节。发行说明与源码相互支持，不仅是关键词命中。

当前锁定SDK bd42a99…的离线反编译有相同缺陷机制。主线R28在首份合法Deck FRAME/NEUTRAL前Cancel/Join旧idle，后续统一raw，现SHA88b5cf…保持，已避开这条已核正常控制流。R23还确认13:46/14:34现场业务DLL已带首次pad前的Cancel/Join，不能把上游缺陷直接当作其已接管后持续闪烁的定论。R28修补首次pad前State竞争的窗口，但未获闪烁实机验收。

没有升级SDK、重开idle或照搬上游4ms高分辨率时钟；那会改变本案既有节奏与CPU。上游primary source及精确哈希见研究根UpstreamIdleFixReview/FETCH-MANIFEST.json，逐边比较见其REPORT.md。

## HC 实际字节，而非仅看另一份源码

本机候选HandheldCompanion.dll SHA0c5132a9…、libVIIPER.dll SHAe2802542…（3,914,240B）。实际HC DLL有SteamDeckTarget→VIIPERTarget→ViiperService→P/Invoke viiper_device_set_input调用。native Git blob6006cfcb…与本地HC commit06c0b954…条目相同；Go buildinfo指向Alia5/VIIPER679f7e0e4e8ed73addfd7c570f1bc42489bf6be7、vcs.modified=false。

这是固定二进制版本／实际调用证据，不证明运行时已选择或加载、正常实测，也不是可复现构建证明。固定HC a250b19a树内另一份10,083,797B库已下载且精确核Git blob1afa78ec…，SHA91a1851e…；其buildinfo为Alia5/VIIPER bf9d018d…、modified=true、模块版本+dirty。它与本机679库及R29的0ec源码都不同；无法把dirty库直接等同于干净bf9d源码或R29实现。

父随后取得679同commit九份必要源码，逐份核Git blob：state复制后frame++并通知；EP3等新输入，6ms deadline返回nil时server重发lastInResp完整缓存。有pending读才走此链；不主动清touch。它不同于R29较新0ec56f5的inputgate/endpoint worker；不能把后者的完整保持实现外推到这份旧库。

旧679源码输出64B、byte3=56，pad touch/click/XY/force偏移与YMCC一致；独立静态核查又限定到旧DLL的HandleTransfer与buildReport两函数，共2016B，确认0x38参数传入并写header byte3。上游较新0ec源码是byte3=64。这个版本差异不构成把YMCC改成56的依据，所核SDL严格64门仍保留；未证该差异与闪烁有因果。也不照搬HC的250Hz声明或6ms缓存重复。

原生版本、元数据与工具回执见研究根HCActualBindingReview；旧后端实际源链见ActualViiper679Review/REPORT.md与SOURCE-MANIFEST.json。

## 辅助鼠标 GET_REPORT 的结果

2026-10-16 R35纠正：固定SDK的非primary分支结尾为SendError(-32)+return，不能到达共同GET_REPORT(Input)缓存分支。真实SDK/USBIP隔离测试if0/if1均拒绝、if2返回当前64B，因此撤回此前“辅助GET_REPORT可返回vendor缓存”的错误判断。鼠标声明40bit/5B的独立描述符校验仍成立。详见screen-touchpad-protocol-mixed-input-r35.md。正常interrupt路径只完成primary0x83；18:47当前Host的190条端点观察中，0x81/0x82均pending2且oldest sequence固定43/51，primary的oldest有190个不同值。它们只是稀疏元数据，不是GET_REPORT或实际完成证明。

微软官方契约与样例未建立“辅助中断不完成→自动持续GET_REPORT→系统鼠标事件”回退；专有reader是否另行请求及消费仍未知。不能因为handler存在就认定有第二路持续鼠标。WindowsGetReportReview与AuxiliaryEndpointReview保留来源及精确限制。旧679参考同样忽略GET_REPORT的wIndex，不把HC当这方面正确性的保证。

另从同一归档定向核controller.txt与support累计测试文件：主要18:42:32–36段未找到controller重开／关闭记录，前后窗口仅有config cache/activation queue条目；不能把Queueing activation等同实际binding切换。支持文件历史FAIL不当作当前触摸板或震动故障，也没有由缺feature日志证明没有control请求。见研究根FeatureControlFieldReview。

## 完成边界

当前SDK缺陷类型已有独立上游确认，单写者修补保留；本轮没有新生产行为或CPU工作。仍缺同一次持续滑动在实际最终交付中的touch位／XY连续性，无法仅凭源帧编码一致、SDK稀疏缓存、Host ACK或参考库版本证明现场消闪。整体目标active、未完成、未验收。

没有运行测试、fixture、仿真、SDK／产品、设备或进程内存操作；未编译、导出、部署或替换SDK。共享Program/main及其他任务修改全部保留。维护只读audit仍13 areas/397 candidates/1已知缺锚native/task_view_helper.cpp/0unmapped。
