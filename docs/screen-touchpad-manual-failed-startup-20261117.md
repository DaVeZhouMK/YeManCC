# 干净失败启动的显式设置恢复

已写入主线，未导出部署。只改输入通道与触摸板配置消费两个产品头，共4处上下文；正式fixture、main/前端/监控/Steam/公共睡眠未改，无新timer/thread/poller/刷新率或等待。start本体逐字不变，不增加每次自动绑定的尝试，也不重置3repair/3episode/5秒窗口。

真实复现：原delegate new16B仅失败一次后，独立启动已干净退出，State.closed=true且queue/action/completion全空；自动刷新仍保留同一失败State（原边界），但显式重选同一归一设置也继续复用它，输入无法恢复。最终旧对照仅“显式同设置恢复”检查必要FAIL，其余前置及退出清理通过。OOM是自有可控故障，不当Win5原E_UNEXPECTED根因。

现State在发布控制窗口/ready前记startupPublished。仅原revealFromSettings=true、master开启的native candidate路径，才可调用退休辅助；必须从未发布、clean closed、错误失败、原thread已signaled，才由原shutdown归还失败空启动。随后原refresh建立一个新State，配置及自动预算保持。自动main configure/refresh消费默认false，start不自动清理重试；健康及已发布通道拒绝此辅助，仍沿原有恢复。若旧thread尚未完成会拒绝，之后再次显式选择即可重试，不增加等待。

手动恢复最终11检查全过，真实原owner configure/refresh、单次原C++分配失败、自动不重试、显式同设置恢复一次、健康不替换、已发布退场不按空启动重试、配置/预算和退出释放均核。原IPC active-profile set只有其现有入口传true，自动刷新/原frame消费用默认false；仅只读源码链，不冒真实IPC或设置文件写。

同一最终产品字节四个原消费者组：严格quota11、复位20、切换13、邮箱17过，非一次默认父门/完整Native。8实际Job全关前0，原root/business/自然与工具状态分列。初始公开HRESULT过严的前置假设FAIL保：State确为EOOM，但原Surface catch-all公开E_FAIL，修fixture只要求State真实EOOM+窗口非零，不改产品错码或把现场E_FAIL归OOM。第二旧对照有效，第三仅强化自动main消费对照；全部原件保留。另prepare wx拒绝复制文件覆盖/后续缺runner为编译前失败，0Native，修原未测副本后续行原预算。

2target+21dep全部share0同持144.6098ms/23HANDLE全还；4块整源逆补和导后23源全等实跑候选，私有friend+arm严格逆除，原consumer CPP/PS1未写。独审约158s超120目标但180内，范围内无阻塞，未替父核测试。声明只覆盖clean never-published startup的显式恢复；不是已发布故障的第四次自动修复，也不是任何Windows永久失败均能恢复。

权威：VALIDATION-INTAKE.json、SETTINGS-ENTRY-BOUNDARY.json、REVIEW-MANUAL-STARTUP.json、SOURCE-REVIEW.json、MAINLINE-IMPORT.json、POST-IMPORT-READBACK.json、SOURCE_READY.json、CLOSING.json。下一自然匹配Native/runtime/source-PDB；其他Windows queue异常、实际Win5新链loaded/全屏触摸捕获、原Windows错误与长期CPU仍OPEN，持续目标ACTIVE。
