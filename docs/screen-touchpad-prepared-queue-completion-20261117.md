# 输入队列关闭回调提前准备

已写入主线，未导出部署。仅原末级独立输入通道的一个产品头，5处上下文；正式测试CPP/PS1、普通UI/监控/Steam/公共睡眠、原启动/命令/join等待与3repair/3episode/5秒窗口保持，无新增poll/timer/thread/刷新率。

针对独审OC-OLD-01验证一个具体可达分支：原SDK AsyncActionCompletedHandler(lambda)经make_delegate执行真实new delegate。本轮仅在该原表达式前启用当前worker线程的operator new一次失败，实际命中16B。旧代码已启动队列，关闭时抛bad_alloc后worker退出，State仍持queue/action，closed=false且原stateError=0；负例明确保留未归还COM State到测试进程退出，不能当资源清理通过。

现把同一完成回调创建提前到CreateDispatcherQueueController之前，State只持既有handler，关闭阶段直接复用，自己的handler引用在原STA清空。lambda仅捕获原worker DWORD，无State/COM循环捕获；总共仍是一处原16B分配，提前时间而非降低实机CPU的声明。该分配失败时线程退出，queue/action均未创建，start返回E_OUTOFMEMORY，空资源shutdown成功。不要把“启动未建资源后的干净关闭”混同于“失败清理被第二次关闭改绿”。

原真实失败参数5检查（4业务要求失败）作为必要负例；候选同一注入参数5过，正常参数5过。四个原始消费者组在同一最终产品字节上实跑：严格quota11、完整复位20、切换交错13、邮箱溢出17均过。原源码从源符号与实际646/相应图输入核同，私有friend+arm语句严格逆除等产品。源SDK未改，未替换HRESULT，也未发送实际设备/OS输入。不是完整Native、默认父门或Win5实体睡眠/全屏触摸验收。

7个真实owned Job关闭前0，原root/business/自然与工具收尾分别保留，旧负例的COM债未因进程归零改绿。首次raw runner保留无关缺失输入pin在启动前拒绝，0compiler/Native；仅新r2删除无用pin，原准备与费用保持。独审125秒超120目标但180内，未发现新增范围内阻塞；其输入pin与最终导入整头核同。

1target+22dep全部share0同持73.299ms/23HANDLE全还，整头5块正逆与导后23源同最终实跑。0正式fixture写/新ID/默认门；前向接OwnedInputCompositionRetirement的channel，其余原资源隔离/信号释放修补保持，下一自然匹配Native/runtime/source-PDB。没有为收口重跑已过的本轮选择。

严格保留范围：ShutdownQueueAsync、Completed ABI登记、Status、GetResults的其他Windows异常仍OPEN，本轮未给所有永久OS失败出具最终STA资源归还保证。旧Win5 0x8000FFFF的Windows内部原因、新链loaded及实体全屏/触摸捕获、长期CPU也仍OPEN。持续目标ACTIVE。

权威：VALIDATION-INTAKE.json、SDK-ALLOCATION-SOURCE.json、REVIEW-PREPARED-COMPLETION.json、SOURCE-REVIEW.json、MAINLINE-IMPORT.json、POST-IMPORT-READBACK.json、SOURCE_READY.json、CLOSING.json。
