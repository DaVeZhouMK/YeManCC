# 触摸板 R35：控制接口、长度与混合输入模拟

本轮续接R34已执行的隔离连通链，不创建虚拟目标、设备或驱动，不操作Steam/游戏、不注入输入、不部署。任务整体仍未完成，连续滑动闪烁尚未证明根因或改善。

## 对既有判断的纠正

R31/R32原先将固定SDK的共同GET_REPORT缓存分支误认为可由辅助接口到达；这忽略了非primary分支末尾的SendError(-32)及return。R35对锁定SHA bd42a99b…的实际SDK/USBIP socket链发送GET_REPORT(Input)：if0鼠标、if1键盘均返回status=-32/length0，if2 vendor返回当前64B且触摸XY保持。原先辅助GET_REPORT会泄漏vendor触摸报告的判断撤回，R31/R32文档已更正。

辅助中断EP1/EP2读取在20ms观察窗内保持pending、不完成SD报告。此测试只支持该SDK模拟行为，不冒充Windows所有输入来源被排除。

## 描述符和短请求边界

真实SDK返回三个HID报告描述符，独立解析其Input item：if0=40bit/5B、if1=64bit/8B、if2=512bit/64B。报告ID为0，具体描述符/配置字节均保存。

对EP3 IN请求容量0/8/56/63/64/65/128实际运行：SDK总返回status0及64B。前四种因此实际返回长度超过请求容量，条件违约已复现；64/65/128正常容纳完整报告。primary输入描述符本身要求64B，当前没有故障同段真实读请求长度，不能把短请求反例直接当现场闪烁根因，也没有据此截短报告或改SDK。

ProtocolLoop共15项探测完成；独立raw header与descriptor解析66检查0失败。初版取消请求测试因夹具未初始化音频对象而失败，属于fixture不完整，Run/FAILURE.json原样保留；不当SDK产品异常。最终探测取消该无关Audio路径，直接在私有socket结束时清理pending。

## 混合手柄输入

R34持续滑动测试的普通手柄字段为中性。R35新增独立手写混合输入场景，覆盖物理按钮、hat、摇杆、扳机、IMU、专用键、背键和触摸组合；输入明确标注independent mixed scenario，不谎称来自真实native触摸或物理采样。

408帧经真实HostSession.Handle、TrySubmitFrame、SDK raw/shared/pump、USBIP/socket接收完成，含复用控制共75项通过。独立解码socket raw Hex并对expectedWire核验按钮、hat、摇杆、trigger、IMU、quaternion零值、force零值及pad全字段，共10,541检查、0失败；不是只核pad。独立离线encoder的408帧同样10,541检查0失败。

## 变更边界与交付

生产运行时代码、报告频率、SDK、手势和CPU链无新增修改；只增加隔离夹具、离线验证、研究证据和维护文档。31项Host/依赖输入与R34冻结基线的当前逐文件核对见研究目录SOURCE-CURRENT.json。

研究根：Build/Analysis/Touchpad-Producer-R35-20261016。ProtocolLoop/Final为协议探测结果，ProtocolLoop/MixedRun为混合场景socket原始结果，MixedInputReview为独立输入/期望和全字段复核。SDK及Host对象图仍为私有内存/本地连接，未覆盖实际HID completion或Steam消费。
