# 共同刷新屏蔽责任回归

实际生产消费者是main.cpp的hidHideRefreshPhysicalHide；夹具逐字提取该函数、实际journal结构和路径辅助函数，只把发现/驱动/日志末端设为显式假实现。覆盖prehidden不认领、命令回执丢失仍保留已尝试责任、派发前记账、大小写去重、批次未执行尾部、已有owned条目及原拒绝门/回读次数。共74项。

运行入口：`node tools/common_visibility_ownership_selftest.mjs --out <新的绝对证据目录> --toolchain <已有捕获工具链目录>`。工具链目录必须包含实际build-environment.json和toolchain-manifest.json；只读复用该快照，不依赖某一个固定B盘证据路径。现有默认native诊断目录具备这两个文件。

输出保留真实编译输入、编译器身份、原始源码与提取函数、生成物和执行回执；执行前核验静态DLL闭包只含KERNEL32。CRT动态加载导入不等于运行期模块追踪，不能据此宣称实机/驱动验收。夹具不编译设备后端，不执行YMCC或普通Host。

修改刷新责任逻辑时须更新本夹具并复跑；它不替代完整native/managed切换链、原始预算/所有权负向门或实际SDK/PnP验收。原先的20项红例及中间结果保留在主任务100证据中。
