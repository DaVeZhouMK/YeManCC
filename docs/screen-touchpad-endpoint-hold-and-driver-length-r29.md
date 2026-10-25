# R29：虚拟 Deck 端点等待、参考状态保持与驱动长度

2026-10-16。继续虚拟 Steam Deck 触摸板闪烁；直接 Mouse 正常这一对照保留。上一轮已有源码修补与隔离编译，属于 progress。整体仍 active、持续闪烁未定位。本轮只查生产/交付代码与已有日志，未继续 Steam 客户端逆向。

## 本轮新增的确定证据

**核对的后端就是本案实际后端。** 固定 steam-deck-composite 的 backend=usbip；HMContext factory → UsbipBackend.CreateDevice → 托管 UsbipEmulatedDevice → loopback UsbipServer → VhciClient.Attach 调用已闭合。18:47现场193条被动对象观察全部通过模块身份/字段门，主端点均0x83、缓存报告均64B。当前 Host11272/epoch4 的191条中frameQueue均0、线程元数据存活、连接关系same-device；190条主pending=2，1条尚无键。旧Host释放态另分组。稀疏采样不能排除滑动瞬间队列积压或证明请求已完成。

**固定生产到服务端路径会提交完整64B，未找到无新输入自动补touch0。** raw builder → SDK同步拷贝 → 共享长度64/extendedLen0 → pump稳定读 → 完整byte[] → RET_SUBMIT actualLength64/payload64 → socket循环写。相同共享序号时不完成新请求，无数据只挂pending；unlink是取消回复。默认零缓存有控制GET_REPORT路径，未见无输入时自动送入主interrupt-IN。共享slot仍可能合并中间状态，最多8份整帧队列可丢旧帧，发送失败被catch；均不能被Host ACK掩盖。

**重新核对对应驱动的长度规则。** 固定SDK安装器指向usbip-win2 0.9.7.7；从官方重取同tag commit的wsk_receive.cpp与已有原始缓存逐字节相同。该源码在匹配非等时IN请求上接受actual_length<=缓冲容量，对64超过8失败完成，并非成功截成8；64/64按64接收，短网络接收也不会按完整成功处理。SDK不验证已经存在的驱动SYS版本/哈希，故不把这个固定源码自动当成现场运行驱动，不排除上层HID读取或错误处理。

## 与 HC 参考后端的真正差异

同一固定 VIIPER commit 0ec56f51019a0fa454dfd18f9f162bef76095ebf 补齐恰好8份必要服务器/注册/协议源码，并逐项核Git blob。上一轮只到设备helper，本轮闭合到实际RET_SUBMIT和socket Write；R28文档已精确更正。

| 条件 | 当前固定 SDK | 同commit VIIPER参考服务器 |
| --- | --- | --- |
| 有新完整状态 | 共享事件唤醒pump，配pending或入报告队列 | 更新完整状态/frame++，唤醒当前端点job |
| 没有新状态、有pending IN | 继续等待新共享序号 | auto/keepalive且有有效interval时，attempt期限到后构建当前完整状态并写回复；旧frame值不变 |
| 没有pending IN | pump仍接收Host状态，最多8报告缓存 | 端点worker阻塞等job，不自行推报告 |
| 取消 | 删除pending并RET_UNLINK | job取消；取消检查与后续写非原子，不能承诺绝无迟到completion |
| 时基/CPU | 自动重置事件；有句柄时等待500ms检查，无句柄才Sleep4；不是4ms重复发包器 | 每端点1个worker/8个job槽；轮到job才有timeout；pacer为复用Timer，不是永久报告ticker |

参考服务器直接将描述符BInterval按毫秒使用；该Deck控制器interval6，因此默认attempt期限为6ms，名义约166.7Hz，但不保证所有相邻wire回复最短间隔。排队URB从入队不带deadline，期限只在执行attempt时建立。强制nak模式会关闭该timeout，环境可改默认模式。参考源码尚未绑定到HC内置libVIIPER.dll，也未采实际写出/接收。

**不原样搬6ms保持。** 本案Host后段约8ms一次提交，单写者接管后按此节奏发raw；参考6ms重复会增加报告数量，不能称为“不提升刷新率”的纯链路优化。还没有同场景因果证据，故不改counter步长、压力、包头、UP或SDK，不重启idle、不增加补帧/定时器。

## 低CPU方向与剩余边界

可以借鉴的是已有pending请求驱动、完整状态复用、自动重置事件/单槽合并和按需期限，不能把这些设计名称等同为本案已可安全落地修复。保持现有频率下若要改变交付，必须先明确是消费不到新状态、返回延迟还是主动松触；目前已有日志只能覆盖Host成功返回和SDK缓存，缺逐transfer的完成/长度/时序。无需用改变显示补画去填这个证据空缺。

另外核到SDK共享映射名只含index、分配仅在HMContext本地字典，独立SDK使用者占同index可共享甚至清同一区；这是条件风险，现场第二写者未证明，本轮不改命名/生命周期。

本轮未新增生产行为或运行时诊断，CPU新增为0；没有量测总CPU。未运行功能测试、仿真、Host/SDK、输入/设备、真实socket服务器或进程内存读取，未构建、导出、部署或覆盖共享Release。R28已修主线ScreenTouchpadReport.cs仍为SHA88b5cfde91b47df2acb1667a6dea3b567f4b7eeb1ae8f0931ce5e324dde19c4a，没有宣称持续闪烁已经改善。

完整证据根：G:/YeManCC-Work/Build/Analysis/Touchpad-Producer-R29-20261016。LockedEndpointReview/REPORT.md是父核当前SDK与驱动的逐段结论；ReferenceEndpointReview/ServerFollowup/REPORT.md和CALLCHAIN.md为独立参考端实际调用链；BackendFieldBinding为已有现场对象/队列投影，DriverReceiveReview为官方源码复核。
