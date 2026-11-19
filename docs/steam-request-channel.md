<!-- STEAM-RUNNING-COMMAND-COORDINATION-r254 -->
运行中 Steam 的命令占位由普通 Native 主进程的一份 `steamcommand::Coordinator` 管理。手动 set/stop/show、已安装库 Shutdown、GamingHome Show/Focus、主进程 CDP 写操作和原恢复 child 共用占位；同一请求显式借用，独立新请求不得借别人的票。占位不授予身份、动作权限或成功回执。Busy 明确未派发，本次调用结束，不作身份变化、恢复失败或自动重试依据。含 `menuGame` 的潜在别名写也参与协调；原轻量 gamepad-lease 独立通道和只读事实保持。

恢复在原内存／持久化预约之前取得占位，Runtime 在原 closeOwned／层级推进之前取得；大屏关闭、读回、打开、读回沿同一个父事务。实际副作用仍在原调用／认证同映像 child，原 source、process、user、session、account、power、current、deadline 和最终一次消耗性准入保持。动作与事实共用原认证管道的一份 reader 和连续序号，事实回复不授动作权限。取消、未知许可或关闭失败不凭超时抹掉准确 child 债；后续原入口可在已有事实执行器请求原清理，准确归还才退休，无新增常驻线程、轮询、服务或 EXE。

本能力证据范围为私有原消费者的定向组件与对象编译，不等于默认／完整 Native、安装版、实体 Steam UI／手柄／触摸／睡眠或 CPU 收益。BAT 和提前启动主进程的拓扑按用户决定继续待定，不能据本能力宣称全部开机路线或全图唯一出口已验收。原失败、分 epoch 回执与未覆盖项保留；仅下一次自然构建配套，不追包部署。
<!-- /STEAM-RUNNING-COMMAND-COORDINATION-r254 -->

<!-- STEAM-MENU-PURE-FEATURE-OWNER-20261115 -->
2026-11-15 菜单原纯game/Host/power/foreground归属已接入唯一公共steamLiveRequest第四可选ownerCurrent，原nativeCurrent逐门合取且异常拒绝。旧第三参admitAction仍只交原最终发送准入，不重复消费；qualified初次及原350ms/4秒同请求接续传同纯谓词，fallback/read/dispose及空第四参旧语义保持。无新增reader、worker、timer、焦点操作或一般Ready要求。
7配套文件11精确hunk含1新支持文件已鲜读排他导入，143.2304ms/7HANDLE归还；main只核所改函数，保并行字节和换行。原8项6通过2回执字段期待FAIL保留，仅续原2项4检查；实际旧主函数首归属断言FAIL，原native尾部仅2场景，非法/空选择4条拒绝。4实际编译图、13有终态Job关前0已核，准确工具收尾/raw businessSucceeded=false分列；另原包装器准备拒绝无终态单列，不拼默认父。
权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/MenuBrowserViewBinding-20261115/pure-feature-owner/{SOURCE_READY.json,FINAL-INTAKE.json,fresh-cas/MAINLINE-IMPORT.json,independent-review-r117/REVIEW.json,SEAL-HISTORICAL-PATH-CORRECTION.json,REPORT.zh-CN.md}。SOURCE_READY4243B/SHAa12c81b3389981d296b24707dfdee7eb4fd0bdbd2d6ba3144f8c63ec3af37848，仅此软件守卫/globalSourceApprovalGranted=false；审查before历史路径现用原保存副本逐字核，原核收FAIL保留不重跑业务。PendingSources/SteamMenuPureFeatureOwner.json只交下一自然Native/runtime/source-PDB，不追180522/155943。有限BrowserView provider/异步资源仍私有，ROG三点pad/touch/游戏抑制/关闭返回及睡眠、全消费/V2/长期CPU均OPEN，总目标ACTIVE；无本后继构建部署/loaded/实体证明。
<!-- /STEAM-MENU-PURE-FEATURE-OWNER-20261115 -->

<!-- STEAM-ORDINARY-MONITOR-SHARED-IDENTITY-20261115 -->
2026-11-15 普通steamMonitorSet及原160ms队列Live attempt改消费公共immutable IdentitySnapshot，await贯穿同epoch/PIDbirth/account/root和原可读非零注册PID门。Unknown不当offline、不借目录fallback换账户；未新reader/worker/queue/poller。原GET/bootApplyOwned/queue正文不改，offline三freshPresence/两freshTarget及backup/原文CAS/atomic/readback逐字保持。
Live提交前原设置锁内完整预像CAS消费pendingfalse，终态保存失败不复活。重挂pending或Live成功终态仍在原锁核同epoch；失效后false/unknown终结允许，旧完成不覆盖后继用户意图。普通整文档保存保native monitor整值/absence，正常backup读写及实际bounded恢复仅降SteamMonitor许可/proof，正式restartRequiredfalse、未知restart/desired/account/path及其它模块保持。
8配套文件25hunk freshWindows share0正逆导入201.2971ms，8HANDLE归还；main保并行字节。原ordinary29/133与锁/离线7/27各51→51分epoch，不拼36父；root原8/71仅opaque精确保留范围，r3错误restart字段1/41及childSourceReady已拒，父formal1/50和actualbounded1/24及各必要旧源FAIL分列。49Job关前0=29自然/19限定vctip/1准备空Job拒绝，19实际图核同，不冒称全自然或全业务父通过。
权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/RemainingReaders/MonitorOrdinaryWrite-20261115/{SOURCE_READY.json,COMBINED-INTAKE.json,FINAL-INTAKE.json,COMBINED-REVERSE-REVIEW.json,combined-import/MAINLINE-IMPORT.json,REPORT.zh-CN.md}。SOURCE_READY9463B/SHA6a957318ef0faa12d2148d69594a74563a40e62b3bbf441506e5db7f1f7afbae；PendingSources/SteamMonitorOrdinaryWriteSharedIdentity.json接续所选重叠源，其余不改。仅下一自然Native/runtime/source-PDB，不追固定180522/155943；普通default只编译，实际rootIO为原memoryport，无完整Native/部署/loaded/设置或Steam设备动作。全消费/V2/ROGpad-touch-sleep/长期CPU、HTTP会话寿命和同步watchdog并发close风险继续OPEN，总目标ACTIVE。
<!-- /STEAM-ORDINARY-MONITOR-SHARED-IDENTITY-20261115 -->

<!-- STEAM-DOCUMENT-REQUEST-PROCESS-HANDLES-20261115 -->
2026-11-15 公共document请求内只复用原held Steam父及所选listener子进程HANDLE，仍逐次查询child→parent映像、公共fresh拓扑、完整双栈表、exposed/端点上限与全部准入；bind后同对象，退出/未知拒绝，不回退PID重开。无跨请求cache、常驻reader/timer/worker。反推发现最后owner回调可撤进程，最终Wait已放回调之后，旧tail反例实际拒绝。

4配套文件14唯一上下文hunk fresh Windows share0正逆导入123.3289ms/4HANDLE归还。原r1 13/67及修后尾部3/17、两个必要旧源控制、popup1/14分epoch；不拼默认16整父。listener父预筛与84/600兼容子组实际执行，后者选择偏宽、重复成本保留，无新唯一覆盖。默认TU仅编译，普通整组未跑。21Job关前0=11自然/9限定vctip/1旧residual FAIL，原控制双头路径重定义、准备/干演算FAIL及费用均保留。8实际图与独立反推已核。

最终候选9950实际5docs/3类只读与原baseline对照OpenProcess/Close77→4、TCP调用152不变、image77→79实时，HTTP15/15全归还；耗时31/32ms不签CPU/速度改善。原57→197等句柄delta未解释，长期资源仍OPEN。身份连续仅child bind起，不追溯discovery；父映像项模拟查询失败，不冒签成功异路径。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/DocumentPopupSource-20261115/live-sp-provenance-20261115/request-process-witness/{SOURCE_READY.json,INTAKE.json,FINAL-INTAKE.json,REVERSE-REVIEW-r2.json,import-r2/MAINLINE-IMPORT.json,REPORT.zh-CN.md}。SOURCE_READY6718B/SHA74ecdf08bdd82406c1376fadd86d8016cd9bab256e9071541e72d66fa9195162，PendingSources/SteamDocumentRequestProcessHandles.json前向接续所选重叠来源。只下一自然Native/runtime/source-PDB，不追固定包，无本轮产品构建/部署/loaded证明。ROG三点pad/touch/游戏抑制/关闭返回、普通SET/V2/全消费/长期CPU仍OPEN，总目标ACTIVE。
<!-- /STEAM-DOCUMENT-REQUEST-PROCESS-HANDLES-20261115 -->

<!-- STEAM-DOCUMENT-RESOURCE-OBSERVATION-20261115 -->
2026-11-15 公共document固定4次同进程readStyles实际均5docs成功，31/16/16/15ms；cold57，等待后185/188/191/192，不能称plateau/纯一次初始化/产品泄漏原因。另两次真实OS叶旁账各15HINTERNET和77OpenProcess全部Close成功/remaining0/无失败，证明仅本有限执行的直接持有归还；未mock真实API或替换产品算法，不签进程内部kernel资源/长期无泄漏、CPU收益或ROG。
两个实际图456/355出现及各11库核同，4Job关前0=2业务自然+2准确PID/birth/path/SHA的声明vctip工具收尾；编译raw businessSucceeded=false保持。原57→197资源FAIL、准备文件名错和核收误要求工具自然的FAIL均保留，仅续原raw消费，不重跑4call。无产品修改/部署、新常驻reader或永久ID，旧ActualHref源码Ready/包要求不改。
权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/DocumentPopupSource-20261115/live-sp-provenance-20261115/handle-lifetime/{INTAKE.json,REPORT.zh-CN.md,lifetime.raw.json,direct-audit.raw.json}。下一成本候选为owned→stillOwned每次重新完整listeners及processImage；实际77open不等于已归因全部成本，须保原最终拓扑/双栈完整/loopback/暴露排除、PID birth/current/发送前后门后才优化。普通SET、V2全消费、ROG输入睡眠/主题生效/长期资源和CPU仍OPEN，整体ACTIVE。
<!-- /STEAM-DOCUMENT-RESOURCE-OBSERVATION-20261115 -->

<!-- STEAM-DOCUMENT-ACTUAL-HREF-20261115 -->
2026-11-15 实际held Steam/owned CDP来源证明target列表保creation locator而页面已导航：SP实际Steam index，QAM实际routes/apprunning。最终公共reader只对原严格SP/已识别popup locator接受已验证actualSteam href；原aux exactlocator兼容，非Steam/任意blank不放行。精确origin、初末actualhref/timeOrigin、process/listener/current/finaltarget集合保持，sourceUrl返回actualhref；r104候选parser/expression/evaluate逐字保持，无额外eval/reader/worker/timer或写入。

2文件4上下文hunk fresh Windows share0正逆合入41.7026ms，两句柄归还。原20case102与popup1/14按r1保，实际r1 readStyles在导航QAM失败后仅续aux6/32及r1必要反例，不拼26整父/不重跑r104形状组。最终候选一次实际readStyles在9950读到5文档/3类且ok/complete/sequential；只查无害property，不签CSS生效/完整main power组合或ROG菜单实体输入睡眠。

source-fix17自然Job0/7实际图已核；另原provenance2Job原件独立核。原采样exit1/handles57→197资源FAIL及后续135/138/142句柄delta保诊断，长期资源闭合不签，Job0不当无泄漏。原r1实际失败、诊断摘要误标SUCCEEDED及前向纠正、准备路径/inline提取/whole友元witness/未声明control编译等FAIL保留，仅续未到达控制或元数据，不重跑原业务。Observer privatefriend逆除全字节等产品，不导入adapter。独立源审184s/150s、r2窄审71s/115s区间与现场核收401s/180s成本如实保持，不双计父子重叠。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/DocumentPopupSource-20261115/live-sp-provenance-20261115/{LIVE-INTAKE.json,source-fix/SOURCE_READY.json,source-fix/FINAL-INTAKE.json,source-fix/PARENT-INTAKE.json,source-fix/import-r2/MAINLINE-IMPORT.json,source-fix/REPORT.zh-CN.md}；SOURCE_READY4970B/SHA97e8cc891e3f3f2aa565682f9cc097c378fb911f4d7104816346861cd1a4eb09。PendingSources/SteamDocumentActualHref.json后继r104及targetdiagnostic两个重叠源，其他4文件/main诊断保持。只下一自然Native/runtime/source-PDB，不追155943/134733、不部署；全消费/V2资源/物理/CPU仍OPEN，总目标ACTIVE。
<!-- /STEAM-DOCUMENT-ACTUAL-HREF-20261115 -->

<!-- STEAM-BOOT-MONITOR-APPLY-SHARED-IDENTITY-20261115 -->
2026-11-15 boot-mode监控ApplyOwned已消费同一Observer身份和epoch，原boot current、文件目标重选、公共Live最终admission及SET→单次GET确认保持。消费自身不再private registry/PID/fresh全表；admission仍可读文件/boot归属，不是纯ownerCurrent；受控重复gate不是实机调用频率或CPU测量。无新reader/poller/worker/queue/retry，原GET/普通SET/pending/offline写门正文逐字保留。

独立审查抓到r1漏注册PID原门，r1未导入；现核同发布registeredPid可读非零且由epoch覆全程，不新加等于verified PID策略。原35case200check与旧birth反例保留，修后只4case26check及r1零注册实际反例，不拼39整父。7实际图、17实际Job均自然关前0，默认/read两类型按r1保留，仅修后受影响默认类型续验；原35/旧20/默认业务不重跑。审查约389s对240s目标的超时成本与修后105.5s独立记录，旧BLOCKED/FAIL/UNKNOWN不改。

1产品4配套5文件25hunk fresh Windows share0正逆导入60.7337ms，5句柄归还，全源等最后候选；官方owned prepare修后21输入，原read准备20/类型配套。旧MonitorLive Pending四个重叠源由SteamMonitorOwnedApplySharedIdentity.json前向后继，旧SOURCE_READY/raw保历史。权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/RemainingReaders/MonitorOwnedApply-20261115/{SOURCE_READY.json,FINAL-INTAKE.json,REPAIR-INTAKE.json,import/MAINLINE-IMPORT.json,review/REVIEW.json,review/REPAIR-REVIEW.json,REPORT.zh-CN.md}。SOURCE_READY4810B/SHAe18f3f4372a5402c475c246e8fcbac0331449bf47a38ffbcdbc1b2132b1abf44。

只下一自然匹配Native/runtime/source-PDB，不追155943/134733，无本轮编包部署或Steam游戏设备睡眠动作。实际producer未发布OS变化/同epoch文件对象/最终使用竞争、完整boot handler/全消费/V2资源、ROG菜单实体输入睡眠和CPU收益仍OPEN，总目标ACTIVE。
<!-- /STEAM-BOOT-MONITOR-APPLY-SHARED-IDENTITY-20261115 -->

<!-- STEAM-MONITOR-LIVE-SHARED-IDENTITY-20261115 -->
2026-11-15 监控GET的Live消费已捕获唯一Observer身份，root/account/presence及PID birth来自同发布；原配置路径两次选择保持，同epoch predicate交原公共Live，读前/后/最终路径后核失效。unknown不当offline，只请求原refresh。GET不执行pending，原SET/Apply/boot/offline最终fresh写门保持；无新reader/poller/worker。原消费两次presence、两次PID注册表及两次私有config发现归零，公共CDP仍有原identity/discovery，未签CPU百分比或整个GET零OS查询。

1产品3配套4文件13精确hunk已fresh Windows share0正逆导入121.7321ms，4句柄归还；当前全文件同候选。原实际getter+真实Observer发布20case99check及handles51→51、旧PID birth首必要FAIL/非法selector、正式prepare/现存/冲突拒绝已核。默认memory叶仅配类型，原完整CPP+实际main kick /Zs通过，默认业务不跑。三图307/307/298出现，9/9/0库，9实际Job全部自然关前0。

旧LF锚准备FAIL与MJS严格原字节核收FAIL保留；后继只恢复原物理LF，正式执行CRLF副本/旧MJS与旧CPP作历史来源，未重跑业务。独立审查无可证新阻断，约180s目标实际215.59s/超35.59s保留；初始文件展示跨代归属、同epoch文件对象/未发布OS变化、最后检查后使用及整App/实际transport不扩签。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/RemainingReaders/MonitorLiveRead-20261115/{SOURCE_READY.json,FINAL-INTAKE.json,PARENT-INTAKE.json,import/MAINLINE-IMPORT.json,review/REVIEW.json,REPORT.zh-CN.md}。SOURCE_READY4307B/SHAdd6a64d83cf0039fac79bbeade7130cc3fa5700027477be4e1528cfa87a2a49a。只进下一自然匹配Native/runtime/source-PDB；134733与155943均不含，不追编部署。菜单155943已由原任务交付/9950 loaded，ROG alias实体pad/touch仍OPEN；聊天工具-32000未冒称送达，PendingSources已留共享回执。全消费/V2资源/实机睡眠与CPU收益仍OPEN，总目标ACTIVE。
<!-- /STEAM-MONITOR-LIVE-SHARED-IDENTITY-20261115 -->

<!-- STEAM-DOCUMENT-TARGET-SOURCE-DIAGNOSTICS-20261115 -->
2026-11-15 公共document-source-unconfirmed只补原targets→readStyles失败回执→main finish→Decky过滤的targetSource固定7字段。记录SP/MainMenu/QuickAccess、discovery/final-targets、固定urlClass、截断4097长度及前4096字符的query/fragment布尔；无raw URL/账户/路径/CSS。旧reason、发现终止、来源/继承origin/精确href/timeOrigin/current/final-targets保持，无新reader/worker/poller/retry或blank放宽。

3产品+3既有夹具6文件14hunk已fresh Windows share0正逆精确合入，109.2114ms；main并行后继保留且所测共享函数逐字相同。原producer8case80检查、实际main到日志22检查、必要旧源首1caseFAIL与unknown selector拒绝已核。main测试仅将document read叶替换为原producer首项回执；实际Observer发布正文保持、friend仅私有夹具适配，不签整App/真正CDP。

旧generic IPv6 mock返回ERROR_NO_DATA导致首2检查FAIL保留，现仅修成功空表size/header；consumer driver缺advapi32链接FAIL保留，原COFF对象仅重链，未重跑已过8case或重编该对象。4实际图/10Job关前0分5自然与5精确vctip工具收尾，原businessSucceeded=false保持；所有准备/原件/费用不重置。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/DocumentPopupSource-20261115/target-source-diagnostics/{SOURCE_READY.json,FINAL-INTAKE.json,PARENT-INTAKE.json,import/MAINLINE-IMPORT.json,REPORT.zh-CN.md}；SOURCE_READY8635B/SHA58df08834d1f3242e9350bdb2505174e29be2936c630b33d286b765ab962b140。旧Popup/Listener的3+1重叠所选文件由新PendingSources明确后继，旧SOURCE_READY/raw不改。下一自然匹配Native/source/PDB，不追固定134733。该现场实际来源拒绝/完整gameID原生恢复/实体三点pad-touch-sleep/全构建与CPU收益仍OPEN，总目标ACTIVE。
<!-- /STEAM-DOCUMENT-TARGET-SOURCE-DIAGNOSTICS-20261115 -->

<!-- STEAM-LISTENER-COMPLETE-TABLES-20261115 -->
2026-11-15 公共listeners两族TCP表必须size probe/实际成功/实际size与count边界都完整后才进入原process映像授权；不完整兄弟表不能视为缺席。原拓扑/映像顺序/loopback/外露排除/16端点/stillOwned及transport后缀保持，无新增reader/轮询/重试，正常仍4次TCP调用，两表同时最多32MiB，不宣称CPU收益。

3配套文件6hunk已fresh Windows share0正逆精确导入，120.0352ms，保留并行字节。原84case600检查与旧header仅安全第二族denied反例、原兼容12/68、6selector保留；CREATE_NEW输出后继只续原首1/6及原文件拒绝，不拼新84整父。3实际图各385 include出现/351唯一文件/9库，原r3图错指前置receipt由实际编译参数/deps/link stdout前向核正。17自有Job关前0，15actual/2前置UNKNOWN；14自然与1精确vctip工具收尾分列，原raw businessSucceeded=false不改。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/ListenerCompleteTables-20261115/{SOURCE_READY.json,FINAL-INTAKE.json,PARENT-INTAKE.json,import/MAINLINE-IMPORT.json,REPORT.zh-CN.md}。SOURCE_READY3029B/SHA045033218e022a01dc40d91027d2ea8c03b2b15e4c4ef08bd947aa0128f413e5。原准备/编译/selector前置/后置reader与两次父核收失败、恢复间隙和原费用均保留；同raw核收，无业务/编译重跑。

下一自然匹配Native/source/PDB，固定134733不含且不追编；未部署/loaded/物理pad-touch-sleep/CPU验收。当前134733 document-source-unconfirmed与游戏身份/输入交接仍OPEN，globalSourceApprovalGranted=false，总目标ACTIVE。
<!-- /STEAM-LISTENER-COMPLETE-TABLES-20261115 -->

<!-- STEAM-DOCUMENT-AUXILIARY-POPUP-SOURCE-20261115 -->
2026-11-15 共享主题document读取仅补MainMenu/QuickAccess固定about:blank?browserviewpopup=1&requestid=<1..20ASCII数字>&parentpopup=<1..20ASCII数字>原顺序兼容，原裸blank保持，BigPicture不接受blank。完整expression/receipt/evaluate/readStyles原后缀逐字不变：真实origin、exact href/timeOrigin/ready、Steam/listener/loopback目标、取消及最终targets仍严格，无新reader/worker/timer或权限。

3文件9hunk已fresh Windows share0逆向精确合入，117.5608ms保并行。原68case404检查、旧header popup-three在document-source-unconfirmed必要FAIL、5selector拒绝；补guard后仅原popup-three1/14实跑，不拼新68整父。非document完整Windows叶/Od、/Zc:inline-、/Gy-及/OPT:NOREF实际链接，非整个默认TU/业务。4实际图384/384/308/384输入、9/9/10/9库、13自有Job关前0，准确vctip工具收尾分列。旧CPP来源由原preguard副本41ded810回核，原raw不覆盖；首compile/父reader字段FAIL、恢复中断与所有费用保留。

权威SteamUnifiedChannel-20261112/DocumentPopupSource-20261115/{SOURCE_READY.json,FINAL-INTAKE.json,PARENT-INTAKE.json,FIELD-SOURCE-INTAKE.json,import/MAINLINE-IMPORT.json,REPORT.zh-CN.md}，SOURCE_READY2963B/SHAe7af36b62434bb3d24bf35886337daf88c95d904628c0c0f12e85c04fa19a863。36条官方创建URL逐行SHA核同、20+5次原document-source-unconfirmed已核，但创建URL不是live CDP href/origin，现场完整原因/CSS成功仍OPEN。

下一自然匹配Native/source包，固定122321不含不追编，未部署/loaded/实体pad/touch/sleep或CPU证明，globalSourceApprovalGranted=false，总目标ACTIVE。
<!-- /STEAM-DOCUMENT-AUXILIARY-POPUP-SOURCE-20261115 -->

<!-- STEAM-LIBRARY-122321-SELECTED-PACKAGE-20261115 -->
2026-11-15 库冷启动13文件后继已进入自然122321完整Native/Host/Worker与匹配source包。所选13源码ZIP成员实际解码CRC/字节/SHA等原import after，主Native与Host/Worker三runtime成员内容全SHA匹配；Host52/Worker32原sourceIndex包含共用A锁header。此前“下一自然包”仅就这13来源前向关闭，111602缺失/历史原件保留，不宣称更晚并行源皆入包。

父权威LibraryColdStartAdmission-20261115/PACKAGE122321-SELECTED-INTAKE.json9237B/SHA30e1c176500ec86e8403fd459a3cda416db738bb971da7417df21202adc5f240；原SOURCE_READY9300B/36040866与import25hunk及全部FAIL/费用保持。两个ZIP全SHA实际核同：runtime3eb28f5b01db96dc77c8ecdd758c76c55a2ffdf1358d5ffdc80682d261b1e94c、source88e5b149a3b3295db89584717dea3addad2c72ad9e61d434ae5c92ca3523db38；Native ce096c0d57b2399b47715208a15c3771f37717208439f7de42f44fb2fb62b340，compiled8b3366e7050cd7360dcefed2587f4d4ebd6415fc311afbd098b1dd1c082ec286。

原producer1514include/55lib与完整10081source-PDB归原producer证明范围，父未重核其全闭包。root0/Job0但EXITED_WITH_TOOL_CLEANUP/businessSucceeded=false，准确vctip工具收尾分列，不称整Job自然。没有业务/编译重跑、部署/loaded/实体证明，fixture NOT_RUN。稍后ModeLocalContention两产品头与包内旧SHA实际不同，明确不含该后继，继续下一自然包不追编。所有实体pad/touch/sleep/CPU及完整公共进出目标仍ACTIVE。
<!-- /STEAM-LIBRARY-122321-SELECTED-PACKAGE-20261115 -->

<!-- STEAM-MODE-LOCAL-CONTENTION-RETENTION-20261115 -->
2026-11-15 原生readMutex try_lock miss携精确localReadBusy pending7字段；原ModeObserver仅same session/no in-flight invalidation/已有未撤source时保原凭据，cached/delivered仍pending unknown，无新健康/账户/启动许可。原renderer/CDP live-busy无native标记继续invalid撤销，真实hint/进程/账户/demand/退出/owned取消保持；原有限read-window预算和常驻工作不变。

2产品头+7原canonical/支持共9文件13hunk已fresh Windows share0精确合入，逆补保其它字节，138.606ms窗口；当前源等实际执行私有来源。原CPP定向7case335检查、旧Observer首保持断言必要FAIL、未知选择器拒绝、2图各312输入9库与5自然Job0独立核收。真实完整read只适配Observer类型，两线程/native mutex与原OS事件实跑，identity/CDP为叶，不签全main。派生raw字节核同；原steam.shared-mode-source增加freshness检查与strict选择，后继1正3负source-only新Job自然0，不重编不重跑7case。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/ModeLocalContention-20261115/{SOURCE_READY.json,FINAL-INTAKE.json,PARENT-FIXTURE-INTAKE.json,SOURCE-RESULT.json,import/MAINLINE-IMPORT.json,REPORT.zh-CN.md}，SOURCE_READY5555B/SHA3474b14ff9c259bfc6196fc4ea8e34f6f6b0d890a6795e901604c78241e886f1。原worker494855ms及其r2零Job/NOT_RUN、70089ms越界、各准备FAIL与父首次过期额度前置拒绝全部保留，原账不重置。父确认0handle接管source-only3.86秒；所有编译/业务旧原件不覆盖。

仅所选SOURCE_READY，globalSourceApprovalGranted=false；下一自然Native/匹配source包，不追已固定包或部署。完整V2/资源/默认父/主产品编译链接、真实loaded/三点pad/touch/sleep/长期CPU收益仍OPEN，总目标ACTIVE。
<!-- /STEAM-MODE-LOCAL-CONTENTION-RETENTION-20261115 -->

<!-- STEAM-LIBRARY-COLD-START-PAIRED-ADMISSION-20261115 -->
2026-11-15 公共冷启动与库离线写入已配套导入13文件/25hunk：原shortcut-writer Mutex A由共用非拷贝steamoffline::Guard持有，primary创建suspended child前到resume/处置后，fallback最终absence/Shell前取A；Host/native写与回滚A→原data事务B，B后A归还再媒体/快照，Worker保原10秒/恢复0秒。abandoned只授互斥，无新reader/poller/worker/电源权威。

当前13字节、严格11唯一raw source pins和4未改canonical来源独立核同；原Kernel7/37、clean NoIO3/30、membership14、Host26+9/Worker4和delete-unknown后继保持各自epoch，不拼默认整父。canonical既有PID隔离经transitive头确认，无新增夹具补丁；Host仅/Zs、Worker原竞争正文实际执行。误判隔离、过严新文件断言、编译/reader FAIL及原UNKNOWN/费用全部保留；build-guard历史前像由逆hunk证明，不误读当前路径作旧来源。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryColdStartAdmission-20261115/{SOURCE_READY.json,FINAL-INTAKE.json,combined-import/MAINLINE-IMPORT.json,REPORT.zh-CN.md}；SOURCE_READY9300B/SHA36040866d6b76210a3fc5ce122e207e29fc13eb13c692e8e06ce6e1979db471e。fresh Windows share0导入167.6945ms保并行字节，本后置仅四索引精确增量与新头source-map登记。无业务/编译重跑或部署，globalSourceApprovalGranted=false。

固定111602不含该13文件后继，不追编；下一自然Native/Host/Worker/匹配source须含公共头。独立BAT/外部Steam启动者、完整V2/资源/默认父/全构建与loaded/实体三点pad/touch/sleep/CPU收益仍OPEN，总目标ACTIVE。
<!-- /STEAM-LIBRARY-COLD-START-PAIRED-ADMISSION-20261115 -->

<!-- STEAM-PUBLIC-111602-SELECTED-PACKAGE-INTAKE-20261115 -->
2026-11-15 新111602自然固定包所选源/Native已交付：LegacyStopSharedOutlet原5918B SOURCE_READY、当前UnusedSleepLocal 2778B SOURCE_READY及101730/focus-handoff 16828B SOURCE_READY均已进入此固定包。本段仅前向关闭这三selected source原nextNatural/current包缺口；旧094805、旧索引/SOURCE_READY/raw/FAIL/UNKNOWN及费用按原epoch保留，不宣称当前全部并行源已入包。父本地处理库gate配套由父方继续，本侧不重复。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/UnusedSleepLocal-20261115/PACKAGE111602-PUBLIC-INTAKE.json，6020B/SHA 6a19a821446eb06ec50550f601d3465b8f54e0067ca9d8bee20737210381339f；菜单同父FieldLogReview-20261115-101730/focus-handoff/matched-delivery/DELIVERY-SUCCESSOR.json，7607B/SHA 26d7bac382982921882399d23a46b113d15ec7d60652e22d080dd60af551923e。LegacyStop SR SHA 23a317f909bc8710f629670c59dd7e7be9eae76ce620820abe59fb7dfde634a8；UnusedSleepLocal SR SHA cdd841ee6832ef3d247e4657c2b7d46976377331385c5a0392d54013c9c0be5d；focus-handoff SR SHA 9f989df7b6859d89c7cf5b74ac56b74afc98daa3c4fc1c894ebf28b00d9d3cfb。

引用父原selected ZIP/Native/实际cdp include/10成员事实，不重检ZIP、producer或完整compiler图。Runtime ZIP 279059e905fb84e4cdd6f3685eea0742f73f66d824da53962f32e99937a2459a；source ZIP 911d3661b235bf5b98a65dfb731d5838dba495552949d7a3077bcff8a9ecce10；Native 272198e87cfe87704814e065ee7dfed23f09eb6a6e1ffd18420a2cb3520b732a；compiled 21c5e896c1e435da020eb77b655228e8dca833a1a9d8d278caf44569c2a4a2be。实际cdp为31808B/SHA 511c7d295f1608c2111a123477f03a12f8e222fe80d57c87e2c4fac353f2fc99；原完整220511B menuJS仅一处内嵌。PE/PDB及完整10078成员/compiler图保持原owner证据范围，本侧未重parse。

producer原root0/Job0，但EXITED_WITH_TOOL_CLEANUP、businessSucceeded=false、wholeJobNatural=false；准确vctip PID30764/birth134391862279250123/path/SHA限定工具收尾，不称整Job自然，不豁免产品。fixtures NOT_RUN，producer/业务/编译均未重跑，未部署。库gate私有后继明确不入111602，不改库产品或父Field分析。

LegacyStop无已知当前caller，入包不证明实际stop/完整退出或CPU收益。ROG物理三点pad/touch、loaded、sleep及完整唯一进出仍OPEN，总目标ACTIVE。仅四索引fresh Windows FileShare.None短窗口插入/JSON增量，重复marker拒绝，逆补还原全部其它fresh字节；证据同UnusedSleepLocal-20261115/package-index-111602/{IMPORT.json,REPORT.md}。本侧scope预算300000ms含准备封存，父r86原账不重置。
<!-- /STEAM-PUBLIC-111602-SELECTED-PACKAGE-INTAKE-20261115 -->

<!-- STEAM-PUBLIC-094805-MATCHED-PACKAGE-INTAKE-20261115 -->
2026-11-15 公共核收首个自然094805固定包：鼠标展示共享IdentitySnapshot与菜单params修复的所选Native/source配套已完成。此段前向取代这两项旧nextNatural/current包缺口；旧SOURCE_READY及索引原条目按原源码/测试epoch保留，不再把其“下一自然包”当成所选修复当前交付状态，也不宣称当前并行整源均入包。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/MouseDisplaySharedIdentity-20261115/ROG094805-PACKAGE-INTAKE.json，8472B/SHA ea38b1f157c086115d6a4ec62c1cca26b2e27eb1fb12c2764e16f86b934ccb81；本次核其原件及SourceReady/直接证据pins，引用原核收，不重查ZIP成员或producer。runtime ZIP 5fc7bbe4f699f7e5f290b5d2edd9fd1a84b031986e7e8415e32919d7490bdd74、source ZIP dfc0c2052bfd51a34e23d1c285529c461162e345cadf68c0c4d20b61db4bf2a2固定；Native 57fc181e326aac117445e332e0adb258ab00145e8707a014b13171e05520da8f、compiled 7f3abdcc15c1d811c1bcd9634185540e624548138730ea45e59ad0cacf93ac85。

原5 mouse支持+7 menu支持/产品selected成员CRC/SHA及main runtime b7f1ac4fc3463911dad51fd9419cc93bde6b1717efdf6d9d1b79e8397700d424、原Observer 639721f366174e36e3e5ce5fb4fdae110cddcc016881babcdd76ce08022b2d87绑定原Build.sources/compiled proof；完整209905B新menuJS在PE仅1处。PE/PDB只引用原菜单matched-delivery/INTAKE.json及DELIVERY-SUCCESSOR.json，本方未新parse；producer完整10077成员/完整compiler图仍限其原证据scope，不扩成本方重核。

原root0/Job0，但terminal=EXITED_WITH_TOOL_CLEANUP、businessSucceeded=false；仅原准确PID/birth/path/SHA的vctip工具限定收尾，不称整Job自然、不豁免产品。PACKAGE094805-SEAL-SUPPLEMENT.json保留原Native路径选择失败，原失败/费用与SOURCE_READY不改；源修复原6case/156检查及mouse24断言不重跑、不计本轮PASS。

固定包未部署，loaded/物理input/CPU未验；完整Observer/OS/transport与其它全消费仍OPEN，总目标ACTIVE。LegacyStopSharedOutlet-20261115后继不在094805，继续下一自然Web/匹配source，不追编固定包。本方仅四索引短暂FileShare.None fresh插入/逆向字节证明；封存同目录package-index-094805/IMPORT.json与REPORT.md。游戏库最终写/冷启动审查由父方负责，本方不重复。
<!-- /STEAM-PUBLIC-094805-MATCHED-PACKAGE-INTAKE-20261115 -->

<!-- STEAM-LEGACY-STOP-PUBLIC-OWNER-20261115 -->
2026-11-15 旧steamStop name-based taskkill出口已转交原steamUi.shutdown，签名必须精确displayed state+userInitiated:true；原手动/实例/ACK/错误统一准入，lazy import不新增顶层初始化边，无新reader/query/poller/worker/cache或fallback/replay。当前426源未见旧导出调用者，收敛残留能力，不冒称常驻CPU收益。正常运行中未知UI mode但精确实例仍由原stop规则决定，旧unbound running拒绝。

1产品1原fixture2文件3hunk fresh Windows share0正逆合入，整源同执行、原shared owner不变。实际legacy声明+完整steamUi/IPC-shell受控叶6case，旧名称kill控制在公共调用数0!=1拒绝，未知选择器业务前拒绝；3Job自然关前0。原default service/live组体逐字保持/未跑，无新ID/minimum；canonical原steam.ui-mode-service，draft误ID前向更正。实跑前VM dynamicimport/跨realm适配draft保留，独立review201.89秒超180费用照录，无可证新bug，不签完整app加载/类型/bundle/Native或实体退出CPU。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LegacyStopSharedOutlet-20261115/{SOURCE_READY.json,MAINLINE-IMPORT.json,FINAL-INTAKE.json,CANONICAL-ID-CORRECTION.json,review/review.json,review/SEAL.json,REPORT.zh-CN.md}。只交下一自然Web/匹配source，094805包不含，不追编部署或现场。全唯一进出/剩余BAT/V2资源与真实手柄触屏/sleep/CPU继续OPEN，总目标ACTIVE。
<!-- /STEAM-LEGACY-STOP-PUBLIC-OWNER-20261115 -->

<!-- STEAM-ROG091856-PUBLIC-PARAMS-INTAKE-20261115 -->
2026-11-15 公共核091856原33shared raw/ZIP及已声明availability仅内存SHA/CRC：running marker911eeb83绑定writer11072/birth134391787469183881，不只disk，也不签其它writer/所有loaded。30completed全accepted，当前19中Stalker10次typed proof与原Host inspection同game11644/birth134391789056253759、Steam16056/birth134391787622004429、Host1732/birth134391789145888995/browser1441797，logical/short/registered三域保持；Kitty4/无routing5及旧writer分列。公共history09:15:20.825为running[4064044731]/raw1/primary11644，但首菜单09:15:18.690更早，不倒授准入或代每call的UI running.length；09:17:20退出后root为Kitty。

九composition全sameTupleInputReassert=false/nativeRequestSubmitted=false且source tuple matched，队列[game,769]不是双游戏，不能称原生补交无效。固定官方sp/library解名证明ownData(params,'appid')不兼容observable getter，现场descriptor仍未知。菜单owner已修：只捕获原qualified registeredAppid primitive，原game.current live对象/值与single-game/identity/cache/once/uncertain保持，通用ownData不放宽。当前JS76b25745/H50720586和原fixture三来源同实际候选，正文嵌入只读逐字核、mode/light不变。

原候选6case156检查/旧源首getter模型FAIL前缀37独立、4Job自然关前0已核；921/922只为声明输入，实际dynamic closure未知，不拼默认父/完整Native。独立review291450/300000ms，首嵌入读取器把JS分号当声明结尾及后继误把before角色字段与after身份字段整对象比较FAIL前向保留，只读修消费者、无producer重跑；首次交互误出request环境由原review留事故，交付仅pins不复制值。原准备/post/强收封存成本保持。

原件FieldLogReview-20261115-091856/public-state-intake/{PUBLIC-STATE-INTAKE.json,PARAMS-SOURCE-INTAKE.json,REPORT.zh-CN.md,params-source-review/INTAKE-r2.json}及原params-admission/SOURCE_READY.json；专家原consultant-review/OBSERVABLE-PARAMS-CONTRACT-REVIEW.json。只交下一自然匹配Native/source，071202不含，未本轮构建部署/现场或重跑默认。物理手柄触屏/真实sleep/CPU及全消费继续OPEN，整体ACTIVE。
<!-- /STEAM-ROG091856-PUBLIC-PARAMS-INTAKE-20261115 -->

<!-- STEAM-MOUSE-DISPLAY-SHARED-IDENTITY-20261115 -->
2026-11-15 灵敏度只读展示已消费原唯一Steam Observer IdentitySnapshot：root/account/presence与registered PID均由同次快照，缺发布只请求原refresh；Unknown不选offline，文件/可选Live读取后用原sameIdentityEpoch撤掉旧展示。展示自身直接registry/process读0，不声称完整Live transport零读或实际Observer producer已组合。默认fresh target可完整重建，Set/Apply/Reconcile与最终VDF/Live门逐字不变；无虚拟手柄/陀螺仪/设备实现变化。

1产品5原配套6文件16hunk fresh Windows share0正逆合入，当前runtime等执行体，保并行main。原read-observation定向24断言/22标签（三种unknown共标签）、完整runtime/VDF及原IdentitySnapshot/current方法实际执行，OS/publication/Live受控叶；旧完整source在首私有读断言拒绝。两图各188输入7库，8Job关前0分5自然/3准确vctip工具收尾；旧receipt storm/default/整main编译链接及实体CPU未跑，无新ID/minimum。

首fixture缺namespace alias编译FAIL、import非main equality误用变量的元数据与seal消费者换行归一化FAIL均保原件，后两者只读前向纠正、无业务重跑。独立review无可证bug，限定已发布身份；不签同身份文件原子性/未发布外部变化。权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/MouseDisplaySharedIdentity-20261115/{SOURCE_READY.json,FINAL-INTAKE.json,MAINLINE-IMPORT.json,IMPORT-METADATA-CORRECTION.json,SEAL-SUPPLEMENT.json,REVERSE-REVIEW.json,REPORT.zh-CN.md}。SOURCE_READY 13442B/SHA8af5829388ebd29a8922332d8a9c141f085a1e503805e7fb62334d150b164101；下一自然Native/匹配source包，不追固定包，无本轮部署/现场。其余URI/BAT/V2资源/全消费与真实手柄触屏/睡眠/CPU实益继续OPEN，总目标ACTIVE。
<!-- /STEAM-MOUSE-DISPLAY-SHARED-IDENTITY-20261115 -->

<!-- STEAM-BOOT-BAT-FINAL-SETTINGS-SUBMISSION-20261115 -->
2026-11-15 独立BAT捕获相邻设置路径/选择模式/原preparation revision+observedBoot；原Local设置Mutex最多2000ms内重读核同请求，持锁实际Process.Start，日志/等待锁外。新选择或准备请求拒绝，普通theme/global/runtime/input ACK及同prep步骤推进保持，显式null不借generic；冷请求和迟到Show同门且失效停旧流。保无YMCC IPC的独立bootstrap、原flags/URI/观察，无新常驻reader/poller/worker。

1产品1原fixture共2文件10hunk share0鲜读正逆导入，整源等执行体；默认原Start AST只配套CaptureRequest，定向边界沿同flow-selftest.ps1 -CaseGroup入口，原run/default/ID/minimum不扩。r1原16/23有行为观察但父FAIL保留：惰性cmd带Steam参数留交互进程，本方Job强收cmd/conhost且关前0，6实际managed DLL漏before声明不倒填。r2仅惰性执行argv改有限exit并记录原argv/事前配套工具，只续healthy+late-show两项3断言，不重跑其它15；删锁/删request key各真实错误本方child启动1并被原断言拒。4Job=3后继自然+1原失败强收均关前0，837唯一来源/工具身份核收，3实际managed/module图，native-loader未知；独立审阅无可证阻断，不拼默认16父PASS。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/BootBatFinalAdmission-20261115/{SOURCE_READY.json,MAINLINE-IMPORT.json,FINAL-INTAKE.json,REVERSE-REVIEW.json,REPORT.zh-CN.md}。仅签配置→OS提交，legacy无prep同值ABA、跨提权/不遵锁写者、完整bootstrap/所有focus、standalone Native power/V2/共享Steam receipt/唯一observer与实体输入/睡眠/CPU仍OPEN。下一自然PowerControl/匹配source，不追固定包，无本轮部署/真实Steam游戏设备睡眠，整体ACTIVE。
<!-- /STEAM-BOOT-BAT-FINAL-SETTINGS-SUBMISSION-20261115 -->

<!-- STEAM-BOOT-SELECTED-DECLARATION-ORDER-20261115 -->
2026-11-15 CPU1完整私有main /Zs暴露BootSelected helper提前引用后方g_poolCancel，前轮片段夹具前置stub确实漏声明顺序，原SOURCE_READY/FAIL保历史。仅把同一原static atomic定义前移至g_exitRequested后，初值/load/store保持，2fresh share0精确hunk保并行；未新加取消变量或删门。
CPU1同一固定完整私有main带其FIFO/subset与匹配两头仅续/Zs exit0/55.014秒，1657 include appearances逐项预声明；本方核实际Execute.py AST调用+参数/source/tool/Job0及vctip限定工具收尾，当前六Steam main span/两头同源，整main后继不同不签全主线已编/link/包。原档案全哈及精确indexed provider只读核，缺原路径准备FAIL保留，无恢复大目录或重跑。
原BootSelectedLaunch-20261115/DeclarationOrder-20261115/{SOURCE_READY.json,TYPE-INTAKE.json,MAINLINE-IMPORT.json,REPORT.zh-CN.md}。下一自然Native/source配套，不追固定包，无新业务/部署或实机声明，总目标ACTIVE。
<!-- /STEAM-BOOT-SELECTED-DECLARATION-ORDER-20261115 -->

<!-- STEAM-SHORTCUT-APP-LAUNCH-20261115 -->
2026-11-15 手柄快捷动作app.launchQuickApp已消费原launchSteamAddon，明确Steam.exe保选中路径进原唯一steam.ui.launch/在途门，原checkpoint随入；ordinary程序和不透明脚本保shell boolean。helper返回boolean，Steam true只表示typed启动请求接受，不是进程Running/实体输入/新权限。原claim、one-shot去重、配置成员、power/ready、回执和释放保持，无Native输入/设备/其它action改动。

2产品2原配套4文件7hunk fresh Windows share0正逆导入、整源同执行，原coordinator不改。完整原runtime+原coordinator+公共launch桥内存叶定向8case：100重复请求去重、普通程序脚本/false失败、移除、电源变化、等待作废、共享在途、负/失ack均核；旧runtime单变控制在实际公共dispatch计数0!=1原断言拒绝，原普通addon仅受影响1项另核。三个独立Job均自然关前0；post stack列偏移消费期望FAIL保留，只消费同raw纠正，未重跑业务。raw.scope继承旧Vue通用标签，本方不据此签其它action/Vue整组；默认/native/Web整编/type与物理均NOT_RUN，无新ID/minimum/默认门。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/ShortcutSteamLaunch-20261115/{SOURCE_READY.json,MAINLINE-IMPORT.json,FINAL-INTAKE.json,REPORT.zh-CN.md}。下一自然Web/匹配源码包，不追固定包，无部署/现场。独立BAT最终配置→Process.Start窗口另只读确认，建议原Local设置Mutex锁内绑定同请求且保无YMCC bootstrap；未修/未实跑，不混本闭合。原RemainingChannelAudit-20261115/BAT-FINAL-ADMISSION-REVIEW-20261115.json。

剩余URI/generic消费者、未调用旧name-based stop export、BAT、完整V2/全资源及实体三点手柄触屏/睡眠/CPU实益仍OPEN，整体ACTIVE。
<!-- /STEAM-SHORTCUT-APP-LAUNCH-20261115 -->

<!-- STEAM-BOOT-SELECTED-LAUNCH-20261115 -->
2026-11-15 自动模式明确选中Steam.exe已接原公共启动事务，独立BootSelected来源/原receipt和40秒自动围栏，保plain空参数；不伪造userInitiated、不消费logon grant、不走manual fallback或联动级联。原设置锁核activeSnapshot和当前quickApps成员后调用唯一resume叶；原WindowsPorts最后fresh公共census后复核Native48 current，修实际census内电源撤销仍Resume漏洞。publication-only仍可继续；Kernel成功不被gate后false/异常抹掉，清理锁外。其它程序、独立GamingHome/BAT、Manual/Logon保持。

3产品9配套共12文件28精确hunk鲜读Windows share0正逆导入，6实际main span等执行体；整main有其它并行后继，首whole-hash封存拒绝原件保留，导入体由原fresh bytes+同hunk精确重建，不覆盖或签并行代码已编。原NoIO adapter未改，3源pins/新include依赖和manual-timed-ban两原mutation锚配套，无新ID/minimum/默认门。

backend31/116；实际WindowsPorts+main+原设置锁+Native48 r2为19/206、16自有child归还（4运行12拒绝终止）。最终r3仅续census撤销及publication-only各1/11，不拼默认整父；settings/power/census三实际错误Resume控制保FAIL。10实际图/485唯一path身份核收，19Job关前0=13自然+6准确vctip限定工具收尾。普通软件/FxSound generator仅type/object已核，原4个MD库预声明缺失FAIL保留不签完整link，216业务/默认NoIO/整产品均NOT_RUN，不为核收重跑。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/BootSelectedLaunch-20261115/{SOURCE_READY.json,MAINLINE-IMPORT.json,FINAL-INTAKE.json,REVERSE-REVIEW.json,REPORT.zh-CN.md}。下一自然Native/匹配源码包，不追固定包；本轮无部署或真实Steam游戏设备睡眠。用户联合开关游戏/部署新版YMCC/必要提权关闭YMCC授权已广播相关owner，不重复请示，现场由菜单任务协调；真实睡眠/设备/驱动另界。renderer预排队代次/完整V2资源/剩余URI及独立BAT最终动作/实体三点手柄触摸/睡眠与CPU实益仍OPEN，整体ACTIVE。
<!-- /STEAM-BOOT-SELECTED-LAUNCH-20261115 -->

<!-- STEAM-QUICKAPP-LAUNCH-20261115 -->
2026-11-15 快捷应用明确Steam.exe按钮已复用公共launchSteamAddon→steam.ui.launch，保原选中路径/空参数及跨入口在途门；普通程序和脚本保持plain动作。busy/未就绪/无条目零IO，拒绝/失ack不走generic重放；提示为“已发送启动请求”，不把回执当进程已就绪。

1产品2原fixture共8精确hunk持Windows排他句柄fresh正逆导入，文件同候选，模板/几何逐字不变。实际QuickApp launchApp AST+原公共桥6场景、旧view绕行控制已核；原browser mock现消费真实公共桥片段，build-only只编译实际页面，55实际文件/12虚拟输入闭包已核，无浏览器/默认UI组。首漏settings-baseline合同bundle FAIL/raw/费用保持，补缺后只续该bundle；4实际Job均自然关前0，不重跑已过6或旧控制。共享bridge/Native未改，前驱Native13/43与addon6只引用。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/QuickAppSteamLaunch-20261115/{SOURCE_READY.json,MAINLINE-IMPORT.json,REPORT.zh-CN.md}。下一自然Web配套包，不追旧包，无部署/现场Steam游戏设备睡眠。已存Steam URI、自动boot来源最终动作、独立BAT/V2/全资源及实体菜单输入/睡眠/CPU收益仍OPEN，整体ACTIVE；自动来源不能伪造userInitiated。
<!-- /STEAM-QUICKAPP-LAUNCH-20261115 -->

<!-- STEAM-SELECTED-ADDON-LAUNCH-20261115 -->
2026-11-15 Steam页自定义/固定联动明确选中Steam.exe时已接原steam.ui.launch，同一在途门与原Native receipt/最终absence。selectedExecutable只约束原安装源，变化拒绝而不替换；保空参数/不级联其它联动，原默认flags/addons和logon授权保持。任意脚本/快捷方式不猜内容，失败不借generic shell重放，无新扫描/registry reader/poller。

4产品2原fixture共17精确hunk持Windows排他句柄fresh导入，正逆核并行字节、完整文件同候选。前端6/原生13场景43断言及旧view/删path guard必要控制实跑；fixture只续受影响普通程序1及未知selector，不重算覆盖。两图499输入7库，8已准入Job关前零分2限定vctip/6自然；早期未准入UNKNOWN及UTF8/EOL准备拒绝/费用保持，空Job0不代业务。2原NoIO审查清单配套只推进3源pins，adapter/原断言保留，默认整组NOT_RUN；独立review无具体bug，不冒称实机。

权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SelectedAddonLaunch-20261115/{SOURCE_READY.json,MAINLINE-IMPORT.json,NOIO-SOURCE-REVIEW-SUCCESSOR.json,REPORT.zh-CN.md}。下一自然Web/Native配套包，不追旧包，本轮无部署/现场Steam游戏设备睡眠。QuickAppView显式启动、bootModes自动联动、独立BAT最终准入与完整V2/全资源/实体菜单输入/睡眠/CPU实益仍OPEN，整体ACTIVE；不得伪造自动来源userInitiated。
<!-- /STEAM-SELECTED-ADDON-LAUNCH-20261115 -->

<!-- STEAM-SOURCEJOB-SETUP-RETURN-20261115 -->
2026-11-15 SourceJob在State已创建后的Job/IOCP/SetInfo失败已接原finalize/retained账本；不再直接delete跳过关闭确认。原清理线程在子树/无child归还后先beginClose并结束临时继承窗口，再允许其它线程原reap。已seal Export短接原状态，避免重复Kernel继承位清除；原error字符串/取消/预算保持。

2产品3hunk持锁fresh配套导主线，整头等实跑候选。原9场景111断言通过：健康及4种create/setup错误clean/protected，错误返回受控但Job/IOCP/保护/Close/mutex真实；4次保原资源和租约、其它线程实际续归还。旧源必要控制与仅删beginClose单guard均实际拒绝，后者实证继承锁留原线程。3图410输入8库、6实际Job关前0，2编译限定vctip/4自然分列；前驱144不重跑、不拼跨epoch父PASS。wire/通用Handle/Imported与drain/自有close保持，无新产品thread/timer/poller。

唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SourceLeaseConsumerClosure-20261115/SetupOwnedReturn/{SOURCE_READY.json,REVERSE-REVIEW.json,RESULT-before.json,RESULT-guard-control.json,RESULT-candidate.json,REPORT.zh-CN.md}。临时诊断仍仅Evidence/Workspace，无新canonical/minimum/默认发布门。State分配前失败、Standalone Export/capture、Imported泛用句柄与双重inherit-clear/Close失败隔离仍OPEN；不签全资源、实机或CPU收益，整体ACTIVE。交下一自然Native/库匹配包，不追包/部署，无实际Steam游戏设备睡眠，普通导出fixtures NOT_RUN。
<!-- /STEAM-SOURCEJOB-SETUP-RETURN-20261115 -->

<!-- STEAM-COMPLETED-EXPORT-RETURN-20261115 -->
2026-11-15 公共Steam完成Export归还后继已导主线2产品3hunk：成功构造的SourceJob在原子树及自身句柄归还后，逐一确认三事件/provider/只读mapping五专用副本关闭。失败保同State/Export/SourceLease，原reap续归还；开始关闭及inheritance seal失败均sticky拒绝current，不新建worker/timer/reader，不重放业务。

真实受保护event旧源控制exit1保留；候选12场景144断言、2本方child自然退出，覆盖五slot分别失败、全保护分批归还、未seal早退、原root债再副本债与撤销后清理。两图各402源8库、4Job实际关前0，2编译准确vctip限定收尾与2业务自然分列。两头均逐字等实跑候选，wire/通用Handle/Imported及原Job drain/constructor保持；前驱53不重跑、不拼跨epoch父PASS。临时诊断仍仅Evidence/Workspace，无新canonical/minimum/默认发布门。

SOURCE_READY只签本完成/无child清理路径；Standalone Export/capture准备失败、SourceJob allocation/setup失败、子端Imported/通用Handle关闭债及inherit-clear与Close双失败时的继承隔离仍OPEN，本轮只注入真实PROTECT_FROM_CLOSE且核正常清继承位。正式原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SourceLeaseConsumerClosure-20261115/ExportOwnedReturn/{SOURCE_READY.json,REVERSE-REVIEW.json,RESULT-before.json,RESULT-candidate.json,REPORT.zh-CN.md}。

交下一自然匹配Native/库包，不追旧包，不把前驱历史头SHA当当前来源；全Steam统一进出/全资源/实机与CPU收益仍OPEN，整体ACTIVE。无部署或实际Steam游戏设备睡眠，普通导出fixtures NOT_RUN。
<!-- /STEAM-COMPLETED-EXPORT-RETURN-20261115 -->

<!-- STEAM-SOURCEJOB-OWNED-RETURN-20261115 -->
2026-11-15 公共Steam SourceJob关闭失败丢账已修：原drain成功后，member/thread/process/port/job各checkedClose，失败保同State/Export，原reap续归还；物理子树已归还标记独立于部分关闭，不再访问已关Job/port。1产品4hunkfresh排他导入，完整头等实跑候选；原drain/constructor字节保持，无新poller/timer/线程。

原真实保护Job旧源exit3保留，不当产品PASS；候选5场景53断言、3本方child自然退出，保护Job/退出process/退出thread的3失败同账保源，解保护后同reap实际关句柄并释放源。两图各402源8库，4Job实际关前0，2编译限定vctip收尾与2业务自然分列；临时诊断只Evidence/Workspace，无新canonical/minimum/默认发布门。

同时复读core12及壁纸11共23原Job：main factory确已安装，壁纸跨child/HANDLE_LIST原7/109证据存在，不将旧OPEN解释成未接线。当前core仅原NativePowerAccess逐字类型抽取后继，main只核原生命周期span。壁纸7/14文件当时同原件，其余后继分列；不借旧epoch签当前完整main/资源终态。

唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SourceLeaseConsumerClosure-20261115/{SOURCE_READY.json,EXISTING-SOURCE-INTAKE.json,PROBE-RESULT.json,FIX-RESULT.json,REPORT.zh-CN.md}。SOURCE_READY仅SourceJob自有句柄归还；Export专用句柄自身Close失败、constructor早期清理、全消费者/配套Native库包和实机仍OPEN，整体ACTIVE。下一自然匹配包消费，不追包；无部署/实际Steam游戏设备睡眠，CPU实益未知，普通导出fixtures NOT_RUN。
<!-- /STEAM-SOURCEJOB-OWNED-RETURN-20261115 -->

<!-- STEAM-PUBLIC-UI-READ-OWNER-20261115 -->
2026-11-15 普通Steam UI读取的取消接线已补主线：steam.uiState强制/无共享owner兜底及共享快照投递、showBigPicture内部read，消费原Native48 capture/current和退出/隔离归属。仅publicationRevision前进不取消健康读；原最终固定typed输出/额度保持。capture从原生handler开始，不签renderer排队前代次绑定。

2产品+4原入口支持共10hunk已fresh排他精确导入；原transport实际14场景31断言、旧main与旧show两个必要控制及3图各394输入/9库已核。原源码父门先在旧Decky静态断言FAIL，两个受影响原断言经明确selector实际通过；不拼默认20或完整Native父。漏支持编译FAIL、重复Node pin准入UNKNOWN、摘要process引用纠正和源门CRLF等价原件保留，不为封存重跑；10实际Job归零，4次编译vctip限定收尾单列。最终Kernel动作在本读测试为受控叶，不作动作Kernel证明。

独立泛用路由审计19当前span/6历史原件已核：自定义联动条件可选Steam.exe/脚本走generic shell；独立boot BAT入口验证后仍有最终准入时间窗。未读安装配置或执行竞争，不签已发生绕行；合法GamingHome/前owner独立启动保留。审计封存越窗如实保留。唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/RemainingChannelAudit-20261115/{SOURCE_READY.json,GENERIC-ROUTE-INTAKE.json,REPORT.zh-CN.md}。

本后继交下一自然Native/匹配包，030440固定快照不含，不追编或新增部署。全SourceLeaseV2 main/remote、其余consumer/资源与完整配套及实体三点手柄触摸、睡眠和CPU收益仍OPEN，整体目标ACTIVE。本层无现场Steam/游戏/设备/睡眠动作，普通导出fixtures NOT_RUN。
<!-- /STEAM-PUBLIC-UI-READ-OWNER-20261115 -->

<!-- DECKY-TYPED-REMAINING-20261113 -->
2026-11-13 Decky原typed余18场景已实际补验（17常规+1原global missed-event，原24不重跑），42原注册场景均有实际回执引用，跨epoch/定向不能拼成一次整父PASS。包括余3资源Ready/OFF/admission及原第二preflight子分支、9session拒绝/畸形mutated回包、长temp/活watch/最终准入拒绝/准备后二验/retained Ready，全部句柄与Client/Owner/reader/pendingIO归还。global原4实际Runtime回执场景核明确失败2次、成功1、uncertain2、cancel1，uncertain真实观察20031ms；通知完全漏掉后5093ms自动接续、plan2，OFF后不复活，原风暴/虚拟时钟边界及各1000轮交错入口保持；0ms字段不当零CPU。当前main03335因托盘交互与旧main64f642不同，不能借旧hash签当前整main；固定一次新源后，原生成器导出的全部489实际编译记录/410unique输入逐字相同、15库/选定编译工具和选项相同，故仅实际编译范围复用旧EXE，正式整文件身份门与global cache均不改、不冒称缓存PASS。2新原生Job自然准入/关前0，原deferred Job0/handles关0；原45秒和业务界保持，零重编/产品或fixture变更，无新增ID/minimum。输出路径斜杠比较的准备FAIL及费用保留，修后仅复读来源比较。主线Steam只读盘点818候选/0缺anchor/39待映射仅源码地图。42范围观测、整父、当前整main/配套包、实机S0/S3与视频/CPU收益分列，未部署/操作Steam或设备，目标ACTIVE。唯一后继同Decky恢复项目recovery-cause-trace-20261113/typed-remaining/EVIDENCE_READY.json、OBSERVATION-COVERAGE.json及CLOSING.json。
<!-- /DECKY-TYPED-REMAINING-20261113 -->

<!-- DECKY-EXTERNAL-INSTALLED-CPU-20261113 -->
2026-11-13 Decky相关现有进程完成一次外部30秒只读CPU观察，无设置/启停/Steam/睡眠动作、无产品/fixture源码修改。PID36980与原日志writer的实际出生FILETIME逐字相同，约30.143秒内CPU增984.375ms，单核3.265690086%/按监测进程可见32逻辑CPU归一0.102052815%；这不是被测进程或整机affinity证明。另一YeManCC PID20180及两个已观察Loader PID15156/20532在各自约30.12秒窗口计数增量为0，只称未记到增量，不推成零CPU/无尖峰；其他PID角色或Job归属未独立证明，主进程CPU也不当Decky独占成本。自然场景/视频可见状态未知，不签idle、视频/唤醒峰值或后继性能收益。使用原owned RawProgram守卫运行临时PowerShell脚本，整个portable PowerShell树/精确Node及实际Process/Automation/CoreLib程序集前后pin，1监测Job实际准入/自然关前0；该Job只拥有监测器，不归还或控制被测产品进程。原raw保留100ns CPU/出生与单调时钟，消费端独立重算四对全部比率。无Add-Type/编译/新产品EXE/服务/轮询自启、无新canonical ID/minimum；旧安装队列观察与未部署后继分开，完整目标ACTIVE。唯一后继同Decky恢复项目recovery-cause-trace-20261113/installed-process-cpu/EVIDENCE_READY.json及CLOSING.json。
<!-- /DECKY-EXTERNAL-INSTALLED-CPU-20261113 -->

<!-- DECKY-TYPED-ADMISSION-HANDOFF-20261113 -->
2026-11-13 Decky原typed启动许可/资源交接8个定向消费者已核收，零重编、零产品/fixture源码改动。复用同DTR13原EXE前核完整源码/工具/当前main等相同，原图489条source记录为410唯一路径、15库（前文sourceCount489是记录数，不冒充489unique）；前5资源/退出及9唤醒不重跑。四final admission核绑定成功后许可暂缺的自动/retained接续，以及OFF/终止停止；四resource handoff核超时后UI暂缺/恢复、自动与明确提示接续、hash/OFF终止，等待时原preflight仅1/plan0，恢复沿原3秒offset而不重开额度。原automatic内4边界hint-only/OFF/session/Ready各32提示，普通hint不取消准备，真实owner边界才取消/重验，重复Ready/OFF后hint拒绝、无binding。8父及4原嵌套边界句柄147→147、Client/Owner/reader/pendingIO全零，1新增原生Job实际准入/自然关前0、原45秒和所有业务时限保持。已从相同实际Runtime核cancelResourceRetry清pending/due且false时不把到期资源timer加入等待；此源码/行为证据不等于idle CPU实测或百分比收益。无新增ID/minimum/logger/probe/线程/EXE/恢复额度，不拼完整typed父、真实睡眠或配套包，无部署/Steam/设备动作，原失败/费用保留，目标ACTIVE。唯一后继同Decky恢复项目recovery-cause-trace-20261113/typed-admission-handoff/EVIDENCE_READY.json及CLOSING.json。
<!-- /DECKY-TYPED-ADMISSION-HANDOFF-20261113 -->

<!-- DECKY-TYPED-WAKE-ORDER-20261113 -->
2026-11-13 Decky原typed唤醒通知顺序9个定向消费者已核收，零重编、零产品/夹具源码修改。完整489源15库/工具及当前main64f642和Runtime3b60ad等实际原文件身份先核相同，复用刚完成resource-stop的同EXE，不作正式global cache命中；前5资源/退出组及其他旧组未重跑。owned/retained两种S0 Ready实际main提取消费者各核原512重复信号，另各原committed no-Loader/terminal-admission子路径512重复信号；每次新代次只增一次mode read/plan，不换旧Loader。正反resume/disconnect、late result只接当前代次、OFF压过迟到结果及child exit owned/retained均通过；后两原case另含实际held endpoint后代的PID/birth/终止和不手动release子路径，不另造ID。9父case句柄147/148前后相等，2committed和2held子路径的Client/Owner/reader/pendingIO亦全归还，旧Loader/CEF保持；仅1新增原生Job真实准入/自然关前0，原45000ms/业务时限不改。两次准备FAIL（旧raw request字段名/本地process TDZ）在业务前已停止、原FAIL/费用保留，修脚本后只执行尚未开始的9项。无新增诊断schema/logger/探测/轮询/线程/产品EXE/恢复额度；不拼原完整typed父，不签真实S0/S3睡眠、游戏/Steam实机、CPU或恢复降频收益，无部署。目标ACTIVE，唯一后继同Decky恢复项目recovery-cause-trace-20261113/typed-wake-order/EVIDENCE_READY.json及CLOSING.json。
<!-- /DECKY-TYPED-WAKE-ORDER-20261113 -->

<!-- DECKY-TYPED-RESOURCE-STOP-20261113 -->
2026-11-13 Decky原typed资源恢复/卡住退出5个定向消费者已按当前固定源码核收，无产品或夹具源码修改。三resource原case分别核临时deadline恢复、hash终止及cleanup未归还终止；同一原自有子分支另跑这三项的第二次preflight边界（不另算父ID），recover按原3秒offset恢复，hash/cleanup未通过前不再绑定/起Loader。两个stalled原case在夹具release未发出时归还，实际172/156ms，原3200ms上限保持；绑定子/后代/拥有Loader退出、late refresh/recovery拒绝，父154→154及各Client/Owner/reader/pendingIO全零。资源三项父155→155/子147→147，原deferred自有Job关前0/handles关闭0，子7094ms/父观察9766ms，原共享45000ms不改。一次/W4 /WX原编译、一图489源15库、2外层真实准入Job自然关前0；旧offline2/ready/legacy/runtime/CPU组不重跑，不拼完整typed父。主线main已变，故固定新epoch一次而未借旧EXE签当前来源；后继变化不追编。只读安装版同旧PE，保留日志中新42行中19mode三路正常、无恢复/trace行；仅保留片段，不承诺跨截断缺口连续健康、视觉或实机睡眠成功。无新增logger/探测/线程/EXE/恢复额度，无部署或设置/Steam/设备动作。原FAIL/UNKNOWN和费用保持，目标ACTIVE。唯一后继同Decky恢复项目recovery-cause-trace-20261113/typed-resource-stop/EVIDENCE_READY.json及CLOSING.json。
<!-- /DECKY-TYPED-RESOURCE-STOP-20261113 -->

<!-- DECKY-COST-ALIAS-CONTINUITY-20261113 -->
2026-11-13 Decky成本采样known-birth共享上下文/birth0窗口别名切换已实证修复：实际main同PID两来源字段不同，旧Sampler在第二alias-canonical-missing场景重复OpenProcess触发原port拒绝；原计划6仅首场景完成、第二失败、4未到达，原native退出2/后置误预测完成5项的FAIL和全部费用保留，未重跑旧源。仅3产品hunk兼容未知birth expectation选槽/保留后续兼容槽/更新expectation；两个不同nonzero仍不直接匹配，每次实际liveness/image/session/times/birth证明片段逐字不变。候选6别名组合实跑通过，含未知转已知、上下文消失/返回、前插/满容量、路径归一及冲突birth拒绝后重建baseline；同一EXE再跑原5身份故障，6真实Job准入/关前0且均自然（含预期旧反例/selector退出2）。两个图各302源15库；普通front场景JSON及端口调用完全相同，消失/返回opens[2,0,1]，满容量仅开新1个。1产品+2原fixture精确写回，编译/写回仅声明CRLF等价，其他并行字节保留；原CPP宏未定义默认源码相同。旧8/8 target-order、own-session8及默认117/117不重跑，不拼原父；本轮首次补上alias+churn的六个明确组合，不扩大为任意组合或历史断连根因。无新增logger/轮询/线程/产品EXE/身份来源或恢复额度，未完整Native/包/部署或实机睡眠、CPU百分比、恢复降频证明，目标ACTIVE。唯一后继同Decky恢复项目recovery-cause-trace-20261113/cost-alias-continuity/SOURCE_READY.json及CLOSING.json。
<!-- /DECKY-COST-ALIAS-CONTINUITY-20261113 -->

<!-- DECKY-TYPED-OFFLINE-READY-PAIRING-20261113 -->
2026-11-13 Decky原production typed离线与Ready重绑两个定向消费者已核收：实际main账户/准入/健康路径离线可达，Ready/重复及零Ready判定保持、重绑不换原拥有Loader；实际OS监听正例/活子进程无监听/foreign父监听三边界通过。产品资源与句柄归还（153→153、165→165），一实际图489源15库，含旧编译FAIL的3个Job关前0（1自然/2精确工具收尾）。仅导入3既有夹具接线：Observer publication严格支持旧30391与新34301两个已知SHA来源，回剥body逐字相同，旧输出/畸变/未知/二次适配/CRLF负例保持；退出CAS后仅允许精确closeSharedPowerMirrors前缀，缺CAS/错序仍拒绝；原惰性lighting stub仅maybe_unused，/W4 /WX及业务断言/时限不变。产品0修改，不新增账户保护/查询/logger/轮询/恢复额度。旧准备/编译FAIL和两次只读后处理字段错误前向保留，修后只复读成功raw，不为封存重编或重跑；原账户登录与真实Steam动作0。两个原case不签完整typed父、NativeReadScope/SuspendCapture整体验收、匹配包或实机睡眠/CPU/降低恢复频率。Steam层确认原有bounded steamFailure/action/episode/Steam PID+birth/power/recoveryBudget足够，后续依明确失败补，不重复logger。未完整产品构建/部署，整体ACTIVE。唯一后继同Decky恢复项目recovery-cause-trace-20261113/owned-ready-offline/SOURCE_READY.json与CLOSING.json。
<!-- /DECKY-TYPED-OFFLINE-READY-PAIRING-20261113 -->

<!-- DECKY-COST-TARGET-ORDER-20261113 -->
2026-11-13 Decky成本采样目标变化缺口已复现并精确修复：旧fallback取首个未用槽，会先丢弃后续仍需要的exact目标句柄及连续CPU baseline。原canonical native_cost显式target-order8旧/8候选核旧19处速率丢失；满12前插替换旧open12/新open1且保11目标速率。另仅续5旧/5候选故障边界，旧5处其余目标断率、候选0；retained退出/映像/Windows会话/出生/CPU失败仍按原证明拒绝并可后续重建baseline。所有非handle目标查询/CPU读取次数相等，OFF句柄归还/闭门零查询；4图各302源15库、10 Job关前0（8自然含预期旧FAIL/selector负例，2精确工具收尾）。只改替换fallback一hunk，向后避开仍需的复合身份槽；正常matched/重复alias原分支逐字不动，无新timer/线程/产品EXE/跨调用身份缓存。3源精确导入；原CPP默认宏undefined条件源码逐字同，既有own-session8 helper原字节保留。默认117/117、旧wire/storm/runtime不重跑、不拼父PASS；alias+churn组合尚无本轮新实跑，按原分支未变分列。调用次数减少只为隔离端口实证，不当实机CPU收益。无整编/包/部署或设置/Steam/设备动作，整体ACTIVE。原件同Decky恢复项目recovery-cause-trace-20261113/cost-target-order/SOURCE_READY.json与CLOSING.json。
<!-- /DECKY-COST-TARGET-ORDER-20261113 -->

<!-- DECKY-DEFAULT-COST-CHECKED-20261113 -->
2026-11-13 默认成本原Checked配置后继已核：同一冻结CPP/adapter/model/JSON，以原/Od /RTC1 /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MTd、Sampler宏未定义运行，117检查实际通过；与先前Release117的参数只差预定profile和输出路径，编译器身份相同，Release及其他已过组未重跑。新增实际290源15库图、2自然Job关前0，CPU/内存仅读本方测试进程。原默认两个配置的C++消费者均各自有原件，不能拼成原Python wrapper或完整typed/legacy父的整轮PASS；先前Checked NOT_RUN按原epoch保留，以本补验更新当前范围。此段无产品源码/设置/应用/Steam/采样或部署改动，匹配整包与实机仍OPEN，整体ACTIVE。唯一后继同Decky恢复项目remaining-legacy-consumers/default-cost-checked/CHECKED_PROFILE_READY.json与CLOSING.json。
<!-- /DECKY-DEFAULT-COST-CHECKED-20261113 -->

<!-- DECKY-REMAINING-CONSUMERS-20261113 -->
2026-11-13 Decky剩余旧消费者与默认成本入口补验完成，无产品源码修改。原crash-breaker173检查实际核五次崩溃停止、未知/重复账户观察不补次数、已知新账户按原规则再启动一次；原binding-cancel两个真实静默loopback HTTP/WS场景核1500ms原界内关闭、子进程退出、callback weak owners释放与OFF迟到请求不复活（毫秒计时0不当零耗时/零CPU）。原native_process_cost默认Release宏未定义入口117检查通过，实际CPU/内存仅读本方测试进程；Checked及两profile默认wrapper仍未整跑。三图实际313源14库/381源15库/290源15库与6 Job关前0已核（3自然/3精确工具收尾）；源/断言/内层上限未改，旧41/11、wire6、storm、Sampler8未重跑。首默认入口runner的process局部名TDZ准备FAIL保留，0编译/业务，改名后只续该未运行入口，不涉及产品。旧FAIL/各epoch、完整typed/legacy父/匹配整包/实机睡眠和CPU仍OPEN，整体ACTIVE。唯一原件沿Decky恢复项目recovery-cause-trace-20261113/remaining-legacy-consumers/EVIDENCE_READY.json及CLOSING.json；本轮未部署或操作应用/设备/Steam。
<!-- /DECKY-REMAINING-CONSUMERS-20261113 -->

<!-- DECKY-COST-OWN-SESSION-20261113 -->
2026-11-13 Decky诊断成本采样已做有证据的小幅去重：只复用同一次Sampler调用内成功的本进程Windows session读取；失败仍对后续目标重试，每次调用重新读取，目标PID/映像/会话/出生与退出核对、OFF归还、原采样周期和JSON不改。复用原native_process_cost_selftest.cpp的显式参数/宏，8个旧源+8个候选场景输出及全部其他端口调用严格相同；12个有效目标的own-session/own-PID读取各12→1，首次失败2次后复用、持续失败后下一次调用恢复、别名/目标错误/电源/OFF边界均已核，未知参数拒绝且不跑默认组。两实际图各302源/15库、5 Job关前0（2工具收尾/3自然含预期selector退出2）；旧Runtime/wire/storm和原默认CPU/memory消费者未重跑。只写1产品头3精确hunk+原CPP/1helper；无新timer/线程/产品EXE/跨调用缓存或Steam身份来源。原8个具名CPU栈只作选点，调用去重不冒充实机CPU下降；未整编/建包/部署，整体ACTIVE。唯一原件同Decky恢复项目recovery-cause-trace-20261113/cost-own-session/SOURCE_READY.json及CLOSING.json。
<!-- /DECKY-COST-OWN-SESSION-20261113 -->

<!-- DECKY-RUNTIME-TRACE-VISIBILITY-20261113 -->
2026-11-13 Decky旧回归入口可见性后继：原FailureTrace定义曾被owned-binding宏隐藏，而通用恢复代码仍引用，旧编译FAIL保留。现在只在原runtime头拆开两处guard，暴露同一诊断helper；正常生产条件展开逐字一致并有实际trace token突变拒绝，不冒称新生产整编。原Runtime41、health-close11实际全过，4 Job关前0（2自然、2精确工具收尾），原用例正文及超时未改、旧wire6/storm未重跑。仅1头文件2 guard精确写回；旧legacy Runtime/health-close编译缺口已闭合，其他legacy/typed完整父、配套包和实机继续OPEN。只读本机055102日志89行：启动一次页面重建后07:30:44两通道恢复，尾08:07:15无后续UI重建/重开，不是后继已部署证明。复用CPU775已解析样本，4成本Sampler/4共享ModeObserver具名栈已核原raw归属；不当CPU比例或完整采样证明，独立Decky子进程未覆盖。无新采样/业务设置/Steam/部署动作，整体ACTIVE。原件沿同Decky恢复项目recovery-cause-trace-20261113/runtime-trace-visibility/SOURCE_READY.json与CLOSING.json。
<!-- /DECKY-RUNTIME-TRACE-VISIBILITY-20261113 -->

<!-- DECKY-BUDGET-WIRE-SOURCE-RETURN-20261113 -->
2026-11-13 Decky预算追因真实IPC后继已写主线：原共享委托/匿名管道/ManualRepair/Runtime回执核收6边界，锁外附加预算、畸形只丢可选字段、power/OFF迟到回包不成功、旧action/Steam/power11与writer12分列。并复现同Steam未知后返回漏即时健康检查，原Runtime仅ON且非stopping的正边沿置原connectionCheckRequested；负presence及episode/stage/deadline/attempt/持久额度原代码不改，不添加重启/退款/查询循环。原同一canonical case显式控制另核1000同tuple通知零新增健康读及1000 OFF返回不复活，五个已过私有边界未重跑。4图各497源/15库与11 Job关前0已核，包含精准工具收尾及一次超时兜底，不冒称全自然。旧2s/5s FAIL、collector超时、legacy宏缺FailureTrace编译FAIL及只读后置拒绝均保留。仅1产品hunk及3夹具文件；混合换行按整源CRLF归一等价核对后原字节精确写回，不改并行菜单日志。无新增timer/线程/EXE/重试/额度，未整编/建包/部署/操作Steam，实机睡眠、整机CPU及降低恢复频率仍OPEN，整体ACTIVE。原件接recovery-budget-wire/SOURCE_READY.json及CLOSING.json（同DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113项目）。
<!-- /DECKY-BUDGET-WIRE-SOURCE-RETURN-20261113 -->

<!-- DECKY-RECOVERY-BUDGET-LINEAGE-20261113 -->
2026-11-13 Decky恢复额度来源诊断已写主线：原锁事务只捕获无分配POD事实，锁释放后随原私有回执附加recoveryBudget；父进程原结束日志沿原actionId/episode/开始Steam PID+birth/power记录，writer power仍单列。固定enum/boolean区分普通/已验证新构建补偿/未知、主备写入及回读；可选64hex仅为已读frontend build，不冒充Native安装构建。reserved不代表UI派发。原实际函数22双源返回/设置/全部IO及current调用严格相同，实际分配失败、11畸形metadata和操作错配控制通过；另仅续最后第6次归属失效1项，仍拒绝并保uncertain，不拼23项整父。4实际编译图、10Job关前0：6自然，4仅预声明PID/path/SHA正确vctip工具兜底，未豁免业务。原后置计数FAIL、并行main全hash拒绝及继承CRCRLF告警保留；主线main仅3精确hunk保留普通Steam/light退役字节。2产品+原CPP/2helper；无新增查询/日志调用/timer/线程/EXE/重试/额度，默认及旧Runtime/client不重跑。完整私有IPC/Runtime/Native配套包、部署及实机睡眠/CPU/降频仍OPEN，整体ACTIVE。唯一后继Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/recovery-budget-lineage/SOURCE_READY.json及CLOSING.json。
<!-- /DECKY-RECOVERY-BUDGET-LINEAGE-20261113 -->

<!-- DECKY-FAN-OBSERVER-ISOLATION-20261113 -->
2026-11-13 Decky风扇显示订阅异常隔离已由原TSX/真实MirrorClient内存消费者核收：旧源在选择、应用前、隐藏回执及取消的6处实际拒绝，最小notify隔离后同6处通过；原父FAIL中两项误要求detach销毁后继续通知已保留，只续旧源/候选各2项正确撤销检查。原风扇21项与未知/组合/空选择3项拒绝另实际通过；8个实际准入Job关前0，未拼成一个全父PASS。仅fan notify+原UI夹具精确导入，不改准入、2秒意图、恢复次数或Steam动作。源码解释显示订阅不得打断发送/结算/需求归还。现有公共有界错误码与action/episode/Steam身份链继续保留；本缺口不是历史掌机重启的已证根因。未完整Web/插件包/部署或实机睡眠、CPU与恢复降频验证；候选交下一自然构建，不追赶旧包。整体ACTIVE；唯一后继Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/fan-observer-isolation/SOURCE_READY.json。
<!-- /DECKY-FAN-OBSERVER-ISOLATION-20261113 -->

<!-- DECKY-CACHED-HEALTH-LOG-20261113 -->
2026-11-13 Decky健康缓存日志去重已精确写主线：现场留存识别215条相同零耗时全健康记录，原force路径每次ofstream flush；现仅合并elapsedMs明确unsigned0且模式/身份/官方三路及checking全部已确认健康的重复项。实际读取耗时、未知/故障、Steam/来源变化与power边沿首条、失败写后补记和failure/action trace保持。原日志频率消费者新增同入口decky-mode-health定向选择，不增ID/minimum；真实提取的5个当前产品函数与隔离文件写入核收20项（原7 Decky控制保留），旧源20项中3个重复写负例拒绝；128相同健康调用原128行、候选1行，不能外推整机CPU收益。首次测试依赖编译FAIL/准备拒绝与成本保持，3实际Job关前0。并行main全文件漂移先拒绝，再核5函数/配套头未变，只对原日志函数精确rebase，其他并行字节保留；原default整组未跑。只导main日志函数+原Python夹具，原full Native/watch103属于各自epoch，本后继交下一自然构建，不追赶重编；未部署/现场恢复降频/CPU百分比或物理睡眠验证。执行保存账519803与写后console519804分列，额外准备开销单列补账，900000总额不重置旧失败。Steam公共root写租约/V2仍未正式，未用metadata顶替写权限。整体ACTIVE；唯一原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/cached-health-log/SOURCE_READY.json。
<!-- /DECKY-CACHED-HEALTH-LOG-20261113 -->

<!-- GAME-STEAM-JOINT-CACHE-CONSUMER-20261113 -->
2026-11-13：共同查询与首个 Decky 侧栏消费者已配套写主线。游戏 EXE/PID/birth/规则/选择与 Steam 会话/AppID/完整 GameID 仍由两个独立唯一层提供；共同 adapter 不选游戏、不新增事实缓存。`gameSteam.context.get` 只连接内存观察，`refresh` 才提交一次明确需求；同一 canonical 进程事实共用唯一 TrackerOwner 的关联及执行 token。feature 私有配置、文档和 global selection 世代留在请求/动作 guard；显式前台 B 不借全局 A 的世代。

运输复用原 GameLink worker，4 槽有界 batch 不新增线程/定时器/EXE，job-only wake 不额外触发旧恢复查询。真正 Pending 的新选择订阅持到完成，旧 A→B→新 A 不因旧票据撤销而失去唯一执行。完成仅触发 get；可见侧栏的明确需求、真实 Ready/同 target readiness 恢复、已覆盖的 Steam Session tuple 变更才 warm，重复签名合并，隐藏时不 warm。Steam 不可用或实际源失败保留独立游戏元数据，不回退旧阳性；GameID 始终为精确十进制字符串，不作为账户 SteamID 或当前动作证明。

原生39、队列18及4反例、前端25及 host8保留各自原来源；后继只续 queued ABA 单行为/旧头反例、readiness 单行为、默认失效接线与受影响类型/完整 main 语法。1001 joined cache读取零进程/注册表/源文件/时钟发现，1001重叠需求仅一次物理观察。工具缺项按既有固定 global pins只读核收，原FAIL/UNKNOWN及限定工具回收不改自然退出。未完整 Native 链接/Web/包/部署、未测安装版 CPU 百分比。菜单新增750ms cold typed dispatcher仍为独立后继 OPEN，本段不声称菜单已迁移或全部 Steam 消费完成。

唯一当前原件：`Testing/YMCC/Evidence/SteamGameIdentity-20261113/cache-consumption/JointConsumerCandidate/SOURCE_READY.json` 与 `MAINLINE-IMPORT.json`。此前“joint query OPEN”保留为历史 epoch；本段只关闭共同 get/refresh 与首个侧栏的范围。
<!-- /GAME-STEAM-JOINT-CACHE-CONSUMER-20261113 -->

<!-- DECKY-WATCH-FOREIGN-CONTEXT-CALLBACK-20261113 -->
2026-11-13 Decky监听跨上下文回调后继已精确写主线：原remember会将其他页面的fatal形异常与同run信号计入32队列，最终消费却要求当前上下文；真实本地WinHTTP/CDP128回放使当前旧源误报context-event-limit。现入队前共用当前/已排队future context判据，忽略不可能消费的回调；queued future的有效connection通知、原账户marker256与后置确认均保持。当前原watch消费者103断言（原96+7）通过，旧源真实拒绝；5个实际准入Job关前0，另1次长输出路径导致未准入UNKNOWN保留，不能用空Job0替产品归还。私有源/输出MAX_PATH编译FAIL和后置时钟断言FAIL均保留，只续候选/只复读原件，未重跑旧反例。只导入watch头+原夹具，无新ID/minimum/轮询/线程/EXE/额外CDP或恢复预算变化。累计原执行账487286/600000ms（保存回执487233与稍后console487286分列），原FAIL/费用不改。此前完整Native候选为旧watch epoch，本后继进下一自然构建，不追赶重编；UI24/Runtime/bootstrap未重跑，未认定为历史现场根因、未测CPU百分比/恢复降频或部署。整体ACTIVE；唯一原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/watch-context-callback-scope/SOURCE_READY.json。
<!-- /DECKY-WATCH-FOREIGN-CONTEXT-CALLBACK-20261113 -->

<!-- DECKY-NATIVE-TRACE-INTEGRATION-20261113 -->
2026-11-13 Decky有界源头诊断和监听噪声过滤的完整Native候选已核收：原native/build_native.bat实际135351ms，5PE，1451固定源/3027工具、1447实际include/54lib、无未声明输入，生成identity头由原generator/pending/最终PE与sealed receipt共同绑定，自有Job关前0。原编译启动器路径FAIL及电池合同stale FAIL保持。仅私有快照按原generator刷新两份battery合同，主线/managed电池未改，所以不是当前主线配套包通过，无Web/包/部署或新实机睡眠/视频/恢复降频/CPU收益证明。末次主线单次观察除声明的私有生成输出外drift0，不追赶重编；原96watch/24UI等业务仅引用原epoch，本次新增夹具0。Steam V2及安装根最终SourceLease继续等待公共层正式来源，目标ACTIVE。唯一原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/native-integration/CLOSING.json。
<!-- /DECKY-NATIVE-TRACE-INTEGRATION-20261113 -->

<!-- DECKY-WATCH-CALLBACK-NOISE-20261113 -->
2026-11-13 Decky监听回调噪声后继已写主线：旧队列会把旧run健康标记、非fatal插件exception和未知context销毁计入32容量；实际本地WinHTTP/CDP 128混合回放使旧源误报context-event-limit。现只提前丢弃不能触发原消费动作的元数据；销毁若对应当前或已排队future context仍保留，已退休target的迟到标记不再产生health read，后续context仍能附着。原账户marker256字节、后置确认和相关事件32上限保持，200字节有效账户信号已实跑。官方V8按已登记binding名订阅，故正式回放使用本绑定旧run；此前94范围及FAIL/费用保留，当前原consumer96断言（原89+7）通过，当前4与此前4 Job关前0，各来源分列。精确导watch头+原fixture，无新timer/thread/EXE/重试或账户一致性门；UI24/Runtime/bootstrap/阶梯未重跑。未归因为历史现场、未整编/包/部署或实机CPU收益/睡眠验证。证据Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/watch-callback-noise/SOURCE_READY.json。
<!-- /DECKY-WATCH-CALLBACK-NOISE-20261113 -->

<!-- DECKY-WATCH-CONTEXT-NOISE-20261113 -->
2026-11-13 Decky监听无关上下文风暴后继已写主线：原remember在命令等回包时将foreign-origin/isolated-world创建事件计入32容量，晚消费才忽略；实际本地WinHTTP/CDP 128无关事件回放使旧源mirror-bootstrap-context-event-limit误断链。现在仅在入队前复用原accepted-context谓词，消费共用同一谓词；128噪声仍能附着有效上下文且零无关evaluate，40相关上下文仍按原32上限拒绝。原watch消费者当前89断言（原85+补4），旧源真实拒绝、4Job关前0，精确导入watch头+原fixture。原4096packet/2500ms/7s/owner/fatal/退出规则不变，无新timer/thread/EXE；其他UI24/Runtime/阶梯未重跑。两份选定留存日志未匹配该reason，故不认定为历史现场根因；未测CPU百分比、未整编/包/部署或实机睡眠。证据Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/watch-context-noise/SOURCE_READY.json。
<!-- /DECKY-WATCH-CONTEXT-NOISE-20261113 -->

<!-- DECKY-MODE-FAILURE-CAUSE-20261113 -->
2026-11-13 共享Steam模式探测故障溯源已接Decky：UI read在原会话失效返回前归一化现有steamFailure；实际ModeObserver.read按开始时Steam PID/birth/power写ModeRead/Observation/actionId0。旧steam-mode-read保留，晚到失败只记原身份，旧模式结果仍拒绝；成功/无源错误不伪造failure。旧源原UI receipt消费者实际拒绝，当前24断言（原12+补12）通过，覆盖原真实共享transport与精确提取的main回调、CDP/JS/WinHTTP、迟到身份/电源、诊断抛错隔离、1001缓存零额外查询及句柄归还；4个Job关前0。只2产品+原fixture/PS生成器精确导入，无新探测/轮询/线程/EXE，不改known/mode/重试/自愈预算。旧失败与原费用保留，原Decky85/47/192/阶梯未重跑；main只编实际回调，未完整main/link/包/部署或实机降频/CPU收益验证。原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/mode-read-failure-relay/SOURCE_READY.json。
<!-- /DECKY-MODE-FAILURE-CAUSE-20261113 -->

<!-- DECKY-UNCAUGHT-EVENT-FACTS-20261113 -->
2026-11-13 Decky未捕获异常诊断后继：Runtime.exceptionThrown在原fatal分类及现有live确认后，terminal通知现保留该源事件的公共有界异常类型/行列；验证命令不再清掉源头事实。旧源以同一原watch消费者实际拒绝，新候选85项断言通过（原81保持），4个Job关前0；仅watch头与原fixture精确导入。bootstrap47与Runtime原用例只引用前epoch，不重跑或冒充合并整父。无新增动作/守卫/轮询/线程/EXE，未整编/包/部署。同期本机留存日志525次mode中524次三路正常，21:59有一次镜像断链，5812ms重连恢复；同Steam PID/birth及官方Loader出生时刻保持，无页面重建/开关大屏。它不是新版诊断或降低恢复频率的实机证明。原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/exception-event-facts/SOURCE_READY.json。
<!-- /DECKY-UNCAUGHT-EVENT-FACTS-20261113 -->

<!-- DECKY-WATCH-FAILURE-RELAY-20261113 -->
2026-11-13 Decky 有界故障诊断后继已写主线：初次 bootstrap 与后续 watch 保留公共安全 CDP/JS 事实；父 Runtime 用原绑定 actionId/Steam PID+birth/power 归属转发，失效或缺 run 仅不记归属，不改变原操作。单次 command 事实在通知后清除，避免旧 CDP 错误串到独立 socket close。47 原初次守卫、当前 81 实际本地 watch 断言与 1 原 typed live-watch/192帧均核收；不同来源分列，原 FAIL/费用保留，自有 Job 关前为0。3 产品+3原夹具已精确导入；无新轮询/线程/EXE，不改恢复次数或时限，未整编/建包/部署/真实睡眠，未测现场降低恢复频率。原件：Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/SOURCE_READY-BOOTSTRAP-RELAY.json。此前 bootstrap/private 记录按原 epoch 保留，以此后继为当前范围。
<!-- /DECKY-WATCH-FAILURE-RELAY-20261113 -->

# Steam 检测与请求统一入口

## 游戏 EXE 与 PID 的唯一选择入口（2026-11-13）

用户明确的模块边界：**游戏识别唯一层与 Steam 唯一层是两个独立、并列的事实模块，共同服务 YMCC 消费层。** 游戏识别层拥有 EXE、PID、进程出生身份、识别规则、选择及广播；Steam 层拥有 Steam 会话、账户和 Steam 游戏身份。Steam 的游戏关联功能通过只读接口消费游戏识别结果，不迁移、合并或接管游戏识别 owner。YMCC 的其它消费者仍直接按需消费这两层，不能各自再建识别源。

本次 `NativeGameSelectionReceipt` 及两个 `nativeValve*Selection*` 接口属于游戏识别层的对外消费合同；`SteamGameLink` 只是其中一个消费方。两层的各自代次和有效性分别核对，任一层未知或失效都不能用另一层猜测补齐。

用户随后明确要求优先缓存短接。游戏层已增加 `nativeValveReadCachedSelection`：仅消费原内存选择，`processValidated=false`，不读取规则文件、不查询 Windows 进程、不查询 Steam。原 `nativeValveReadCurrentSelection` 成功后才标 `processValidated=true`，仍是鲜验证入口；两个返回都不自动授动作权。GameLink 首次选择现走缓存入口，再保留原精确进程 pin 与最终门，避免重复验证同一 PID。此后继实际 54 组（缓存19、受影响关联35）、两项反例及连续1001次缓存零进程查询已核，主线生成体相同；原件 `Testing/YMCC/Evidence/SteamGameIdentity-20261113/cache-consumption/SOURCE_READY.json`。此前37/152属于各自固定来源，未重复执行。本目标还缺公共 Steam 关联缓存与联合查询消费接线，不能将游戏侧局部 SOURCE_READY 称为两层联合完成。

运行中游戏关联的首次绑定已接现有游戏识别 owner。`NativeGameSelectionReceipt` 保存原 `NativeGameValveSnapshot` 与本次读取的 `powerGeneration`；`nativeValveReadCurrentSelection` 只复用一次原 `nativeValveReadCurrentTarget` 的规则、进程和会话验证，不枚举或重新选择游戏。`nativeValveSelectionCurrent` 仅在原锁内核对 PID、出生时间、EXE 路径、规则、稳定选择代次和本次电源代次。普通心跳允许，A→B→A、规则或睡眠代次变化拒绝。正游戏快照没有无游戏证明的 `qualificationPower`，不得借该字段判断正游戏是否有效。

`SteamGameLink::discoverGame` 只为该唯一进程匹配 Steam tracker、AppID 和完整 GameID，不能以另一条 Steam 候选替换 YMCC 选择。首次读取和 worker 最终绑定均须凭据仍有效；首次 tracker 使用 `RunningListAndProcess`，已绑定原进程仍按 `ProcessLifetime` 观察。游戏选择真正变化时唤醒既有 GameLink 事件，不增加线程或定时器。

该凭据不是操作权限。已固定的睡眠/运行中原进程继续守住原身份；显式菜单前台游戏与全局所选游戏不同时，仍按调用者原合同处理。原控制器指针消费者已读同一游戏发布，前端 `gamePolicyTarget` 继续转发 `gamedetect`，不新建识别器。Steam session、账户、窗口、power Ready、OFF、请求和实际句柄的最终检查仍由各操作保留。

定向消费者 37 组、旧独立选择/撤掉 ABA 守卫两项实际反例、原 valve/publisher normal 152 检查及 7 个无效选择拒绝已核。主线生成体与已执行候选相同；其后 Observer 原始目录拼写后继单列，不为追齐重跑。本段源码已写回，未完整 Native/包/部署或实机验收，不代表 Steam 全图迁移完成。原件：`Testing/YMCC/Evidence/SteamGameIdentity-20261113/SOURCE_READY.json`。

本任务目标是让 Steam 身份、重复读取和对外请求由共同入口协调。Stage1曾把旧授权回调当纯谓词提前调用；Decky真实消费者已复现未发送请求却发布RAM恢复授权，原111项不能覆盖此缺口，旧PASS仅保留其原范围。修后已核原111项及真实ManualRepair的3项授权边界。当前还已接恢复引擎/库Host共用进程生产器，并在原Observer发布同份身份receipt；全图迁移仍在进行，不能把本页当作已全局唯一或已验证 CPU 降幅的说明。

`native/steam_request_policy.h` 是原生 Live 操作分类的唯一声明。`main.cpp::steamLiveRequest` 和 `steam_live_cdp.h::run` 消费同一分类；未知操作在端口发现前拒绝。`run(steam, request, admitAction, shortReadMs, ownerCurrent, expectedSession)` 将两种回调分开：第3参 `admitAction` 保留旧合同，只在最终派发前调用一次，允许原消费者领取恢复额度；第5参 `ownerCurrent` 必须纯读、可重复，不能预留或消耗授权，在等待前、取得串行权后和实际发送前复核。未传第5参的旧调用不获得发现前取消能力。失效或纯谓词异常在未发送时返回 `live-action-cancelled / mutated=false / requestDispatched=false`；实际发送后沿原不确定回执处理，不能据超时自动重放。

分类只说明操作种类，不授予账户、游戏、启动、界面模式或恢复权限。账户设置继续核当前登录账户；显式菜单键保持 Steam 当前接收器语义；模式只读仍允许登录前状态；Decky 同一进程的 UI 账户证明不能替换通用账户写入依据。菜单游戏别名、dispose 等数据相关的写入语义仍由原请求消费者判断，不能仅凭 `.get`、`.inspect` 或操作前缀宣称纯读。

已有事实所有者是 `steamsession::Observer`、`steamui::ModeObserver` 和 `processInventory::owner()`。processInventory 是进程内所有者；两个 EXE 包含同一头文件不等于跨进程共享。缓存清单只提供候选；最终 PID/birth/path/session/account/document/power/boot/请求归属必须按操作复核，失败和部分清单不得证明 Steam 已停止。

| 通道 | 当前路径及待完成事项 |
| --- | --- |
| 主程序 Live | 设置、鼠标、菜单、睡眠经过 steamLiveRequest，再到共同 run；本轮接共同分类和发现前准入。共同run已支持PID/birth/root持有及最终回调后复核；普通steamLiveRequest消费仍待迁移，Decky三调用已配套消费。 |
| UI 模式 | 共享 ModeObserver 合并 CSS/Decky；手动 set/show/stop 与获准恢复共用持句柄固定 typed 出口，最终核原电源/退出归属。独立直读及其他 typed/helper 迁移继续 OPEN，提交由原观察者确认。 |
| Decky | 直接 run 也经过本轮底层门；bootstrap 异步 CDP、恢复私有子进程、双份持久额度保持，后续接共同身份与撤销事实。 |
| Mirror broker | 连接认证/run/revision 及 HUD 逐帧门已存在；其余消息的请求/会话作用域仍待统一。 |
| 外观脚本 | 原 native→PS 进程清单通道已接 owner；RPC/CDP 输出和跨进程租约继续待接统一撤销。普通 Status 含恢复/目标提示写入，不能当 VerifyOnly 纯读缓存。 |
| 自定义库 | Host已接原processInventory owner：只读/等待250ms，关闭最终确认与写入前fresh0，Unknown明确拒绝；原18行+8进程边界及独立正向case已核。Worker产品/6文件原消费者适配仍私有未验，不能借Host旧成功授权写盘。 |
| 原恢复引擎 | resolveCef已只消费共享完整fresh0候选，原父PID/birth/path/session/唯一性保留；13实际函数场景已核。模块/线程现场资格、完整engine/main及包仍分列。 |
| 启动/离线日志/独立助手 | 启动发生在主 owner 就绪前，离线日志也须支持 Steam 未运行；统一解析和票据，不能强制借用不存在的主实例或自动新建服务。 |
| 通用 shell | 有可接受 steam: 的通用能力，旧 steamStop 亦有定义；是否活跃须逐调用者确认，不把文件存在记为常驻扫描。 |

共同身份生产器继续是原 `g_steamSettingsObserver`，没有新增服务、线程或定时扫描。当前可用API：

```cpp
const auto facts = g_steamSettingsObserver.identitySnapshot();
// shared_ptr<const ymcc::steamsession::IdentitySnapshot>; nullptr = Unknown
const bool sameEpoch = g_steamSettingsObserver.sameIdentityEpoch(facts);
// 需要重新观察时只通知原producer，不领取/重置恢复额度：
g_steamSettingsObserver.requestIdentityRefresh();
```

同份锁内发布包含 `session.root/pid/created/account`、`presence`、`registeredPid`、root/pid/account三个读取错误、`windowsSession/known/error`、`revision/processEpoch/accountEpoch/observedAt`。`identitySnapshot` 与 `sameIdentityEpoch` 仅消费内存；原55项实际Observer消费者保留原20秒有限重试与销毁，并证明1001次消费没有注册表读取。

账户必须区分以下来源，禁止用userdata最近目录或完整GameID填SteamID：

| 字段/方法 | 语义 |
| --- | --- |
| `facts->session.account` / `registryAccountCandidateKnown()` | 仅原注册表正值accountID32候选；0和读取失败分别记录，但都不证明logout或当前登录。 |
| `registeredProcessMatches()` | 可读registered PID/root与此份已验证PID/birth匹配。 |
| `currentRegistryAccountKnown()` | 上述候选与注册进程的关联；仍不是UI/CM身份或通用写权限。 |
| `processEpoch` | root/PID/birth或producer生命周期改变使旧观察失效。 |
| `accountEpoch` | 候选值、读取状态或registered PID绑定改变使旧观察失效。 |
| UI/CM账户、SteamID64 | 当前文档的独立实际证明；中央resolution及64位见证尚待接通，不能由候选猜成已证明。 |

`sameIdentityEpoch` 只表示共同缓存尚未撤销，不能代替最终句柄/账户/文档/power/OFF/请求归属检查。显式菜单账户未知仍沿原当前UI接收器语义，不增加全局账户门。独立child长期watch必须有父owner委托撤销及最终验证，不能把本头当跨EXE共享或把一次启动快照当长期授权。公共睡眠准入由【YMCC-睡眠协调层】维护，本层只消费其既有PowerLifecycle/generation，未改PBT/240秒/设备产品。

已核原件分别为 `AdmissionRepair/CORRECTION.json`、`ProcessDiscovery/APPLIED.json`、`LibraryDiscovery/HOST-SOURCE_READY.json`、`IdentityReceipt/SOURCE_READY.json`，均在原Evidence/SteamUnifiedChannel-20261112项目。普通endpoint出生绑定、全部账户消费者/跨进程票据、UI/bootstrap/URI/broker/外观进出仍OPEN；完整native/包和现场CPU不能从本局部检查推导。

每次迁移复用原消费者、原正反断言和原预算，不增加默认测试 ID/minimum。原生、JS、脚本、Web、组件配套及真实现场分别验收。首轮复用 `tools/steam_live_transport_selftest.ps1` 的默认离线入口，扩展其原 fixture 的未知请求、取消、排队失效边界，不联系真实 Steam/CDP。

任务证据入口：`G:/YeManCC-Work/Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/PLAN.json`。其它任务正常并行，用新读字节和精确补丁保留变更；不为本任务冻结源码、部署或控制 Steam/游戏/设备/睡眠。

公共CDP现支持第6参 `const steamsession::Session* expectedSession`，进入等待前复制PID/birth/root，同一已验证进程HANDLE持有至请求返回；监听者父PID也须匹配。实际发送前在纯owner核对及一次finalAdmit后再次核进程存活与owner。未传第6参的调用不获得此保护；普通main/UI/Decky消费者迁移分别核收。

原transport复用新增 `-ExpectedSteamIdentityOnly` 定向入口：实际run/发现/HTTP升级/最终send的23项惰性端口检查、原默认111与原Decky3通过，撤回最终回调后复核的语义负例在原断言失败；6个自有Job关前均0，MSVC工具强制回收单列。原件 `EndpointIdentity/SOURCE_READY.json`；这不证明真实Steam网络/完整native包或整体CPU降幅。

共同纯账户解析现在位于 `native/steam_account_facts.h`：`project(Session, mode)`、`project(IdentitySnapshot, mode)`分开保留注册表候选/读状态/进程关联、同PID/birth的UI账户、watch及document/entry和冲突；本身不读取系统、不缓存、不授权。`select(facts, Policy::UiLocal)`只保留Decky原有UI优先、匹配fromUi=false与旧候选fallback政策。`positiveAccount32`统一整数范围核对。Decky原include/positiveAccount/resolve三hunk已精确导入，diagnosticWithIdentity及其后字节保持。

原SteamLease消费者20项、原两个PID语义负例及22个事实投影检查通过，1001次纯消费RegGetValue=0；缺列SDK头的后置FAIL保留，以原规范SDK清单补齐并复用同一EXE，只续未到达消费者。六个自有Job关前0，工具回收与真实consumer自然退出分列；原件 `AccountFacts/SOURCE_READY.json`。SteamID64仍无实际CM64来源，保持Unknown，不从account32/GameID/AppID猜造。

Decky三处run第6参及原3项verbatim消费者后继由Decky层核收，原件 `shared-decky-dispatch-identity/SOURCE_READY.json`；与本公共endpoint共用的原120000ms账实际累计107064ms，不能重新消费旧64904后的余额。公共账户解析为另一个新能力范围，累计42609/120000ms，不与该账混计。

2026-11-12公共睡眠分类API已SOURCE_READY，Steam消费guard已明确委托非Steam层（runtime wake/PowerReady/sameScope/observation/mutationAllowed/finalCurrent及wakeOperationAllowedLocked）；保留精确Kernel507/107+Resuming软件例外与同代deferred接续，尚待其定向SOURCE_READY，本层不双写producer。普通main的bound/unbound账户与身份读合并、UI读取、child委托撤销和其余全部进出路由仍待完成；本项目继续ACTIVE。

普通 `main::steamLiveRequest`现已消费共同identitySnapshot：入队前固定receipt与Session，普通账户操作核accountEpoch；显式菜单保留账户Unknown，睡眠witness核PID/birth/root并保留foreign registered PID时的旧UI账户pin，所有实际OS lease交共同run6。该函数不再直接读Steam注册表/打开PID。已发后的共同回包还会核当前owner/原进程，失效不返回旧成功；QAM disposeOnly及menuGame发送失败保留可能写入，禁止重放。

原transport消费者新增 `-MainSharedIdentityOnly`编译实际主函数，30实际边界、共同23、默认111及第6参Decky3均通过。原编译夹具初始化缺括号FAIL、随后QAM实际误标mutated=false的FAIL保留；修后撤回回包存活复核的语义负例在原主入口断言失败。10个自有Job关前0，工具强制收尾单列。原账78021/120000ms，`MainRequestIdentity/SOURCE_READY.json`；原SleepSteamRecovery整合/R27入口适配、完整main/包和实机场景仍OPEN，不能从这些惰性WinHTTP边界推导全覆盖或CPU降幅。

UI只读函数现在以同份identitySnapshot取得Session/presence，OS HANDLE统一委托run6，不重复verifiedProcess。cache附processEpoch/accountEpoch；失效就丢弃旧UI账户/文档证明，但不重置ReadBudget，原手动force和真实process变化规则保持。原12项实际read消费者、原UI native23及缓存撤销语义负例通过，1001缓存消费零新增进程/请求；6自有Job关前0、工具收尾单列。原件 `UiReceipt/SOURCE_READY.json`，账51748/120000ms。

UI fixture只将精确提取read函数的Observer参数改为已有内存端口类型，产品函数体逐字核收；原Observer完整55发布/生命周期证据保持原范围，不能替本轮宣称其整组重跑。原生set/stop/URI后续字节未改、23原入口已核；共同URI请求票据与整合ModeObserver/Decky/CSS父消费者仍未完成。原transport新选择 `-UiIdentityReceiptOnly`只生成必要include与define，不新增默认ID/minimum。


<!-- STEAM-SHARED-IDENTITY-R27-CLOSURE-20261112 -->
2026-11-12：原 sleep_steam_only_selftest 的十组 R27 账户/睡眠接线已复用成功 EXE 实跑通过；原十组语句、所有断言及既有标签逐字/逐项保留，默认路径仍只执行一次。独占 --shared-account-only 与 --prepare-only 已接原入口，未新增默认 ID/minimum。注册表外来 PID/账户噪声不会取代睡眠 UI pin；换账户、换 PID/birth/root、退出和旧请求仍拒绝。原准备漏头、后置 Psapi 声明及重复 source pin 的 FAIL 均保留；追加45秒中未编译、未跑整套，自有业务 Job 自然退出且关前0。

当前主线与已执行 R27 的 main wrapper、原十组函数逐字相同。随后睡眠协调层的新 runtime/auto 分类守卫另由其134项/1527检查和2个单项反例核收；本层只读独立接收，不重复运行。当前 R27 生成体补同代/非退出的分类终端内存端口后只做 prepare，未编译或执行新头组合；此端口不证明真实 pending 分类算法。证据为 SteamUnifiedChannel-20261112/SleepIntegration/CLOSING.json 与 SLEEP-CLASSIFICATION-INTAKE.json。完整 main/link/配套包、实机及 CPU 收益继续 OPEN，整体 ACTIVE。
<!-- /STEAM-SHARED-IDENTITY-R27-CLOSURE-20261112 -->


### 公共原生兼容事实的共同维护（2026-11-12）

各任务补同一个公共Steam层，按需消费事实；非Steam恢复不单独维护身份生产器、监听器或私有兼容器。原native/steam_compatibility_*继续提供record/read/publish的共同代码与关系校验，root及vendor原生读取门共用。原Resolver::propagate三类失败现在填入已有Evidence.failedRva，值为原参考图的operand/pdata范围坐标，不是实际目标地址或新offset授权；分支、短路和拒绝原因保持。

该修正只有精确源码及决策表达式核对，本轮未编译/运行，随下一自然Native验证；不代表当前Steam已获原生资格。当前原UI账户捕获不可用与原生图变化分别由共同能力补齐，旧sleep.liveAccountPinned不直接映射持续UI/watch合同。各请求、原睡眠周期和foreign注册PID例外继续分开，原预算不重置。

<!-- STEAM-IDENTITY-DELEGATION-PRIVATE-20261112 -->
2026-11-12共同身份跨进程委托后继仍为私有候选，`SOURCE_READY=false`，尚未写入产品主线或部署。候选复用原Observer发布、原Client/Owner及唯一继承cancel事件，不增加服务、线程或扫描；每请求单独撤销，生产者停止或身份失效先撤销再发布，父端拒绝撤销后的排队成功和上下文转交。

原无效句柄失败项修后实际通过；原Client十种错误身份请求均在Owner准备前拒绝；bootstrap/lifetime及排队成功/转交四条定向序列通过。原193帧前缀保留在失败父组中，不拼成新整父PASS。新增探针的Winsock头顺序、只等待句柄误用于退出码查询及早期FAIL/费用均保留。账户消费者实际编译通过，但其有效业务窗15867ms内未产出结果，保留TIMEOUT；健康消费者编译未启动，不能发布配套成功。

原120秒经用户明确追加120秒，执行235987ms，封存核收1568ms，累计237555/240000ms；十个本后继自有Job处置前实际0，超时子树的强制回收单列。原件 `Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/IdentityDelegation/CLOSING.json`。新API尚不可被睡眠消费者提前接入，单次睡前UI pin也不能替代持续文档watch。全目标及匹配构建/现场CPU收益继续OPEN。

新20:06 Win5包只读核对：20:05:52镜像断连并记录account-changed观察，20:05:53重连；周边Steam PID/birth相同，不证明用户实际换号。媒体重复注册表读取由Decky/壁纸任务在共同层处理，原件 `SteamUnifiedChannel-20261112/Field-20261112-200615/FINDINGS.json`。
<!-- /STEAM-IDENTITY-DELEGATION-PRIVATE-20261112 -->


公共原生兼容诊断后继已定向验收：原tools/steam_compatibility_file_selftest.cpp支持独占`--qualify-only CLIENT UI`，仅调用同一qualifyFile，输出模块SHA和既有Evidence，exit0=匹配、exit3=不匹配、exit2=参数错误；不执行默认整组、不访问目标进程或加载Steam DLL。当前Client/UI仍拒绝但failedRva精确为参考图0x991eba/0xbdeab，旧Public对照保持通过。一次编译、三调用、四个Job关前0，首包装器FAIL保留且同EXE续接，总37072/120000ms；294实际include源码和8库闭包已核。本能力SOURCE_READY仅指共同诊断和选择入口，当前Steam原生资格、完整Native/包/实机仍OPEN。原件QamFocusReview-20261112/shared-qualification-observation/SOURCE_READY.json。


2026-11-12共同Steam兼容图Literal后继：同一graph core/read graph补齐两段Public只读断言文本，使用完整字节含NUL、存储类型及已匹配完整caller引用绑定，不嵌入当前实际RVA、不扫描首个字符串。一次因真实源码变化的编译、六次既有--qualify-only选择实跑：原Public对照通过；当前两处拒绝已推进到Client 参考0x991ec6 compatibility-contract-unrecognized；UI 参考0xbdeb7 compatibility-contract-unrecognized；NUL/文字破坏及同文本迁至可写/代码区均在原参考字段拒绝。七个实际Job自然退出且关前0；原默认断言/代码/fixup与Legacy不改。共享修正SOURCE_READY仅限此范围，current完整资格仍未通过；无新私有producer、线程、timer或产品EXE。累计执行88530/120000ms，原FAIL及身份/R27账保留。原件Testing/YMCC/Evidence/Win5SteamSleepRebind-20261111/QamFocusReview-20261112/shared-qualification-observation/literal-relocation-source/SOURCE_READY.json。


2026-11-12只读续查：当前剩余参考0x991ec6/0xbdeb7分别指向utlmemory.h/bitstring.h的源码文件名字符串。两边只读区和build/src/public/tier1尾部一致，但构建前缀由steam_rel_client_hotfix_win64变为steam_rel_client_win64，完整字节不相等，不能沿两段相同断言文本的Literal规则直接声称通过。精确缺边已交共同Steam层；未继续扩树、未添加私有兼容器或运行更多native。旧Public正对照走原fixed contract，未声称已覆盖完整迁移graph。共同账户/会话委托仍未SOURCE_READY，session()三处旧注册读取尚待配套后迁移。原件shared-qualification-observation/remaining-reference-attribution/MISSING-EVIDENCE.json。


<!-- STEAM-IDENTITY-PAIRED-CONTINUATION-20261112 -->
2026-11-12：用户追加75秒定向核收后，复用原EXE的账户原用例实际通过，包括真实20秒慢准入；共享回执拒绝6项、账户转换12/旧证明拒绝24、binding Windows会话重复查询0、句柄147回到147。原15867ms分配不足的TIMEOUT保留，不改成产品死锁或历史PASS。

健康消费者两次编译失败分别暴露生成器把参数默认{}当函数体，以及旧ownedworker::SteamLease三参数端口漏接原继承cancel第四参。提取器/当前外部ports已补，最后端口改为委托请求消费实际产品SteamLease，原健康断言与业务体保持；最终修正仅源码，未重编/运行。原余量低于既定25000ms编译准入，不降低门槛求绿。SOURCE_READY=false，六产品候选仍私有、未导入或部署。

本段三个实际Job自然退出、关前0；封存复核1514项输入，执行/准备/封存原账299785.031/315000ms，分析与通讯端到端另列。原FAIL、准备误断言及分数时间准入失败均保存。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/IdentityDelegation/PairedClosure-r2/CLOSING.json；最终健康源码在HealthCompileRepair-r4/REPAIR.json。仅待该项及配套声明核收，不重跑账户/Observer/child/Client/R27已过组。

同层PEER-SUCCESSOR-INTAKE-20261112.json已独立核原件：媒体共享身份与空bridge后继按各自范围接收；Literal修正不授当前Steam原生资格，__FILE完整文本差异仍拒绝；当前DD2四条原生回执已读，但不外推Win5/睡眠/Running/双游戏。整体R1～R6及匹配自然构建/现场CPU收益仍OPEN，目标ACTIVE。
<!-- /STEAM-IDENTITY-PAIRED-CONTINUATION-20261112 -->


2026-11-12睡眠回补消费边界后继：固定prepareSession在Observer fallback中重读前清旧账户pin，临时读失败可留下原捕获未绑定；旧pin在Win5当时是否仍合格UNKNOWN，不能盲保。所需持续UI/document/账户证明仍应由共同Steam层提供。两份本地ScopeRunningApps定义1043/FNV3939488289和799/FNV2306644473均不匹配当前原两选择器；实际加载文档和现场调用仍UNKNOWN，未加白名单。共同身份PairedClosure-r2当前为ACCOUNT_PASS_HEALTH_COMPILE_PENDING：原账户选择raw实际PASS，三个Job关前0已独立核收；最终健康接口源码未编，sourceReady=false，未消费或导入private delegate/session候选。所有原FAIL/超时和账保留，未重跑或借预算。原件QamFocusReview-20261112/shared-ui-capture-boundary/FINAL-RESULT.json及IDENTITY-INTAKE.json。


2026-11-12共享SourceFile候选后继：原公共graph的构建目录前缀变化已准备私有节点，固定源码尾部、只读存储、有限NUL及原caller引用；不同长度路径只绑定起点，中间/末端不映射。17个新增边界留在原file工具互斥选择，不增默认ID/minimum。首次编译因离线夹具unsigned char字符串初始化C2440失败，自有Job自然关前0，未执行行为；只在新私有工具副本改char[]与显式字节转换，未重编、未SOURCE_READY或导入。原120000ms账已用102024ms（封存另计），不借身份/R27账；按原成功编译11840ms+准备2596ms+原后续准入6000ms安排不足余量，不降门求绿。当前生产主线仍为已验两Literal版本。原件shared-qualification-observation/source-file-fixture-repair/HANDOFF.json。


<!-- STEAM-SHARED-IDENTITY-DELEGATION-SOURCE-READY-20261112 -->
2026-11-12：共同Observer身份委托及配套已精确写入主线，当前SOURCE_READY仅覆盖本段。6产品文件为steam_session_observer.h、decky_binding_worker.h、decky_binding_process_owner.h、decky_binding_client.h、decky_sidebar_runtime.h及main.cpp的deckySidebarOwnedBindingPlan；另10原测试/生成器/声明。产品头不含测试friend或会话替身，原并行main字节和原EOL保持。

最终私有配套由精确补丁独立重建，六个原生成成员仅worker有EOL差异，无业务代码差异。修后健康consumer实际编译399源码/16库、x64 GUI PE通过，Job自然退出且关前0；五个入口只做syntax，健康业务/完整父门未运行。账户原用例及Observer/child/Client已过范围引用各自原epoch，不重跑，不将失败父组拼成新PASS。每请求撤销沿既有继承cancel接子进程；旧排队成功和context转交仍拒绝已撤销归属。

原TIMEOUT/编译/准备和封存错误原件全保留。人类追加30秒仍沿原身份账，当前317514.3676/345000ms（执行/准备/封存，分析协作和产品导入耗时另列）；未部署、未完整产品编译/新包或现场CPU收益验收。精确写集及来源为Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/IdentityDelegation/SOURCE_READY.json，原件SHA5714609c7be9cd179386071eb63c2cc11edd9dfc39ff3cb73340730dbbf55d30；配套交下一自然构建，不追改旧972/ef25等包。

这不授当前Steam原生兼容资格，也不把单次liveAccountPinned或每请求cancel当持续UI/document/整睡眠周期证明。其余手工监控/鼠标读者、URI/启动/broker/外观/独立库统一、匹配构建与实机CPU收益继续OPEN，整体R1～R6仍ACTIVE。较早private/no-ready记录保留为历史，不再代表本段当前源码状态。
<!-- /STEAM-SHARED-IDENTITY-DELEGATION-SOURCE-READY-20261112 -->


2026-11-12 SourceFile公共兼容图配套已核收写回：3个原生产品头及1个原file工具，读取/发布图对同一utlmemory.h起点采用一致描述，保留完整代码/fixup、只读、固定源码尾部和非起点拒绝。17边界实际一次全过，旧版模块对照通过。首后置FAIL发现发布图仍含同一旧路径Data描述，原件保留；只续定位并补齐该一处描述，当前失败坐标已前移为client 0x991f55、UI 0xc4b398，整体仍未资格化。不重跑未变17正常组、不增默认ID/minimum。295实际include输入/8链接库已核；编译根正常退出0，3次均按已声明并核路径/SHA的vctip工具策略回收，不能称整个Job自然退出；业务观察自然结束，所有Job关前0。本SOURCE_READY只限SourceFile配套，不表示当前Steam整体适配或Running/原PID/primary/睡眠/手柄通过；未完整构建、打包或部署。原120秒追加45秒同账，全部FAIL和费用保留。原件shared-qualification-observation/source-file-publication-pairing/SOURCE_READY.json。


2026-11-12 SourceFile后继只读归因：client 0x991f55是已匹配完整caller调用未建模可执行callee；UI 0xc4b398是导入槽中的hint/name RVA，已由原caller定位实际槽并读取唯一DLL/符号。不能将其当普通不透明只读地址按section偏移强行相等，也不授原生调用资格。归因原件shared-qualification-observation/post-source-file-reference-attribution/RESULT.json；无native重跑、产品更改或现场动作。三点DD2当前已知菜单修复与Running回补保持独立验收，整体ACTIVE。


2026-11-12继续限定同一未建模callee：完整204字节除4个已核RIP操作数外188字节全相等。四引用实指相同断言文本、相同utlmemory.cpp源码尾部及同DLL/符号的两个导入；UI缺口同属导入身份迁移。下一修正应复用共同graph补完整callee/pdata/unwind及共享PE导入身份，不能改固定RVA或放宽任意只读目标。此为静态候选约束，未修改产品、编译或授当前资格。原件post-source-file-reference-attribution/CONSTRAINTS.json。


<!-- STEAM-SHARED-PRESENCE-SOURCE-READY-20261112 -->
2026-11-12：剩余sofSteamRunning私有Toolhelp枚举已移入既有唯一processInventory owner，精确写main单函数及两份原settings夹具。所有调用维持fresh0，只有Complete表能证明不在运行；未知/失败/不完整继续拒绝离线写盘。无新Store/线程/定时器，中央不支持时的Toolhelp回退和原名称规范保持。

原配方/O2一次编译核收302实际源码输入/7库、x64 console PE。29实际正常边界全过，缓存absence的1个必要反例按命名断言失败，6个无效选择实际拒绝、未落入默认业务；原默认CPP全部字节和MJS执行正文保持，不增ID/minimum。主线只做源码prepare/readback，生成字节与已编候选相同，不重跑正常组。

编译根exit0，已声明并核完整路径/SHA的驻留vctip按工具策略回收；其余8个业务/选择Job自然结束，9个Job均关前0。原报告误称全部自然退出，已前向更正，原报告/raw不覆盖。原120000ms独立消费者账约32.3秒加本状态记录实耗，不借Identity/R27/同行账；分析通讯另列。SOURCE_READY仅本函数配套，未完整产品构建/默认整组/部署或现场CPU百分比验收。交下一自然构建，不追赶重编；其他Steam账户/根目录/URI/bootstrap/broker/外观/独立库路径及R1～R6仍ACTIVE。

唯一原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/RemainingReaders/SOURCE_READY.json（SHA 3c8f48086c823b1e0bdb5eb0a76d988472f95cca97578890d1f35599452b407d）；编译工具收尾更正同目录REPORT-CORRECTION.json（SHA f2a88474b6e706bfca515feee441ee2fd39bebf87220daa6ca1fd12beeda97e6）。
<!-- /STEAM-SHARED-PRESENCE-SOURCE-READY-20261112 -->


<!-- DECKY-CLEANUP-SHARED-DELEGATION-20261112 -->
2026-11-12：Decky最终清理及inspect/confirm私有子进程已接共同Steam Observer逐请求身份委托，沿原pipe/cancel归属，不再由标记请求重复发现账户注册表。身份暂缺仍沿原无子进程、无预留等待，旧未标记协议保持兼容。只精确写2产品文件、1原夹具及2原来源声明；共同发布器和生成器原件未改，main其他并行字节保留。

原51项中的“actual native dependency callback carries verified offline account through the existing child protocol”实际一次通过：离线UI账户传达、1000次暂缺事件不耗机会、同一不存在注册表旧路径拒绝与新委托可用、真实进入子进程后撤销身份并归还。编译输入/库图及严格Observer派生已核；编译根exit0后仅按完整路径/SHA回收VCTIP工具，业务Job自然关前0。其余50项、inspect/confirm行为组、完整native/包、实机睡眠和Win5视频未跑，不拼接为完整父PASS。原失败和账保留，后继交下一自然构建，未改已安装9723031a。证据：Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/cleanup-session-receipt/delegated-child-20261112/SOURCE_READY.json；整体ACTIVE。
<!-- /DECKY-CLEANUP-SHARED-DELEGATION-20261112 -->


<!-- QAM-SHARED-STEAM-FACTS-REQUIREMENTS-20261112 -->
2026-11-12用户明确三点需求走唯一Steam层、不得私有接入Steam信息。已发YMCC-Steam层并盘点：Observer身份/账户候选、main请求run6与followup已公共；menu runtime tracked的gameprocess_log读取/缓存及identity头的game/Steam/overlay host OS证明/租约仍是待迁移缺口。要求Steam层按本次选定前台PID/birth/path统一提供process-lifetime AppID/decimal GameID、实际UI账户/browser/host父链命令行及请求期可撤销lease；菜单只保留显式动作与配置/前台/rules/power/OFF策略。无新watcher/timer/worker/全进程扫描，不借sleep/native-primary选中游戏，不省略最后身份复核，不重放未知写；menu.inspect+menuGameRead才是精确只读形状。

公共接口/生产者实施归属尚待owner回应，不能把此项写成已迁移或SOURCE_READY；本轮无产品/测试/构建/部署/Steam游戏动作，原DD2验收保留。后续仅消费owner正式公开的事实入口，不另扩私有来源。需求及源码pins：G:/YeManCC-Work/Testing/YMCC/Evidence/Touchpad-Wake-Recovery-20261111/self-heal/native-route-review/re2-live-20261112/resume-20261112/single-game-return/shared-steam-facts-requirements/REQUIREMENTS.json。
<!-- /QAM-SHARED-STEAM-FACTS-REQUIREMENTS-20261112 -->


<!-- QAM-SHARED-STEAM-FACTS-OWNER-ACK-20261112 -->
Steam层已正式确认公共按需tracker/full GameID、host/browser事实及请求期lease/失效合同由其提供；本三点任务接runtime::tracked/Cache与identity::pin/inspectHost两个消费者迁移，保留菜单显式动作和原最终围栏。公开Observer/delegation/account facts当前不包含可授资格的tracker/host owner入口，也不提供持续document或跨进程长期授权。无SOURCE_READY，不接未验候选；原需求PENDING作为旧观察保留，本后继仅确认分工，迁移仍OPEN。无新私有读取/watch、产品/夹具/测试/构建/部署/现场动作，当前DD2证据保持。原件G:/YeManCC-Work/Testing/YMCC/Evidence/Touchpad-Wake-Recovery-20261111/self-heal/native-route-review/re2-live-20261112/resume-20261112/single-game-return/shared-steam-facts-requirements/OWNER-ACK.json。
<!-- /QAM-SHARED-STEAM-FACTS-OWNER-ACK-20261112 -->


2026-11-13后继缺边已精确记录：实际离线qualify-only诊断把UI失败从0定位至共享DLL名0x10b42a0；修其处理顺序的私有候选前移到runtime-function引用0x123a598，但完整当前UI断言仍FAIL，未导入。client0x991fea由原唯一完整caller指向旧0x11666a0/当前0x118d340可执行复制实现，不能当Literal或用短前缀放行；完整callee/跳表/展开关系仍缺证，停止扩树。原诊断后置误读current而非observations的FAIL复用同raw纠正，不重跑；3正常控制及所有FAIL/费用原件保留。主线仍为先前已验公共graph配套（5头+1原工具），当前Steam整体与睡眠回补继续未通过，无完整构建/部署或现场动作。原件 shared-qualification-observation/import-identity-candidate/remaining-edges/MISSING-EVIDENCE.json。


<!-- DECKY-RECOVERY-CAUSE-TRACE-20261113 -->
2026-11-13 Decky恢复追因源码已接共同steam_failure_diagnostics：原Runtime动作Selected/Rejected/Observation不冒充派发，实际绑定/回收自有Loader/页面重建/开关大屏才分配单调actionId，开始和完成保持相同原Steam PID/birth、电源归属和episode；日志仍复用原有写者/轮转，不采URL、token、账户、原message、堆栈或脚本。Steam进程重启与BPM/document/后端轮换分列，不能用后者计整Steam重启。

原typed-admission-park-resume 1注册用例、4实际阶梯场景核收，私密内容过滤/未知code及Context和写日志回调抛错均保留原业务次数和清理断言。实际Runtime/diagnostics头逐字匹配已编来源；main调用/写者适配只做精确源码核对，完整native/包/实机未跑。普通日志保留当前writerPowerGeneration，实际动作仍沿开始时nativePowerGeneration，避免唤醒后回执被重标。首次launcher工具清单、fixture局部名/WX、长输出路径及并行main CAS拒绝全部保留。公共头有独立SOURCE_READY，精确导3产品+1原fixture；共享Steam身份后继不追入旧编译。独立bootstrap/watch源头错误捕获仍私有，不冒称已部署或已降低现场自愈频率。证据Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/SOURCE_READY.json。
<!-- /DECKY-RECOVERY-CAUSE-TRACE-20261113 -->


2026-11-13 壁纸公共Steam消费后继：已写回素材库存对共有 steamUi/steam.uiStateChanged 的订阅、单请求合并/变化去重与旧响应拒绝；大屏同态完成只调用原ForegroundWake，不撤销健康许可。原媒体消费者声明补齐新公共依赖，不加ID/minimum或轮询。首轮9过1失败及修后定向/真实pending/负例分别留原epoch，当前游戏原14/28通过，71项类型依赖闭包通过。正式当前范围仅 Testing/YMCC/Evidence/Wallpaper-Foreground-Demand-20261102/shared-steam-consumers-20261112/SOURCE_READY.json。默认后台membership私读、SourceLease跨child及同步撤销、前台installation metadata生产配套、持续UI document和完整共同sleep仍OPEN；私有候选通过不等于默认已接，不部署/不冒称全链或CPU实测。


2026-11-13睡眠共享输入消费已核收写主线：仅session()去掉3处私有注册表读，改消费唯一Observer不可变receipt，保留OS PID/birth/path/歧义、原UI账户pin及原最终请求检查；空/不可读、旧PID重用、查询期间进程/账户代次变化拒绝，同身份revision变化接受。原10组R27与新增8子边界实际通过，session私有注册表调用0；341实际include/10链接库及3个Job关前0核收，编译vctip为显式工具回收不冒称自然。原同账129414ms续作，自主追加120000ms至cap285000；旧准备漂移/锚点/缺读依赖/缺类型别名FAIL与费用保留。1产品函数+1原夹具精确CAS，保留并行game-link选择器，无新ID/minimum/producer/常驻任务。新UI源码证据另确认407字节函数7处对象偏移一致+0x10，而实际reader仍controller+0x350；这不是仅RVA搬迁，不能放宽校验后读旧布局。完整getter/owner/controller/heap消费及客户端copy/move callee仍缺证；失败私有兼容候选不导入，当前新版睡眠回补、原PID/primary/Continue/手柄仍未通过。无完整main构建/包/部署/现场动作。原件 session-receipt-candidate/published-observer-successor-r4/Closing2/SOURCE_READY.json 与 shared-qualification-observation/import-identity-candidate/ui-schema-shift/SCHEMA-EVIDENCE.json。


2026-11-13 壁纸共同睡眠消费后继已精确写主线：Host/素材库存/Ready适配使用公共isSharedPowerLifecycleAdmitted，pending与同代更正均保留撤销；Host与插件增加requestContext以补OFF尚未送达就被Ready替换时的可见性重握。原Host两项39断言、实际客户端接收正反控制/旧context重复拒绝、库存同代pending一项10断言及93项类型依赖通过；原FAIL保留。当前证据Wallpaper-Foreground-Demand-20261102/shared-steam-consumers-20261112/sleep-handshake-candidate/SOURCE_READY.json；本段未改公共power/Steam producer，原生coherent最终水印及跨child SourceLease、公共installation消费整链仍OPEN，不作真实睡眠/CPU/部署通过。


2026-11-13 壁纸公共 installation 消费已写主线：steamMediaSource.ts 移除私有registry入口，消费唯一steamInformation.get mask1；保留manifest身份/目录核验与共用缓存，不请求Presence，也不将metadata当current授权。独立核公共SOURCE_READY的7个引用、3产品头与实际IPC函数；已有31/38库存/身份回执通过实际bundle函数独立复现核收，未重跑；当前28文件类型闭包另通过。原fixture只合并库存分支，后来的共有Steam/power案例保留。

V2三撤销源+提供者寿命只完成私有消费边界准备：真实三事件子进程20/21/22、provider退出且事件均未signal的撤销、owned句柄逐一关闭/Job零及mode-only坏例分段核收，旧失败和冷全局handle变化UNKNOWN保留，不拼整父PASS。sourceReady=false，实际acquireSourceV2/export/import和默认Supervisor/Worker仍OPEN；不造公共工厂、不新增私有worker/线程/轮询。原90秒账最终76.615秒含保守准备封存，详见Wallpaper-Foreground-Demand-20261102/shared-steam-consumers-20261112/source-v2-child-candidate/HANDOFF.json。新installation源见同层installation-consumer-candidate/SOURCE_READY.json；未部署、完整Native/包或实机CPU/睡眠验收。


2026-11-13新版布局后继：开发侧固定离线capstone5.0.6，未改产品依赖。完整copy/move1661字节347指令+rep16字节/43跳表项/82引用私有合同已形成，原Public模块及loaded健康/坏目标控制保持通过；当前完整检查仍FAIL，候选不导入。拒绝点已从0x991fea前移至0x9d35dd/0x9d3615，实际67字节tick尾部 [rsi+0xc80]→[rsi+0xc88]，账号getter同形候选 -0x1c66→-0x1c6e但各版3个且归属未闭。UI已有7处字段+0x10，不能只改地址/日志放行旧reader。旧UI短getter无命中原因为未扫描，不能称不存在；caller已定位当前8字节globalLeaf，纠正独立封存。一次原图字符串诊断得到4同文+1utlvector.h构建根变化，未授资格。原17/默认29/整轮未重跑；两新完整当前定向FAIL与原件/费用保持，6自有Job关前0，编译工具回收另列。无新主线产品写入/部署/现场动作，目标ACTIVE。原件 shared-qualification-observation/import-identity-candidate/complete-research-closing/INTAKE.json、REPORT.zh-CN.md、EVIDENCE-CORRECTION.json。


2026-11-13 原生布局后继：账号getter归属已由精确CUser RTTI/IClientUser PMD/slot10闭合（当前0xcfed0，接口0x1e78，account仍0x20a），不再沿用三个同形候选未区分的旧结论。实际构造/完整walker同root交叉核对用户表0x1080/1090→0x10c8/10d8，完整lookup/构造核对记录表0x2568/2578→0x2570/2580；record stride0x330、GameID+8及predicate非RIP字段保持。恢复成员精确类型CUserAppManager现位于CUser+0x3870（旧0x3800），原CUser回指仍+0xd8；不同幅度不能全局+8。主线实际reader/callback仍旧布局，当前完整client/UI资格仍FAIL，不能只改地址放行。两准备归属FAIL和费用保留，原17/29/default未重跑；本阶段无产品写入/部署或Steam/游戏/YMCC/设备/睡眠操作。事实已发送统一Steam层和三点菜单任务；非菜单因果或Running恢复证明。唯一后继：Testing/YMCC/Evidence/Win5SteamSleepRebind-20261111/QamFocusReview-20261112/shared-qualification-observation/import-identity-candidate/typed-layout-closing/INTAKE.json及REPORT.zh-CN.md。目标ACTIVE；下一缺边为实际非虚publisher方法/owner字段、真实reader/OSD配套及loaded证明。


<!-- WALLPAPER-NATIVE-SHARED-SOURCE-CONSUMER-20261113 -->
2026-11-13 壁纸原生前台消费已精确写主线：reader 使用共同 observedSource/current/modeRevision，默认0为Unknown；Facts记公共版本以区分同文档真实撤销重发，两个尾回调只Wake并水位去迟到，不异步撤掉新票。最终睡眠事实直接使用正式公共 capturePower/powerCurrent；物理窗口、held UI/root、会话、OFF、AC/DC及末端检查保持，无新Steam reader/轮询/线程。

原ForegroundBoundary分来源10/57、连续性后继2/12、正式power配套3/20、默认0后继1/7及4个必要反例分别核收，不拼完整父PASS，不重跑Mode14/睡眠producer原组。20自有Job关前零，VCTIP工具回收单列；SDK声明/准备路径/PE元数据/公共ABI晚变FAIL全部保留，补pin只复编复现已测PE，无业务重跑。两产品文件保留并行字节。整链仍ACTIVE：默认后台membership私读、公共V2跨child factory/export/import仍OPEN，不引私有默认路径。未完整产品构建/打包/部署或真实Steam/睡眠/CPU验收，交下一自然Native/包。原件Testing/YMCC/Evidence/Wallpaper-Foreground-Demand-20261102/shared-steam-consumers-20261112/foreground-source-candidate/SOURCE_READY.json及同层MAINLINE-IMPORT.json；整体验收接REPORT-ForegroundShared.zh-CN.md。
<!-- /WALLPAPER-NATIVE-SHARED-SOURCE-CONSUMER-20261113 -->


2026-11-13 发布/getter后继：真实唯一56字节part+CHAININFO定位新publisher0x4f8240，全部8部1357字节308指令（旧1185/272），栈帧188→178、原全局用户枚举通知改用manager+d8所属CUser；方法先前全局UI投递仍在，不能把新callee未核当恢复完成。getter ABI1候选已实际编staticlib/DLL；原13场景×两位移，static/DLL各26实际getter_runs/32070断言通过。首次JSON转发头C1083及费用保留，后继复用3成功native步骤仅续fixture；8自有Job均关前0。候选仍私有SOURCE_READY=false，主线member3800/旧getter/prologue188及embedded尚未配套，完整当前资格/实机仍OPEN，动态/半静态路线保留。三点菜单原DD2窗口无同原实例finished job不作因果排除，type2源码门不代签现场误分类。原17/29/default未重跑，智能体497秒/超17秒与并行重叠UNKNOWN均保留，原执行账追加180秒到705秒，不豁免历史费用。无产品导入/部署或现场操作；目标ACTIVE。后继Testing/YMCC/Evidence/Win5SteamSleepRebind-20261111/QamFocusReview-20261112/shared-qualification-observation/import-identity-candidate/publisher-getter-closing/INTAKE.json及REPORT.zh-CN.md。


2026-11-13 当前布局配套后继：root/vendor共用纯布局数据，原28/102断言、21/84及旧Public健康对照；UI原17+旧Public共18项分别核收。新getter/publisher/member3870/帧178已配套trial DLL→精确嵌入头→host编译，复用已验ABI1库；原engine12定向+6当前边界通过，错误库摘要实际拒绝。32真实准入Job与2未准入空Job关前0分别记录；两次准备FAIL/UNKNOWN及费用保留，仅续未到达用例、未重编已成功输出。仍是私有exact-pair动态候选，SOURCE_READY=false；当前半静态完整graph/loaded/callee及实机Running/primary/Continue/手柄OPEN，不能称通用或无缝恢复。原17/29/default未重跑，无产品导入/部署或现场操作，目标ACTIVE。唯一后继Testing/YMCC/Evidence/Win5SteamSleepRebind-20261111/QamFocusReview-20261112/owner-scoped-readers-pair-closing/INTAKE.json及REPORT.zh-CN.md。

<!-- GAME-STEAM-TYPED-CONSUMER-AND-LIFETIME-20261113 -->
2026-11-13：共同游戏/Steam消费端新增有界冷读衔接，两个唯一事实层仍各自拥有游戏PID/EXE与Steam身份。显式前台B使用自己的闭合scope，和全局refresh共用原GameLink工作线程及唯一TrackerOwner；阳性cache hit不发demand、不等待、不读时钟。冷读的750ms绝对截止为本次新策略；结果分缓存/本次关联、真实负源、不可用、超时和取消，只有原菜单caller持最后UI/Host/焦点门并执行一次，worker不持栈callback或派发动作。

原Observer/ProcessLease/TrackerOwner/readTail的8项新场景30断言及单语义反例已通过；transport为有界测试端口，不冒充原queue整组重跑。原get/capture全文前缀保持。客户端read/refresh可选signal，等在途查询共用IPC、独立退订，最后一位退出才取消IPC；公开纯客户端epoch失效订阅避免页面复制源事件清单，无新增timer/线程/事实缓存。取消5组及后继epoch2组分别按原件核收，reader实际类型闭包通过；旧提取noexcept准备FAIL、fixture getter适配FAIL保留，不拼成整父PASS。

仅公共header和reader已精确写主线；菜单一次button消费、Decky非阻塞可选显示由各owner独立后继，尚未用其结果代签。本段未完整当前main编译/链接、新包、部署或现场Steam操作。唯一证据：Testing/YMCC/Evidence/SteamGameIdentity-20261113/cache-consumption/MenuColdDispatcher-20261113/SOURCE_READY.json及MAINLINE-IMPORT.json；wholeGoalComplete=false。
<!-- /GAME-STEAM-TYPED-CONSUMER-AND-LIFETIME-20261113 -->


2026-11-13 UI结构后继：8完整家族/10parts/139范围/338引用/6导入候选，原公共文件与合成loaded19项实际通过；另只续完整globalLeaf移动4项，正式qualifier按incoming定位新位置，旧引用/破坏拒绝，未用SHA授资格。两次新增图分类FAIL与费用保留，8自有真实Job关前0，未重跑19/原17/29/default。发布callee父级逐字再核：metadata148B仍有engine1410→1458缺owner；旧CUser通知完整5parts461B/127指令而当前85B/28，不能只改RVA。agent整体192866/180000ms超12866保留并关闭。仍私有SOURCE_READY=false，client/publisher完整图/loaded和现场Running/primary/Continue/手柄OPEN；新UI头不得混用旧trial/pins。无产品导入/部署/现场操作，目标ACTIVE。唯一后继QamFocusReview-20261112/owner-scoped-ui-callee-closing/INTAKE.json及REPORT.zh-CN.md。

<!-- DECKY-OPTIONAL-STEAM-DISPLAY-20261113 -->
2026-11-13 Decky 侧可选 Steam 元信息改为独立异步展示：丢回包不阻塞 TDP/输入/帧率/监控的原核心读取。共享 reader d984/7895 仍唯一；本侧只保可见 DTO，隐藏/源 epoch/游戏 ABA/电源代次/停止撤销自身订阅，晚回只使原 relay 重绘一次，不 warm、不新建 timer、不改变动作准入。

原真实 host/relay/reader 六个精确场景旧源一过五失败、候选六过，显式监控意图不重放；185 个实际 TypeScript 输入通过。仅一产品文件与两个原夹具精确主线写回，5 个含失败的实际准入 Job 关前零。准备/模块接线/首类型漏 JSON 的 FAIL 与费用保留；不重跑公共 39/25/host8 或默认整组。后继证据 Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/joint-read-stall/SOURCE_READY.json、CLOSING.json。Bootstrap/watch async socket 的泛化错误与公共 CDP socket 关闭码是两条覆盖，前者仍 OPEN。未整包构建/部署或操作 Steam，实机恢复频率与总 CPU 收益未证，目标 ACTIVE。
<!-- /DECKY-OPTIONAL-STEAM-DISPLAY-20261113 -->


2026-11-13 record/发布组件后继：13完整记录家族/29parts/350范围/1236引用/28导入，原合同与精确复用loaded前缀28项核收；修正新增graph把可变.data初值当运行常量的误拒，旧record/UI反例与新正常/坏RTTI共6边界实跑，旧UI19/4保留原合成范围不重跑。发布变化6家族17parts127范围/329引用/8导入与record合并另12项通过，但完整publisher仍拒绝，agent时点37代码/186非代码当前对应OPEN，需先核record重叠/实际消费。engine1458经typed engine主槽14→9342d0返回赋值及COL/PMD闭到CRemoteClientManager、secondary+8虚槽318→939a60，live/槽语义未证。9afc20/9b08c0校正为更支持诊断打印的静态角色。全部准备/编译FAIL保留，5实际Job关前0；agent340995/360000ms收尾。原账AI追加180秒至885秒一次，端到端准备UNKNOWN保留。仍私有SOURCE_READY=false，无导入/部署/现场动作，目标ACTIVE。后继QamFocusReview-20261112/owner-scoped-record-publisher-closing/INTAKE.json及REPORT.zh-CN.md。

<!-- GAME-STEAM-CONSUMER-TEMP-GOAL-COMPLETE-20261113 -->
2026-11-13 后继核收：本聊天临时“唯一游戏 EXE/PID 层接入独立唯一 Steam 层”的源码消费任务已完成。共同 get 保持纯缓存，显式 refresh/菜单冷读共用原唯一 TrackerOwner 和 GameLink worker；历史关联不授动作权限。菜单三个头/八处原 main 接线及 Decky 非阻塞可选显示已由各 owner 精确写主线，并由游戏层独立读回核收，私有 Steam 解析/缓存已撤换，迟到或取消不重放，侧栏隐藏只撤自身订阅。

原 1001 次缓存零发现与同目标单次物理观察、后继 typed 八项及单有界内存修复分别保持原 epoch；菜单十八场景二十九断言和精确语义反例、一次固定完整 main 语法 1398 输入，Decky 六场景/185 类型图已核原 raw 与当前接线。只读核收未重跑业务或追赶并行 main；主线历史整文件 hash 不当当前来源证明。当前完成审计：Testing/YMCC/Evidence/SteamGameIdentity-20261113/cache-consumption/MenuColdDispatcher-20261113/GOAL-COMPLETE.json；菜单独立核收 MENU-FORMAL-CONSUMER-INTAKE.json，Decky DECKY-CONSUMER-INTAKE.json。

这里只关闭上述临时源码接入目标；整个 Steam/睡眠/typed 资格项目、下一自然完整构建/配套包、部署和真实游戏/睡眠验收仍由各自任务保留，未测安装版 CPU 百分比。所有旧 FAIL/UNKNOWN 与费用不改。
<!-- /GAME-STEAM-CONSUMER-TEMP-GOAL-COMPLETE-20261113 -->

<!-- DECKY-ASYNC-NUMERIC-TRACE-20261113 -->
2026-11-13 Decky async WinHTTP 的现有回调/WS同步失败带回原操作数字错误码及源阶段，经原 steamFailure/failureRecord 对应原动作编号与恢复 episode。原 reason、额度、截止、取消和 callback lifetime 不变；无新探测、timer、线程、重试或重启。真实回环直接消费者11项，后继监听/日志消费者7项分别通过，产品头逐字相同；原双空元信息负例、首新测试误消费通用record的FAIL及费用保留，后继只续watch7。

三产品头/两原夹具精确写主线，5个实际编译图与10个实际Job关前零，已核身份的编译工具强制收尾分列。仅初始inject catch编译/源码核，不冒称真实Steam/Runtime全链；未重跑默认103或完整bootstrap，未整包/部署/操作Steam。优雅WebSocket关闭码仍单列OPEN，公共CDP关闭码不覆盖本async通道。证据 Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/async-transport-failure/SOURCE_READY.json及CLOSING.json。实机自愈频率/CPU仍未证，目标ACTIVE。
<!-- /DECKY-ASYNC-NUMERIC-TRACE-20261113 -->

<!-- DECKY-ASYNC-CLOSE-STATUS-20261113 -->
2026-11-13 Decky自身async通道的优雅关闭码补齐：实际收到CLOSE后，原串行owner对原句柄只查询一次。1000/1011/空1005按真实回执记，固定123字节reason不进日志；查询失败保持unknown且不输出部分写入的code。当前Windows对1012在交付CLOSE前返回12152，direct/watch真实观察均保留原WinHTTP数据，不伪造close。原reason、截止、取消、callback lifetime与恢复额度不变，无新网络探测/timer/线程/重试。

原消费者direct60/watch25定向通过；原bootstrap缺元信息对照引用、watch新支持1011反例实跑，旧错误预期与新测试auto编译FAIL/费用保持，7实际编译图、15实际Job关前零，编译工具强制收尾单列。仅1产品头/2原fixture精确主线写回。上轮11/7及默认完整组/Runtime全链不重跑，未真实Steam/整包/部署/物理睡眠；本机留存旧writer三路健康不算新源码收益。完整目标ACTIVE，实机CPU和恢复频率未证。后继 Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/async-close-status/SOURCE_READY.json及CLOSING.json，取代较早“本侧关闭码OPEN”的当前状态，历史原件不改。
<!-- /DECKY-ASYNC-CLOSE-STATUS-20261113 -->

<!-- DECKY-HEALTH-OBSERVATION-FAULT-20261113 -->
2026-11-13 Decky真实Runtime的健康读取异常误重开恢复轮次已复现并修复：原catch只有reason，默认required/paused=false被当资格丢失；现仅暂停原观察，原episode/stage/截止保持。连续异常/实际读取返回只记两个诊断边沿，不复制异常原文，不把读取返回当三路恢复；无新线程/timer/探测/动作权限或额度。

相同Steam身份下实际生产Runtime（无legacy）注入16次异常，原决策reconnect轮次[1,2]/误stopped[1]/recovered[2]，候选[1]/无误停/recovered[1]。精确1场景7最终语义检查，19健康读取，admission始终拒绝，preflight/UI动作/Loader/Steam均0；OFF正常join且晚读拒绝。5实际Job关前0，工具强制收尾单列；混合EOL准备拒绝和首夹具Windows/Winsock顺序编译FAIL及费用保留，后继只修头顺序续测。12最新Win5日志未命中此异常，不归因现场。仅1产品头/1原夹具精确写回；原阶梯类/期限字节未改，默认完整组/整包/部署/物理睡眠/CPU与现场频率仍OPEN。目标ACTIVE。后继 Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/health-observation-fault/SOURCE_READY.json及CLOSING.json。
<!-- /DECKY-HEALTH-OBSERVATION-FAULT-20261113 -->


2026-11-13 完整当前Steam私有候选后继：原正式文件/合成加载17项（含旧Public/完整getter移动）保留原epoch，最后19项缺口已逐角色核收，原237范围无未解释项。两段含LF文本从8字节补到完整37/49字节；四个真实叶函数边界pdata保留，修前5项证实漏口、修后仅受影响18项通过，原编译/准备FAIL不改。当前发布图21家族55parts357范围1003引用14导入；新UI/record/publication三头成套加入embed sourcePins，DLL/内嵌头/恢复Host实际配套编译5Job0，另定向5Job0，旧17两Job仅引用。Remote槽直接遍历/子谓词角色已核，间接focus仍OPEN；agent超3005ms保留。原账自主加120秒至1005秒一次，端到端准备UNKNOWN不豁免。仍私有SOURCE_READY=false、无产品导入/部署或Steam/游戏/设备/睡眠动作，目标ACTIVE；后继QamFocusReview-20261112/owner-scoped-current-qualification-closing/INTAKE.json及REPORT.zh-CN.md。

<!-- DECKY-AUTOMATIC-HEALTH-TIMER-20261113 -->
2026-11-13 补显式真实Runtime自动观察/线程CPU边界，产品未再改。故障设置后无手动重连/检查/Observer通知：修复候选实测5000ms原evidence cadence再次读取，随后自动发现健康返回，同episode1；10016ms测量窗仅2轮工作循环，总读取4。旧修复前Runtime在6500ms观察窗未再读，不据此称30秒后永不恢复。精确自身worker只读句柄GetThreadTimes差0，退出后该确切句柄已观察结束；0可能受粒度影响，不代表绝对零CPU或安装版峰值/平均通过。

6最终语义检查/2非法选择拒绝、2实际图/6实际Job关前0，工具强制收尾和初准备锚点误计FAIL分列。仅原recovery fixture增--health-observation-timer显式入口及非法选择拒绝，默认阶梯正文/旧fault helper逐字未改；不默认增加长测、不重跑旧7/default。当前Runtime/阶梯头与所执行候选相同；旧7仍按原epoch引用，不将源码相等写成新实跑。无实际Steam/Loader/UI/设备/整包/部署，物理睡眠与现场CPU/恢复频率继续OPEN，目标ACTIVE。后继 Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/health-observation-timer/SOURCE_READY.json、CLOSING.json；原账AI按实需追加180秒至1260秒，不退款或改旧原件。
<!-- /DECKY-AUTOMATIC-HEALTH-TIMER-20261113 -->


<!-- WALLPAPER-PUBLIC-V2-DEFAULT-WORKER-CONSUMER-20261113 -->
2026-11-13 壁纸默认后台已接公共SourceLeaseV2：Supervisor一次捕获，原headless入口消费3个SYNC事件副本+QUERY_LIMITED/SYNC provider+只读snapshot mapping的五项HANDLE_LIST；校验同一held父PID/birth、原directParent/session、安装路径/文件身份/SID，并保持原token/挂Job后Resume/超时/优先级。只改专用副本继承位，通用继承窗口共用进程内锁。原lease持至实际root/成员signalled及Job0，异常同账保留。Worker三处私有RegGetValue移除，无私有回退；来源Unknown在pending及verified快捷方式均按原20秒只延期本lane，其他下载继续，OFF/power/取消门保持。状态文本与backgroundSteamSourceProtocol=1/原导出源码声明配套。

原私有child8不重跑；真实factory/default跨child7/109、2必要坏例、exporter4、原两项25/59、诊断失败父前缀72与只续verified9/65、原network自动20.163秒、cacheIO51和当前公共头parent/child Zs335/337分epoch核收，不能拼完整父PASS。42准入Job关前零，另1未准入空Job UNKNOWN单列，VCTIP工具收尾另列；所有FAIL/UNKNOWN及费用保留。crypt32补pin仅复编8个PE独立字节复现，无业务重跑。精确导14文件/3新头/39片段，保留并行字节，不加私有producer/Observer/常驻/轮询。

当前源码配套SOURCE_READY，仅本壁纸consumer范围。完整Native/Web/CustomSteamLibrary新包、部署、现场CPU和真实睡眠仍OPEN；下一自然构建须带新主程序+库协议，不追改旧包、不自动启动Steam。原件Testing/YMCC/Evidence/Wallpaper-Foreground-Demand-20261102/shared-steam-consumers-20261112/default-worker-source-candidate/SOURCE_READY.json及同层MAINLINE-IMPORT.json，报告同项目REPORT-DefaultWorkerShared.zh-CN.md。
<!-- /WALLPAPER-PUBLIC-V2-DEFAULT-WORKER-CONSUMER-20261113 -->


2026-11-13 当前Steam布局配套已入主线：原完整图17/修后18保留epoch，本后继实际两路engine2项及正版type0/错配拒绝通过；按fresh主线hunk/EOL保留并行字节，20源/来源+内嵌头/原schema receipt/原构建检查共23路径写回，配套重建41695ms。现有build source guard6正反/选择控制已核，后继只修同工具健康同目标/Ready转发两条过时断言，原失败父3前缀及修后20+两反例分epoch，不拼完整23父。实际21唯一Job关前0，584ms有效窗TIMEOUT/强制收尾与全部FAIL保留；原账工具窗加120秒至1125秒一次，准备/重叠UNKNOWN不豁免。Remote子谓词961040完整70B25指令无直接focus写，内部960fd0/间接因果OPEN；agent142776/180000，旧超3005保留。SOURCE_READY仅当前布局配套组件，整体ACTIVE；公共native身份/power scope已反馈唯一Steam层，不私造producer或借候选读发布。无部署或Steam/游戏/设备/睡眠动作，完整main/link/包、Running/Continue/手柄/S3/S4/三点现场仍OPEN。唯一后继QamFocusReview-20261112/owner-scoped-mainline-recovery-closing/SOURCE_READY.json、INTAKE.json及REPORT.zh-CN.md。


2026-11-13 实际snapshot/capture/driver后继已写主线9路径：读取self/account统一消费既有布局；OwnerScoped不再误选Legacy；完整14字节getter按已选profile核验，manager仅诊断。新增capture前后原实例/serial及开始Native power代次日志，附加期间无新增callback/IO，未确认detach不升成功；QueryActor/普通IPC初始化早于begin仍OPEN。地址3/捕获身份3/真实relay4断言/driver2分epoch，最终5步41618ms及1配对4326ms、32唯一Job关前0；40声明依赖未变。最终DLL28b16837...，中间876不导；HOST-SOURCE实际生产副本/原offline正文及单vendor pin变更已核，准备与导入字段适配FAIL及私有copy纠正保留。Remote当前960fd0/旧93bf60各109B27指令无直接focus写，两个内部调用与导入间接因果OPEN，不扩树；192898/180000超12898与旧费用分列不重叠相加。仅本配套组件SOURCE_READY，公共native Steam/account/power scope与最终取消、完整main/包/部署、实机Running/Continue/手柄/S3/S4/三点仍OPEN，整体ACTIVE，无现场操作。唯一后继QamFocusReview-20261112/capture-consumer-mainline-closing-r2/SOURCE_READY.json、INTAKE.json及REPORT.zh-CN.md。


<!-- TRACKER-SOURCE-SCOPE-PROJECTIONS-20261113 -->
2026-11-13 唯一 TrackerOwner 已补同一实际源 revision 的 ProcessLifetime / RunningList 日志投影及原解析器完整 AppID/PID 成员集合。只切行一次，保留每flight一次原bounded读取；原2MiB字节预算、出生窗口和完整64位身份保持。原实际producer的成功/负观察均保留两种scope，正负memory peek均隐藏新增解析投影，历史关联不升级为当前RunningList、native Running、账户或动作权限。不新增reader/cache owner/线程/轮询，成员集合不能替唯一EXE层选游戏。

原Observer显式扩展 --tracker-source-projections 实际31条断言通过，含真实私有注册表、三个惰性子进程、实际ProcessLease/文件读取/提交/负观察；7次实际源读取，1001次peek零新增源及clock读取。两必要反例各精确失败1条，选择器后继仅3项非法输入拒绝；10自有Job关前零，编译工具强制收尾分列。默认正文及旧39/8/菜单组未重跑，准备FAIL和费用保持。两产品头及原fixture精确主线写回，来源/selector epochs分列，不拼完整父PASS。

SOURCE_READY仅上述公共producer扩展。FreshBoundary请求接口、GameLink初次/后pin/活跃消费、console primary两源join、sleep inspect截断语义及绝对捕获/最终鲜读仍OPEN；不可用历史缓存替代原第二鲜读或降低native强proof。完整Native/包、部署与真实睡眠/CPU验收另列，整目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/TrackerSourceProjections-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json及HANDOFF.json。
<!-- /TRACKER-SOURCE-SCOPE-PROJECTIONS-20261113 -->


2026-11-13 原生恢复公共scope需求已按9源20段实际函数/类型交接：候选账户不得升写票，attach前原CUser/完整GameID/record/primary及账号代次current需要公共来源；唯一Native capture/current/observe/粘性撤销保留至清理，不借UI文档。已明确sameScopeNow无accountEpoch及ABI3 Packet/Steam内最后publisher无公共power/account撤销委托，host Current/deadline不能代签最终边界。另原完整QueryActor两路线各4处取消后推进已离线复现，4检查候选14项通过、host编译/归档完成，4准入Job0；Steam调用为端口，真实SDK取消/有界返回和焦点仍未证明。首次重复pin准入UNKNOWN无编译，费用保持；候选未写主线/SOURCE_READY=false，此前9路径28b逐字保持。按公共owner新委托只交源码/类型/需求，不继续构建/采样或私造producer。无现场/部署，目标ACTIVE。唯一后继QamFocusReview-20261112/native-scope-requirements-20261113/REQUIREMENTS.json、CURRENT-BOUNDARY-CLARIFICATION.json、REQUIREMENTS.zh-CN.md及INTAKE.json。


2026-11-13 三点/回补关联补查已得具体字段缺口：屏幕proven-game请求有完整GameID及Steam/game/host出生/browser，但完成association仅gamePid/birth与本地request/flight；JS routing实际三分类字段不含完整元组。writer日志power是记录时观察值，sink原样写detail，不等于动作准入power。只建议菜单owner投影已合格请求和既有准入字段，未知保持未知，无新producer/查询/轮询；首空trace假设FAIL保留。公共R15的713/32Job及两TU原依赖核收仅引用，不重复工作；三个Native scope缺口仍OPEN，QueryActor私有候选不代签/未导。无构建/业务/现场/产品写，目标ACTIVE。后继QamFocusReview-20261112/menu-capture-correlation-gap/SOURCE-FLOW.json、SINK-SUPPLEMENT.json、PUBLIC-R15-INTAKE-REFERENCE.json及REPORT.zh-CN.md。


2026-11-13 四Current取消检查后继已导主线：1产品/3原夹具配套；资格后加载前、ConnectToGlobalUser/GetISteamUser/GetSteamID返回后复用原Current，不新造Steam/power producer。此前实际14行旧新actor及host编译/归档引用，本次零重跑/构建；反向4hunk字节及原fixture默认AST已核。47host依赖、先前8路径、28b DLL/25sourcePins保持；原4Job0、空Job准入UNKNOWN/费用不改。只签本取消检查源码，不签真实SDK可取消/有界或三点根因；三公共Native端口、完整main/包/部署和实机OPEN。旧未导候选记录按原epoch保留；本后继为QamFocusReview-20261112/query-actor-mainline-closing/MAINLINE-IMPORT.json、SOURCE_READY.json及REPORT.zh-CN.md。整体ACTIVE，无Steam/游戏/设备/睡眠操作。


2026-11-13 菜单完成关联字段缺口已有正式源码后继并由回补侧只读核收：12个当前owned hunk唯一，原source反向/调用序列保持；原单screen-shortcut-same-action正例10断言及漏hostCreated反例、两图各298实际输入、4Job0已核，2次精确工具回收不称自然。queue前原power、run游戏scope power与完成writer值分层，完整合格元组来自原请求，negative/unbound与writer出生仍UNKNOWN。公共依赖变更按原编译epoch保留，不签整当前main/包；本侧零重跑/构建/产品写。只收源码诊断缺口解决，不收三点现场因果。三Native端口与实机仍OPEN，整体ACTIVE。原件QamFocusReview-20261112/menu-capture-correlation-gap/MENU-CONSUMER-INTAKE.json；菜单SOURCE_READY在Touchpad-Wake-Recovery-20261111/QamSharedMenuAction-20261113/correlation-projection。


<!-- GAMELINK-PUBLIC-FRESH-PAIR-20261113 -->
2026-11-13 GameLink 原 tracking 三处调用已接唯一公共 TrackerOwner 的新源入口。新需求遇旧 flight 返回 Busy，保留初次/后pin的两次独立强读；原 token 持至 gameprocess 与 console 两份独立有界观察和实际 held lease 末端核对结束。公共读取复用原 readTail、原 scope/完整库存与 native-primary parser/join；缺 console 的单成员 fallback 仍不称 verified，截断/非本地/移除/换主PID保持拒绝。没有新reader/缓存owner/队列/轮询/线程；内存peek不能代替本次源对。

旧根严格大小写比较实际14 FAIL保留，原 Busy1 有效前缀单列，按原路径规范化修后只续18项。实际 Observer/ProcessLease/文件/producer/GameLink 原join均联跑；原确定性SleepOnly端口配套37项亦实跑，后置PASS解析仅核同raw纠正，无业务重跑。必要旧flight订阅反例精确失败1项；9自有Job关前0，编译工具强制收尾分列，首fixture编译与准备失败保持。4产品/2原fixture精确写主线，来源分epoch，不拼Full/完整父PASS。

SOURCE_READY仅本GameLink源码链。sleep inspect截断诊断、绝对捕获/累计活跃预算与最终准入仍OPEN，console原始观察不是原子快照或native heap/Running权限；完整main/包、部署和真实睡眠/CPU继续单列，整体ACTIVE。唯一证据Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/GameLinkFreshSource-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json及HANDOFF.json。
<!-- /GAMELINK-PUBLIC-FRESH-PAIR-20261113 -->


<!-- STEAM-LOG-SOURCE-SLEEP-CONSUMERS-20261113 -->
2026-11-13：原 TrackerOwner 新增仅绑定实际 Steam PID/birth/root/session 的日志源 key，复用唯一 MainReadTailPort 和原句柄持有。游戏 Ready-only scope 不变；睡眠捕获、最终独立核对与 inspect 已消费公共源，原绝对 FILETIME 边界、累计 QueryUnbiasedInterruptTime 预算与强证明不得降级保持。截断只供诊断；身份未知、预算取消和读取后失效不再冒充实际缺日志保底，实际缺 Console 的原单成员保底仍为未验证。gameprocess/console 使用各自实际物理源 revision，无新reader、缓存owner、线程或轮询。

真实公共源16项、实际源接原睡眠 Console4项通过；移除读取后存活检查的精确反例失败1项。原 SleepOnly 游戏选择37（含实际 inspect dense 消费）、捕获预算30、唤醒准入26按精确原名实跑通过；13个实际 Job 关前0，工具收尾单列。准备重复pin拒绝/C2373/固定verifier修改事故与旧费用保留，不拼默认完整父通过。四产品/两原夹具精确导主线并保留并行字节，元数据只声明原readTail实际字段。唯一证据 Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SleepLogSource-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json 与 HANDOFF.json。

本后继只关闭这条日志消费者源码链，上一 GameLink 记录中的 sleep捕获/inspect待迁移状态保留为历史epoch。完整 Native/link/包、公共层其余资源/布局/电源资格及实机睡眠/CPU/恢复频率仍 OPEN，整体 ACTIVE；本轮未部署、启停Steam/游戏或操作设备/真实睡眠。普通导出夹具 NOT_RUN。
<!-- /STEAM-LOG-SOURCE-SLEEP-CONSUMERS-20261113 -->


<!-- STEAM-LOG-SOURCE-GAMELINK-REGRESSION-20261113 -->
2026-11-13同源回归补验：复用本轮P实际编译产物，仅运行原 --tracker-fresh-gamelink 19具名场景，实际公共Owner/原ProcessLease/源对/GameLink全部通过；新增编译0，不重跑公共源16。额外1个真实Job自然归零，连原阶段共14个实际Job关前0，原准备拒绝单列。六个导入源码当前SHA仍吻合。该后继不改变旧13 Job阶段记录或原失败，整目标继续ACTIVE，未部署/实机验收。证据 Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SleepLogSource-20261113/REGRESSION-SUPPLEMENT.json。
<!-- /STEAM-LOG-SOURCE-GAMELINK-REGRESSION-20261113 -->


2026-11-13 R17确认公共三Native端口仅规划、无其方实现任务；不能以聊天活跃当执行。回补侧已自行准备第2项私有消费候选，实际公共类型/登记及原Current/observation/准入代码17定向检查通过、2Job0，电源Reader为内存端口，无Steam/睡眠动作。不导入/SOURCE_READY=false：新增strict capture会遇到现有phase Ready→通知回补→openHardwareWriteGate顺序窗口，需唯一睡眠层提供一致post-gate Ready；只证明源码/契约，不称真实交错或历史根因。未知在Admission前返回，pre-suspend只读不挡；完整QueryActor/debug/remote清理与Native取消event等待未执行/接入，另两端口OPEN。普通Control重建sequence碰撞猜测已按当前可达路径排除，不改账。原账为明确新覆盖追加120秒至1485秒，历史费用/FAIL保留；无产品/部署。唯一后继QamFocusReview-20261112/native-power-consumer-boundary/CANDIDATE-INTAKE.json、BOUNDARY.json、REPORT.zh-CN.md及native-power-consumer-execution/RESULT.json。


2026-11-13 Native撤销消费后继：私有wait14/清理8/原actor清理持有4分别实跑通过，另3旧源反例成立，不拼整父；原17/actor14/菜单/公共commit不重跑。候选同次wait接借用Native event、隔离ownerHeld诊断异常并把driver WAIT_FAILED按未确认退出保留；SDK/进程/分配失败均明确端口，真实debug/remote完整归还和Steam内最终撤销仍OPEN。睡眠owner已确认post-gate两路原先未接回补，正在唯一投影补接；本侧未获其正式交错证明，不导入strict gate。Unknown未消费Admission不等于自动重排，旧pending注释待改。10真实准入Job0/1未准入UNKNOWN分列，编译与核收FAIL/成本保留；原账自主加300秒至1785秒，不重复人类60秒。无产品/部署/现场，目标ACTIVE。唯一后继QamFocusReview-20261112/native-power-lifetime-followup/CANDIDATE-INTAKE.json及REPORT.zh-CN.md。


<!-- NATIVE-READ-SCOPE-POWER-RETENTION-20261113 -->
2026-11-13原生读取范围与电源持有已配套导主线：唯一Observer新增独立NativeReadDelegation，保留实际publisher快照/processEpoch/accountEpoch、tuple/session及粘性事件，账户候选零/不可读仍只用于资格读取，不借UI文档、UiLocal或candidate相等授NativeQualified/发布票。原Steam实际pin与原Native capture/current/observe Registration随恢复Current/observation回调持有，贯穿QueryActor join及词法debug/remote清理；原实例清理不要求重新Ready。两路仍各自资格/额度，另共享同次operation范围，最终汇总不能在账户/电源撤销后复活前路成功。

真实Observer15场景137断言、原双路消费者6场景35断言及跨路后继1场景3断言各分epoch核收，旧6未追跑，不拼当前7/default父PASS；四必要反例各精确失败1场景，涵盖实际账户ABA事件、拒绝入口、观察fallback及跨路最终复活。16准入Job关前0，限定编译工具收尾单列。两依赖编译FAIL、换行/误取默认分支/默认参数/负例锚点准备拒绝和智能体准备失败及费用均保留。6既有+3新配套路径精确写回，保留并行字节，无新producer/线程/timer/EXE/ID/minimum。

此前R18“三端口仅需求”是历史epoch：本后继仅前两端口范围SOURCE_READY；Steam内最终publisher撤销委托仍未实现。真实SDK取消/有界返回、原debug/remote完整资源归还及常驻DLL静默归还不由源码持有证明替代。完整main/link/Web/plugin/库包、部署和现场Running/Continue/手柄/S3/S4及CPU/恢复频率仍OPEN，整目标ACTIVE。普通导出夹具NOT_RUN；无真实Steam/游戏/设备/睡眠动作。唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/NativeReadScope-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json、HANDOFF.json。

<!-- /NATIVE-READ-SCOPE-POWER-RETENTION-20261113 -->


<!-- NATIVE-READ-SCOPE-SELECTOR-CONTROLS-20261113 -->
2026-11-13 Native读取夹具选择器补核：复用P同一真实编译产物，空case、未知case、混合mode三项实际退出1，在创建fixture目录/注册表/子进程/Observer之前拒绝；无业务case和新增编译，不重跑原15/6/跨路1。三Job自然0，连原阶段共19实际Job关前0（12自然、7精确工具收尾），不改原16 Job阶段原件。九路径当前SHA均吻合。只补选择器覆盖，最终Steam内publisher委托/整包/现场仍OPEN，整目标ACTIVE。证据同NativeReadScope-20261113/SELECTOR-SUPPLEMENT.json。
<!-- /NATIVE-READ-SCOPE-SELECTOR-CONTROLS-20261113 -->


2026-11-13 私有配套编译后继：完整engine编译/归档及实际main转发+固定runtime声明编译，340/180实际输入、3真实Job0、25868ms；无行为组重跑/trial重建。当前公共NativeReadScope并行导入先触发旧runtime散列拒绝，后继仅私有固定旧声明，未编runtime全体/当前main，不授整文件写回。正式9路径SOURCE_READY及作者已定位；严Ready挡SuspendCapture只读参考的风险由其owner确认并正修，旧PowerRequest将只作历史，后继借唯一ReadScope事件。共享post-gate修正尚待本侧正式核收；第三Steam内publisher/完整debug-remote/包及实机OPEN。无主线产品写/部署/现场，目标ACTIVE。后继QamFocusReview-20261112/native-lifetime-cohort-closing-r2/INTAKE.json及REPORT.zh-CN.md。


2026-11-13 回补侧已独立核收唯一睡眠层正常Wake后置自动排队：正式commit/共享投影→原Control/PowerReady/enqueue/Admission及真实Registration，9交错22断言、旧commit负例与Recovery13/74分组核；5原Job0、本侧零重跑/编译。当前两个完整函数逐字同正式导入。正常deferredWake门前Unknown已不再是未验证规划；startupUnknown/后到classification及完整runOne/engine仍OPEN，不代签实机。下一步消费公共ReadScope readonly-Suspend后继及借用取消事件，不叠旧私有PowerRequest。证据QamFocusReview-20261112/shared-post-gate-intake-r2/INTAKE.json，整体ACTIVE，无产品/部署/现场。


2026-11-13 启动Ready消费接续已主线精确导入：仅Control.startupPending atomic+原PowerReady无当前deferredWake时给未完成且enabled/当前代次补排原TargetChanged；原runOne判Startup，两路线原Admission/持久账不退款。10新定向+旧callback负例实跑/3Job0，含真实私有队列worker、1000通知合一、Wake优先与不改Wake/Suspend选择；Native/身份/分类仍端口，非完整runOne/engine或实机通过。短Windows源事务两hunk反向与回读，其他owner字节保留，不叠旧PowerRequest。原171ms核收FAIL/费用保留，无旧组重跑、无部署/现场。仅本SOURCE_READY，Readonly-Suspend/后到事实/第三publisher/包与三点仍OPEN。证据QamFocusReview-20261112/startup-ready-consumer-mainline-r2/SOURCE_READY.json及MAINLINE-IMPORT.json，整体ACTIVE。


<!-- NATIVE-READ-OBSERVATION-SUSPEND-20261113 -->
2026-11-13 Native读取的SuspendCapture只读回归已修复并精确导入10个配套路径。原Ready-only工厂会误拒绝睡眠前参考采集；现明确ObserveOriginal/PrepareMutation，默认仍严格Ready。观察绑定原Observer process/account代次、实际Steam/game持有与同一Native reader的generation/通知序号/suspendSerial/phase/flags；诊断publication前进允许，变化或未知粘性撤销。观察借用原mirror取消事件，不生成Ready权限；原Ready三回调、observe正文和GameFactScope保持。日志读取沿原唯一TrackerOwner/ProcessOwner、完整十进制GameID和primary join。

真实producer6/33通过；原runtime失败父中的有效前缀7/65与端口修后单项1/2分epoch，不拼新8项父PASS。旧源SuspendCapture反例与三编译FAIL/准备拒绝/端口FAIL及费用保留。13原Job加2必要兼容编译Job均关前0；默认及旧scope入口同步支持ReadPurpose，最新合流七参数消费者只编译、不重跑业务。原Startup两hunk、双取消事件和配套模板两行均保留，其他字节经逆hunk与短Windows独占CAS核对。

SOURCE_READY只限此修复；最终Steam内publisher撤销委托尚未开始，SDK有界返回/完整debug-remote归还/完整Native包/部署/物理睡眠仍OPEN，整体ACTIVE。证据Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/NativeReadScope-20261113/ReadOnlySuspend-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json及HANDOFF.json。普通导出夹具NOT_RUN，无真实Steam/游戏/设备/睡眠动作。
<!-- /NATIVE-READ-OBSERVATION-SUSPEND-20261113 -->


2026-11-13 公共ReadScope双取消等待配套已主线：5产品文件借用identity/power事件，保留原hint无写票语义；原ownerHeld诊断异常/driver WAIT_FAILED保留修正同cohort导入。9新wait+缺identity反例、完整engine/归档/七参adapter共6Job0，图289/263unique、340/313unique、180/180；原组不重跑。原夹具2行签名+1Frozen include配套，Readonly-Suspend合流完整runOne/route仅额外编译1Job，2旧compileFAIL及UNKNOWN/费用保留。Readonly producer6/33与consumer父FAIL有效7/65+后继1/2分epoch，不拼父PASS。Startup与其他owner字节保留，无旧PowerRequest/新producer/poll/EXE。仅本SOURCE_READY；最终Steam内publisher/全debug-remote/包及实机焦点Running等OPEN。证据QamFocusReview-20261112/public-scope-wait-mainline/SOURCE_READY.json与READONLY-SCOPE-INTAKE.json，整体ACTIVE，无部署/现场。


2026-11-13 第3最终publisher由原NativeReadScope作者已实际开做，本侧独立rights9真实Win32对照通过/2Job0：query+sync source或target复制均error5，DUP权限成功；sync副本不可Set/Reset/继承；关未signal源不撤销副本、先signal后关保signal。原ABI3/callback合同只读定位，不重复写实现；已交Prepare强持有复制一次、末端同时查原YMCC存活和取消、无回调管理/阻塞及原额度不退款等具体边界。无跨权限/跨进程Steam或焦点根因证明。官方两网页超时0B/退出0错误留证、无效引用与已核正文表述撤回，结论仅基于实测/源码。证据QamFocusReview-20261112/final-publisher-revocation-review/INTAKE.json，整体ACTIVE，无产品/部署/现场。


<!-- STEAM-FINAL-PUBLISHER-REVOCATION-20261113 -->
2026-11-13 最终Steam发布撤销已配套精确导入16路径：实际ReadScope作为第8参持到engine，写入前要求原Ready stamp/current及借用事件精确匹配；观察scope不能由JSON升级。ABI4的事件源只从已独立验证的原YMCC进程复制，目标仅持3个本地副本；最后SDK调用前再核原Native电源/账户粘性撤销，原CUser/完整GameID/record/primary/健康pins及已消费不退款保持。源事件不Close/Reset，target retire按shared快照安全归还；dispatcher ABI1保持。实际915968字节DLL与嵌入payload/元数据已配对，完整engine及8参main/runtime分别编译，未完整产品main/link/包。

原最后边界反例按预期失败；新17有效前缀+修后单项1分epoch，非18父PASS。8宿主准入及原13发布回归通过，13复用同一PE无重编。真实惰性提供者正常退出后publisher0/撤销不复活；无发布冷控制全进程句柄69→71单列，实际lease71→71，不称冷环境零遗留。原shadow编译FAIL、局部别名缺项、资源计数父FAIL、选择单目标误用于默认双目标的FAIL及工具声明前置UNKNOWN/准备费用保留。16实际Job关前0，另1未准入UNKNOWN不以空Job替通过。

此前第三端口OPEN记录为历史epoch；仅此撤销链范围SOURCE_READY。实际SDK有界返回、debug/remote完整归还、常驻DLL静默清理、完整配套包与物理睡眠/Running/Continue/手柄/CPU继续OPEN，整体ACTIVE；未部署或操作真实Steam/游戏/设备/睡眠。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/NativeReadScope-20261113/FinalPublisherRevocation-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json及HANDOFF.json。
<!-- /STEAM-FINAL-PUBLISHER-REVOCATION-20261113 -->

<!-- DECKY-INITIAL-SUBSCRIBER-20261113 -->
2026-11-13 Decky首次显示订阅异常隔离已精确写主线：原subscribe先登记再直接首次回调，回调抛错会使调用者拿不到取消句柄；现在首次通知与原publish同样隔离，保留原数据副本、后续通知和取消归属。原稳定性矩阵观察者场景保留原后续异常控制，补普通/connectionOnly首次异常、重连及取消后零回调/零计时器；旧源实际拒绝，候选同一原场景两边界通过。只配对该旧夹具缺失的当前gameSteamContextModel依赖并增加原场景定向选择，未知/空/重复三选择拒绝；5个真实Job均自然退出且关前0，不增canonical ID/minimum。只导1产品/1原夹具，无新查询、轮询、线程、重试、恢复额度或Steam动作。原FAIL和后置pin字典误比FAIL保留，仅复读原件修核收；原完整矩阵/插件构建/部署/实机睡眠及CPU收益继续OPEN，后继进下一自然构建，不追赶旧包。唯一原件沿DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/initial-subscriber/SOURCE_READY.json，整体ACTIVE。
<!-- /DECKY-INITIAL-SUBSCRIBER-20261113 -->


2026-11-13 回补新pair资格消费修正已主线：按原两Admission选择Startup，hint只接Ready，不清账；原Wake消费pair不从普通通知再开Startup，新增查询只读原表。16/73与后继7/1026分epoch及两旧源反例、11Job关前0已核，不拼23整父；2产品/3原fixture精确合流，原Readonly/双event/ABI4保持。第三publisher正式16配套已独立核120引用/DLL915968与embed同字节，仅owner图引用。新Win5 111700包首轮Running真、次轮11:16:02后假；两route均模块校验拒绝且0publisher，次轮公共reconcile又报1043/3939488289合同未知；初期公共Ready准入，末尾约60s后拒绝另由睡眠owner核。缺口归唯一Steam/睡眠层，不新建私有producer；本源码修复不代签现场。原FAIL/UNKNOWN/费用保持，无旧组重跑、整包/部署或实机动作，整体ACTIVE。原件QamFocusReview-20261112/startup-wake-continuation/SOURCE_READY.json及Win5SteamSleepRebind-20261111/Field-20261113-111700/INTAKE.json。


<!-- STEAM-QUERY-ACTOR-CANCELLATION-20261113 -->
2026-11-13 原QueryActor等待的公共撤销漏接已修复。就绪/完成/空闲命令及fire前接同一Native电源和账户事件，caller线程捕获借用句柄及shared ReadScope，持到原join；取消优先，UNKNOWN或缺一拒绝，完成后再核。源句柄不Close/Reset，无新线程/定时器/电源producer；原5000ms、SDK调用/释放及join正文保持，进入SDK内部后不伪称可抢占。

13新真实Win32事件/线程边界、原6完成场景与实际waitReady方法前后对照已核，后者旧满5000ms失败、新按native-cancel拒绝；0ms计时不作零耗时/CPU声明。原6复用同一PE，未知新case0场景拒绝，8实际Job关前0。只1产品CPP+1原配套include精确CAS，原6正文单一lambda/default顺序保持，不扩原默认父或minimum；就绪方法端口不是完整SDK执行。实际SDK有界返回/完整释放、debug-remote/常驻DLL/完整Native包和实机继续OPEN，整体ACTIVE；未部署或真实Steam/游戏/设备/睡眠动作。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/NativeReadScope-20261113/QueryActorCancellation-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json及HANDOFF.json。
<!-- /STEAM-QUERY-ACTOR-CANCELLATION-20261113 -->

<!-- DECKY-TEARDOWN-ISOLATION-20261113 -->
2026-11-13 Decky模块卸载独立清理已精确写主线：旧线性dismount中更新/壁纸等dispose抛错会阻止后续意图、socket和placement退出；现在各原owner分别执行一次，固定stage警告不含原错误或凭据，原receipt归属与幂等保持。原fan observer卸载场景保留显示异常控制，补Loader/pagehide、5处feature/placement异常、TDP待发送及logger异常14组合；旧源实际留下socket及计时器并拒绝，候选同一原case14组合均零socket/计时器/host监听/误发命令，2实际Job自然退出关前0。不增ID/minimum、轮询、线程、查询或恢复预算；仅index.tsx及原slider夹具导入，其他UI/业务断言不改，默认整组/完整插件包与实机OPEN，进下一自然构建。

同日新Win5-111700包已只读核对：今日4次wake到三通道确认603/6220/631/634ms，40次QAM检查ready；11:15:58的失败行是旧peer/demand19被丢弃，当前demand21快照1060ms就绪，不当作新活动断链。341成本样本含339有效间隔，按PID/birth/起止去掉339个alias；343500ms留存窗口内Loader根+自有child加权0.0227438%单逻辑CPU，Steam UI whole-process为2.137918%，不是独占壁纸或新版收益。上述现场不证明本后继已部署，也不归因为本卸载缺口。原失败/费用保留，整体ACTIVE。原件沿recovery-cause-trace-20261113/teardown-isolation/SOURCE_READY.json及field-win5-20261113/REVIEW.json（DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112）。
<!-- /DECKY-TEARDOWN-ISOLATION-20261113 -->


2026-11-13 Win5 111700后继：两轮native路线均在兼容检查前拒绝、0publisher；首轮Running真不能归因回补成功，次轮11:16:02.233起至少54.503s缺失。公共1043已由唯一Steamowner精确导主线JS/H/原fixture，本方独立核完整官方包/ZIP成员/正文、8原VM+旧拒绝+3非法选择、7原Job准入/关前0和87关键引用，不重跑。current公共表含现场pair，实际loaded引擎来源仍OPEN。公共睡眠初期Ready准入正常，60s尾部为Steam消费证明年龄路径而非公共Ready到期，逐guard/loaded限制保持。三点59原completion含首12proven接受，次15拒绝/13type1/2desktop接受，无完整tuple不签焦点因果；QueryActor取消独立raw已纳入，SDK/join仍OPEN。原Startup16/73和仅续7/1026分epoch，不拼父PASS。原FAIL/UNKNOWN及费用保留，本后继无新业务/编译/产品改/部署或真实Steam设备睡眠动作。原件Win5SteamSleepRebind-20261111/QamFocusReview-20261112/win5-public-successors-intake/INTAKE.json，现场说明Field-20261113-111700/PUBLIC-LAYER-SUCCESSOR.zh-CN.md；整体ACTIVE。

<!-- DECKY-OPENING-PROBE-COALESCE-20261113 -->
2026-11-13 Decky同次侧栏打开的冗余读取已精确修主线：原checkConnectionOnOpen先借可见性发普通snapshot，再发带nonce的fresh probe；实际Client+Relay桥接复现2请求/2 read callback。现仅借可见性至发probe的同一同步交接暂缓普通请求，实际1请求/1 read，nonce/服务器fresh/source/身份围栏、超时和恢复额度不变，无新查询/轮询/线程。仅mirrorClient.ts及原client夹具导入。

原restart case候选前段已观察1 read和两visible重连控制，父组随后因旧测试按两个15秒timer的插入顺序选错owner而FAIL；原件/费用保留。夹具现取真实probe/snapshot各自timer，原期限和断言保持；后继仅跑未完成hidden-proof-lifetimes suffix及3非法variant，原超时归还/快照超时250ms原重连/卸载均核，六Job自然关前0，不拼整case/父PASS。两被引用函数切片和产品在分段间逐字相同，旧2 read反例保持。原Win5多次首读discard只为选点，未证明本缺口解释全部现场或新版CPU收益。累计38879/240000ms，原失败不重置；无新ID/minimum、完整插件包/部署/实机验收，后继进下一自然构建，整体ACTIVE。唯一原件沿DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/opening-probe-coalesce/SOURCE_READY.json。
<!-- /DECKY-OPENING-PROBE-COALESCE-20261113 -->

<!-- DECKY-PACKAGE-UPDATE-AUDIT-20261113 -->
2026-11-13 Decky升级维护只读核收：固定原embedded PS真实隔离文件事务，原53项父组受25秒执行额度限止，34项实际通过/19项未执行，TIMEOUT原件与31279ms保持，自有Job自然关前0。后继私有夹具只在原Check入口跳过前34，逐项核余19的名称/顺序/原断言正文；19通过且零已过重跑，原30/25/5秒额度不变，不拼整父PASS。坏回执/重复字段/未来schema/原子写锁解除后恢复等边界已观察；semantic mutation proofs及整发布/安装验收仍NOT_RUN。主线产品/夹具本次零改动。

同账独立官方来源诊断确认最新稳定v3.3.1、commit ffd103b9c1a4ce5bf305e7c0998eda6e05457dad及Windows noconsole资产ID/大小/SHA与当前锁一致，未替换或修改Loader。三次自有Job均自然归零且未强杀，已知执行累计60287/240000ms；准备/分析/记录不伪计为子业务耗时。保留官方整包消费及单YMCC升级路径，不恢复独立Decky升级器或Loader版本倒退否决门。无部署/真实Steam/设备睡眠动作，无实机CPU或恢复频率收益证明，整体ACTIVE。唯一后继Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/package-update-audit/AUDIT-CLOSING.json。
<!-- /DECKY-PACKAGE-UPDATE-AUDIT-20261113 -->


2026-11-13 Steam三点/双路线回补完整Native合流：固定1476源码/原3027工具+OneCore前置pin，原build_native.bat五PE及精确NativeBuildID、实际1470 include/55库已核；公共取消/睡眠、双route、1043JS/H与菜单qualifiedRequest真实入图。原库图漏OneCore的后置FAIL保留，仅原main link/mt/seal续接，原obj不编、其余4产物不变。实际原C2065为共享g_inputCaptureStop先用后定义，仅前移同一atomic声明，主线及固定源其余字节锁内校对保持；准备字段位置/重链selector误判FAIL、原C2065/费用保留。四自有Job关前0，工具收尾与自然退出分列，不重跑旧业务，不拼父PASS。主线只后置观察一次漂移8项，不追赶重编。用户龙信2三点视觉成功保持，未绑定操作号/loaded来源；无Web/受管/包/部署或真实Steam设备睡眠，Running/Continue/手柄/焦点因果及S3/S4未验。唯一后继Win5SteamSleepRebind-20261111/QamFocusReview-20261112/native-integration-20261113/SOURCE_READY.json；整体ACTIVE。

<!-- DECKY-FAN-FINAL-ADMISSION-20261113 -->
2026-11-13 Decky风扇延迟队列最后发送边界已精确修复：原显示notify可同步卸载queue，旧源仍进入send callback一次；现复核disposed/applying及同一当前fan来源，再投递给原共享Client。断线/代次/权限撤回均零send callback，无关revision/game刷新仍仅一次并使用新revision；2秒原延迟保持。此为队列边界证据，不称实际YMCC硬件误写，原共享Client仍有最后保护，不归因为历史睡眠现场。

原unmount case正文及20个其它原case保持，扩展同一原case的5边界；旧源必要反例、候选1原case/5边界与3非法选择拒绝已实跑，5自有Job自然关前0，累计执行44641/180000ms。两次准备FAIL（混合换行锚点、symlink权限）原件和费用保留，后继仅修准备不重跑业务；不新增ID/minimum/轮询/恢复额度。精确1产品+1原夹具写主线，源字节与候选相同，其他字节逆hunk保留。未完整插件配套构建/部署/实机睡眠或CPU收益验证，进下一自然构建，整体ACTIVE。唯一原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/fan-final-admission/SOURCE_READY.json。
<!-- /DECKY-FAN-FINAL-ADMISSION-20261113 -->


<!-- STEAM-LAUNCH-PUBLIC-INVENTORY-R27-20261113 -->
2026-11-13 Steam原生启动两处私有全进程枚举已移除：WindowsPorts.snapshot/absentExcept仅消费原processInventory fresh0；只有Complete能证明不存在，未知/失败/partial拒绝，原ownedChild排除/联动token-user-session-path/持久账/不确定不重放保持。实际12新场景、旧私扫1语义反例、原NoIo68及3非法选择通过；1001跟随缓存零新增查询，不当实机CPU百分比。
定向两图各444出现330unique/10库，原NoIo500出现358unique/7库。13实际准入Job关前0（8自然5核身份编译工具回收）；两编译FAIL与后置统计字段误名、准备锚点/换行误比及原1800秒账超时点保留。只续未执行/复读原raw，已过68/12不重跑；实际原生成器prepare与精确binding六边界已核，1产品+8原夹具配套精确导主线，无新ID/minimum/线程/轮询/恢复额。
Win5 dd5d...导出盘EXE经原d479f2c8包实际zip成员/EXE/BUILD-IDENTITY/固定源码核同，固定2026-11-12 20:38:48.787香港时间，只有Legacy/Public且无新OwnerScoped布局头；因此该盘程序不含后继校准。此关闭远端disk包来源OPEN，不代现场loaded image或全部故障因果，本机c78d包不混用。
整个R1～R6仍ACTIVE：URI/全部typed输出与独立helper、完整SourceLease/默认消费者/实际SDK资源、匹配包和现场CPU/睡眠恢复未齐，不称全图唯一。未完整产品编译/部署或Steam游戏设备睡眠动作。唯一后继 G:/YeManCC-Work/Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SHARED-CONSUMER-COORDINATION-20261113-R27.json；NativeLaunchInventory-20261113/SOURCE_READY.json、REMOTE-PACKAGE-INTAKE.json及REPORT.zh-CN.md。
<!-- /STEAM-LAUNCH-PUBLIC-INVENTORY-R27-20261113 -->


2026-11-13 公共record/UI诊断OwnerScoped错选已修：实际sleep nativeRecordDiagnostic调用；2公共/2原vendor映射及record标签精确导入，未知枚举拒绝、旧分支保持，无私有producer/轮询/额度。原digest adapter定向真实PE副本：旧client/UI两个实际反例，root38/vendor38、3非法选择及13真实Job关前0，三原编译图完整核；两次未resumed来源角色拒绝原件和空Job单列，已过编译不重跑。原DLL915968/21483918...与embed/receipt/sourcepins对应，未加载；3配套生成物和1原fixture同事务导入，原publisher动作/ABI4/取消保持。Win5 dd5d盘EXE已独立核原zip/唯一成员/BuildID/快照，绑定d479旧固定包（2026-11-12T12:38:48.787Z）仅Legacy/Public，盘包来源不再OPEN；仍不代log-time loaded和唯一因果。此前整Native属修复前epoch，不追赶重编，下一自然配套；无部署或真实Steam设备睡眠，Running/Continue/手柄/焦点和S3/S4仍OPEN。唯一后继Win5SteamSleepRebind-20261111/QamFocusReview-20261112/shared-diagnostic-layouts-20261113/SOURCE_READY.json，整体ACTIVE。

<!-- DECKY-UPDATER-SURFACE-CLEANUP-20261113 -->
2026-11-13 Decky更新入口surface清理已精确修复：原module dispose只退DOM，若Steam未同步React卸载，实际两个MutationObserver仍active；先移除组件抛错更会跳过本地清理。现在原owners持完整surface disposer，先归还本地观察/DOM/焦点，再向Steam移除组件；独立释放全部尝试后保留首错误，原顶层有界日志继续接收。初始scan失败自动退本surface，已退surface的迟到callback不再读状态或重建入口。无新增轮询/线程/恢复额度或Loader内核修改。

原single update entry case保留原层级断言并扩5变体：普通module卸载、router错误、effect卸载、首scan失败、observer已断开后的抛错。旧源实际留2观察器，新候选5变体全部0/挂载0/焦点恢复，3非法selector明确拒绝，5自有Job自然关前0，执行29485/240000ms。注入router错误时注册表项仍1、错误保持，不能把本地归还称Steam成功移除。原raw lateCallbackAttempts字段是计划loop上界，旧源在先断言失败后未执行该loop；原raw保留，后继只改为plannedLateCallbackAttempts标签，业务/断言逐字保持且不重跑。精确1产品+1原fixture写主线，匹配插件待下一自然构建，未部署/实际Steam或睡眠操作，未证历史根因或现场CPU收益，整体ACTIVE。原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/updater-surface-cleanup/SOURCE_READY.json。
<!-- /DECKY-UPDATER-SURFACE-CLEANUP-20261113 -->


<!-- STEAM-RECEIVER-LEAST-RIGHTS-20261113 -->
2026-11-13 Steam最终发布器跨进程撤销传输后继已精确合入11配套路径。Host在原durable consume之后、原scope/current及精确目标复核下，把原电源/账户事件各以SYNCHRONIZE复制至原Steam进程；wire2借用接收端本地槽，接收端再持有自己的3份副本，查询YMCC仅需query/wait，不再要求PROCESS_DUP_HANDLE。原Native stamp/Ready/账户/full GameID/primary/healthy及最终SDK前撤销保持。已知Prepare返回后回收两个传输槽；部分失败不退款，CLOSE_SOURCE失败先忘记数值且永不重试，管理结果未知或原清理归属抛错沿原ownerHeld保留至实际目标退出。ABI5配套915968B DLL/嵌入头/源清单与当前引擎对象已核，dispatcher ABI1保持。

真实自有双进程新2撤销+冷控制、旧只读owner拒绝+冷控制、原发布18/494和同PE原13、sender7及原cleanup块4+冷控制均有独立raw；旧cleanup反例成立，原ownerHeld逐字体配真实子进程。27实际准入Job关前0（限定工具收尾与自然退出分列）、14编译图/884唯一引用核收；无新canonical/minimum或产品线程。原编译警告、HANDLE_LIST临时指针准备、旧cold断言、过期预算拒绝、两次合入路径适配失败和后置纠正及所有费用保留；已过业务不为封存重跑，初次主线CAS在写入前拒绝并行payload变化；仅接续原owner正式共享布局两reader，DLL因此补编一次并仅续受影响原18/同PE13和配套engine；sender/双进程/cleanup未重跑，前后来源分开。

未实际测UAC/跨完整性或Steam SDK，不将权限风险认定为历史现场根因；管理状态/清理owner及Native事实为控制端口，不代签完整debugger/remote/DLL退出。SDK内部有界返回/完整资源归还、配套完整Native/包和物理仍OPEN，整体ACTIVE；无部署或真实Steam/游戏/设备/睡眠动作。正式原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/NativeReadScope-20261113/ReceiverLeastRights-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json及HANDOFF.json。
<!-- /STEAM-RECEIVER-LEAST-RIGHTS-20261113 -->

<!-- DECKY-INTEGRATED-PLUGIN-BUILD-20261113 -->
2026-11-13 已将这轮五处就绪产品来源合成真实Decky插件：固定当前518源/678工具，原build-decky-sidebar-plugin.mjs原两段指纹配方实际构建，buildId=38ec47753caea7f16714291b88f6b9a70ab6096c881424678a76035304ed1e1d，总511551B。27个原background-intent场景实跑并逐名/顺序核对，包括关侧栏/卸载后TDP及风扇两秒提交、10类已接受命令的回执、真实来源变化与不确定结果不重放；2自有Job自然关前0，执行21364/240000ms。普通默认整组/完整Native和YMCC包均NOT_RUN。

当前PerformanceControls与原20项focus产物来源相同，原index前像SHA匹配且启动/Content完整前缀逐字不变；仅来源等价引用原20，不重跑或称本轮20通过。一次fresh观察518源drift0后，精确提升五项正式插件资产至主线，实际只写dist/index.js，四个相同元数据文件未改字节；Loader官方3.3.1未改。未追赶并行源码、未写安装目录或启停/操控Steam。只读安装插件仍为ca4e3498...旧来源，不能归功新修；早先一次无返回进程的查询曾被误述为未运行，后续实际观察到既有YMCC进程，该错误判断已前向更正且未产生程序动作。配套整包/真实睡眠与CPU收益继续OPEN，目标ACTIVE。唯一原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/plugin-integrated-build/SOURCE_READY.json。
<!-- /DECKY-INTEGRATED-PLUGIN-BUILD-20261113 -->


2026-11-13 三点/回补源码交点后继：正式driver原installed日志回调处于未继续的debug事件内；现只存原tick，确认detach后且result前发原通知，unknown detach抑制该通知。四诊断hunk逆字节核原动作/query/预算/清理保持，不删实际源捕获和driver共用账户往返。完整实际engine TU object-only/323include及3Job关前0；现有ABI5 DLL435实际输入同字节，host driver不在DLL图，复用2ee58042...并原generator更新verify receipt/sourcepins，不重编或加载DLL。3文件Windows新字节锁内精确主线导入，原SDK条件include/错误封存/混合EOL/旧ABI4复用FAIL保留，来源不重拷。当前漂移0项只观察不追整编。微软debug事件暂停合同支持移除额外sink工作的推论，不签现场延迟或焦点因果；无新producer/logger/轮询/测试父/业务重跑、无部署或真实Steam设备睡眠，整体ACTIVE。唯一后继Win5SteamSleepRebind-20261111/QamFocusReview-20261112/deferred-driver-progress-20261113/SOURCE_READY.json。


2026-11-13 driver进度后置纠正：后置审计纠正：官方文档的两次web调用没有返回可读正文，另一次直接HTTP读取15秒超时。因此此前OFFICIAL-CONTRACT.json/报告中“官方合同已reviewed”的本轮核验表述撤回，来源URL仅是待核参考，不授官方读取PASS。原源码顺序、实际完整host TU编译、3Job0与3文件精确导入仍有原件；暂停时长/焦点影响继续只作未实测推论。原件不覆写。

原600000ms父期最终封存墙钟651245ms，超51245ms，CLOSING内finishedWithinRegisteredCurrentParent已实际为false。该超支及失败抓取未知尾费保留，不追加回填预算，不称预算内整父通过，不为纠正重跑编译/业务。

<!-- DECKY-INTEGRATED-PLUGIN-ROOT-CORRECTION-20261113 -->
2026-11-13 对上段私有源根产物38ec477...的“主线匹配”资格前向撤回：真正原默认--check已实际拒绝dist/index.js，FAIL/exit1/费用保留。518源/工具未漂移时，实际主线源根构建得到不同minified指纹；确切变换原因未证，不用改hash、规范化变量或放宽断言求绿。保留私有产物，仅沿未改的原主线builder把实际主线根产物精确写回正式插件资产，再用原默认--check实际通过。

最终主线buildId=76fe5dc242a108c89c8d56d27466740bf8eec1e789da7df967dc8c0e6efc1ceb，总511566B；仅dist/index.js变更，4元数据相同。原background27为同源实际证据、纠正期间业务重跑0，原focus20仍只引用。5自有Job（含首check失败）自然关前0；原builder共4调用（私有build/失败check/实际主线build/成功check），累计已知执行48524/240000ms，不重置21364/28204/35338。先前SOURCE_READY与CLOSING保持历史，以本SOURCE_READY-r2前向取代其主线产物资格。无产品源码/原builder逻辑改动、无安装目录写入/Steam或睡眠动作，Native/YMCC整包/实机CPU收益继续OPEN，目标ACTIVE。唯一后继Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/plugin-integrated-build/SOURCE_READY-r2.json。
<!-- /DECKY-INTEGRATED-PLUGIN-ROOT-CORRECTION-20261113 -->


2026-11-13 Win5已有130435普通后续包已定向只读核收：sleep/recovery完整保留旧字节前缀；新tail54 route+8睡眠事实，首次采集空格/T时间cutoff遗漏原件保留，同raw归一化补齐。盘EXE仍dd5d旧d479包；13:04目标valid=false/pid0/source none、三次waiting engineDispatched=false，不推断游戏退出或漏识别。新506 reason12/507 reason32的SleepEntered=false、SleepDuration0、generation9065，不是已证第三次S4恢复失败。native-lifecycle轮转只12:15后，旧三点59条仍在111700原件，本包0completion不授菜单成功/无点击。无新修复实机验收或焦点因果，无产品/编译/部署/Steam设备睡眠动作。唯一后继Win5SteamSleepRebind-20261111/Field-20261113-130435/ANALYSIS.json及REPORT.zh-CN.md；整体ACTIVE，SDK cleanup私有候选仍待原owner正式资格。

<!-- DECKY-WIN5-130435-REVIEW-20261113 -->
2026-11-13 Win5新130435“风扇租约”日志已作Decky只读增量核对：今天留存594→837，原594条全部仍在，新243条覆盖设备时间11:17:03.547～13:04:26.964；224次mode报告六个known/connected事实全真、2次QAM检查ready，新增wake/recovery均0，不是新版睡眠自愈通过。8次壁纸inventory成功集中11:17:07～20秒，其后留存没有追加inventory，不作为实际视频首帧或解码成本证明。一次旧snapshot成功后source-changed弃用，随后当前读30ms成功，未作持续掉线归因。

现场报告插件仍646399...，与主线已校验76fe5d...不同，不能把稳定段归功新修。限定4个原生log成员未匹配Decky/cost|cpu|resource交集，不外推整包无资源证据、未计算CPU比例或收益。本段不改产品、不新增夹具/构建/部署或Steam/设备动作；原raw新增行、行号图、两zip/member SHA均固定。整体ACTIVE，下一步只对独立可复现缺口续作。原件Testing/YMCC/Evidence/DeckyRetainedBackendRecovery-20261111/verified-health-wake-20261112/shared-admission-review-20261112/recovery-cause-trace-20261113/field-win5-130435-20261113/REVIEW.json。
<!-- /DECKY-WIN5-130435-REVIEW-20261113 -->


<!-- STEAM-FIXED-CLIENT-COMMAND-R28-20261113 -->
2026-11-13 公共固定Steam命令出口已精确写2产品+5原夹具配套及原identity-only索引4条。manual set/show/stop与已获准恢复共用typed BigPicture/Desktop/Stop、固定参数和持PID/birth/root句柄；手动捕获原共享电源，先完成会话锁读取再核持进程及exit/mock/power，通知/serial/代次/未知拒绝、纯publication推进保持。恢复admit最终仅一次，空admit拒绝，异常/授权期间撤销归还句柄，无重放或新恢复额度；原显示flag/radial与set后CSS/Decky授权保持。
最终36边界、原display30/stop36、3非法selector及必要旧源反例已核；12实际图/50准入Job关前0（40自然10核身份工具收尾），1未准入UNKNOWN另列。旧32与中间5/最终36分epoch，最后共同函数变化才重验受影响组；原静态诊断命名FAIL修后仅续，3前缀+后缀不拼整父。准备模板/复制/重复pin/路径比较FAIL及全部费用前向保留；原1800秒+有据追加900秒，未重置旧账。
UI固定命令只关闭本范围出口，独立直读/其他typed/helper、完整SourceLease/SDK资源/全消费者、完整Native配套包与实机仍OPEN。Shell提交不算PID原子动作、模式/退出完成或历史现场根因。未部署/操作Steam、游戏、设备或真实睡眠，无CPU百分比/恢复降频证明，整体ACTIVE。唯一后继 G:/YeManCC-Work/Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SHARED-CONSUMER-COORDINATION-20261113-R28.json；ClientCommandOutlet-20261113/SOURCE_READY.json、QUALIFICATION.json及REPORT.zh-CN.md。
<!-- /STEAM-FIXED-CLIENT-COMMAND-R28-20261113 -->


<!-- STEAM-QUERY-ACTOR-RETURN-20261113 -->
2026-11-13 完整QueryActor资源后继已精确合入1产品+5原配套/夹具路径。正常结束按原线程归还user/pipe，并要求本YMCC进程local SDK BShutdownIfAllPipesClosed成功后只归还本次LoadLibrary引用；加载后factory缺失/null也归还。SDK抛错、释放false/抛错或loader归还未知均不跨线程逃逸，单一process-local debt保留原module/client/pipe/user和shared ReadScope，拒绝后续actor加载/调用，不重试/卸载/借Ready复活。它不是Native电源或Steam账户authority。原host在发布结果及最终读回之间显式finish/join并确认cleanup，summary独立记录真实归还。SDK内部仍原join等待，不detach/TerminateThread。

完整原类Current14、真实自有DLL生命周期12/相关原断言、两原module泄漏反例、实际success-tail3/缺finish原反例及3非法选择已核。6个未知场景在actor结束时有意保留SDK/module/scope，直到夹具进程退出，不拿Job0代作提前归还；两真实阻塞SDK叶保scope/module到放行join。28实际准入Job关前0、6编译图已核，限定工具收尾分列。原R1重复pin在业务准入前UNKNOWN/空Job另列，旧失败/费用不变。独立审阅修fixture截止异常跳join：仅clock叶改为零剩余并保留join，F/P2分别补验，原14/12按P epoch保持未重跑。原prepare-only入口补H/等待/json及三完整actor闭包，默认14保留，定向每case独立进程，SDK债无reset，无新ID/minimum/产品线程或timer。

真实SDK API/quiescence/内部有界返回、任意SEH、完整debugger/remote/常驻DLL和全消费者、配套完整Native/包与物理仍OPEN，整体ACTIVE；无部署或实际Steam/游戏/设备/睡眠。此轮不重跑既过publisher/旧wait组。正式原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/NativeReadScope-20261113/QueryActorCompletion-20261113/SOURCE_READY.json、MAINLINE-IMPORT.json及HANDOFF.json。
<!-- /STEAM-QUERY-ACTOR-RETURN-20261113 -->


<!-- DECKY-WALLPAPER-LOCAL-TEARDOWN-20261113 -->
2026-11-13 Decky壁纸模块卸载异常已按原late-home-no-route-remount消费者复现并精确修复。原Steam路由移除抛错打断后续清理，真实React/Edge留下10监听/2 MutationObserver/1 IntersectionObserver、2patch、1表面及壁纸订阅；候选逐项尝试全部本地释放后再抛首错误，故障与正常两变体各31原断言通过，迟到事件不复活，共用HUD连接保持。故障Steam注册项仍1且原错保留，不冒称外部移除成功。3非法selector在编译/浏览器前拒绝，原复现及候选共6自有Job自然关前0，旧负例不重跑。

仅1产品+1原浏览器夹具精确导入，显式teardown变体补模拟native foreground事实，默认组/产品准入不改；无新timer/线程/恢复额度/ID/minimum。准备阶段一次多余转义修正断言在写前拒绝，未改来源且原FAIL/费用保留。执行账72617/300000ms，图片场景不是视频解码或实机CPU收益证明。旧76fe5d...插件构建不含此后继，交下一自然配套构建，不追赶重编；未部署或操作Steam/游戏/设备/睡眠，整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\wallpaper-teardown\SOURCE_READY.json。
<!-- /DECKY-WALLPAPER-LOCAL-TEARDOWN-20261113 -->


<!-- DECKY-HUD-MODULE-TEARDOWN-20261113 -->
2026-11-13 Decky监控HUD卸载异常已用原cold-connected-later/already-defined HUD-only入口、真实React/Edge和MirrorClient复现。原router移除报错后仍留hud passive demand/本地占用标记/1根；候选逐项释放全部本地归属再抛首错误，并阻止旧React根与迟到bootstrap重领。故障/正常两变体各13原扩展检查（HUD内10）通过，32迟到事件与残留根强制重挂后仍0旧归属；下一模块取得新标记并正常退出，独立共用连接保持。故障外部Steam注册项仍1及原错保留，不冒称移除成功。

原首次编译因新SVG图标未在旧fixture快照filter登记而拒绝，未到浏览器，FAIL/原stderr及费用保留；只补两处SVG登记/原asset loader转交后续同用例。原复现只跑一次，候选5Job加原复现及准备失败共7自有Job自然关前0，3非法selector在输出/编译/浏览器前拒绝；累计106888/420000ms。仅1产品+1原浏览器夹具精确导入，默认组/Loader/业务准入/恢复次数不变，无新timer/线程/ID/minimum。此处为惰性Full-mode能力导出、无native popup/实际Steam/实机睡眠或CPU收益验收；不认定历史掌机根因。旧76fe5d...插件未含此后继，交下一自然配套构建，不追赶已完成包，未部署；整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\hud-module-teardown\SOURCE_READY.json。
<!-- /DECKY-HUD-MODULE-TEARDOWN-20261113 -->


<!-- STEAM-LIVE-NATIVE-OWNER-R29-20261113 -->
2026-11-13 公共Live原steamLiveRequest已消费唯一Observer，本后继补exit/mock纯撤销，原快照/队列/发现/最终admit及迟到回包保持该请求归属；不加全局Ready，不阻断原SuspendCapture只读，固定sleep witness/foreign-registry语义及admit最终一次保持。公共run仅sleep.input-resume迟到拒绝时保留原bounded nativeReturn/v1，diagnostic-only不授成功/输入/Continue或重放；原false的steamFailure规范化保留，无新logger/查询/timer/线程/恢复额。
实际native-owner前2+后14/44分epoch不拼16整父，当前nativeReturn13/50、旧体反例、当前原main30、3native/3PS非法选择、原PS实际prepare与编译片段逐字核同；5图/20实际Job自然关前0。旧可选回执/路径拼名/片段核收FAIL和原1800整体超时/2700收尾费用保留，当前只读发布另账不倒填旧PASS。2产品+7夹具依赖/原索引精确写回，parallel listeners/main保持。
旧boot automatic完整extractor之前已缺held-process/分类接线，本轮仅配新依赖，其完整64项仍OPEN；全部typed/helper/SourceLease/SDK资源、完整Native配套包与物理CPU/睡眠/降频未齐。无真实Steam/设备/睡眠动作或部署，整体ACTIVE。唯一后继 G:/YeManCC-Work/Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SHARED-CONSUMER-COORDINATION-20261113-R29.json；LiveNativeOwner-20261113/SOURCE_READY.json、QUALIFICATION.json及REPORT.zh-CN.md。
<!-- /STEAM-LIVE-NATIVE-OWNER-R29-20261113 -->


<!-- DECKY-HUD-ENTRY-STARTUP-ROLLBACK-20261113 -->
2026-11-13 Decky HUD部分初始化及入口回滚已实证修复。原入口先启connection demand，HUD注册后抛错使module拒绝且没有onDismount，实际MirrorClient/React/Edge留下在线socket、2订阅、2timer和1注册组件；原cold-connected-later/already-defined消费者捕获。候选HUD以同一disposer回滚已尝试的注册/事件/订阅/自己取得的需求；入口按逆序归还已成功兄弟并dispose原client，保留首初始化错误。注册前/后/diagnostic发布/壁纸factory/更新factory五故障各10检查通过，正常启动5检查通过；32迟到事件不复活，新module仅1socket且无等待timer。兄弟工厂为惰性协作者，不授其内部部分初始化通过。

首次候选因fixture遗漏原隐藏连接probe回复，恢复后的2timer尚在而失败；补显式startup-only correlated native ready:false回复后只续该必要集合，产品及零timer断言保持，原FAIL/raw/费用保留。原复现一次、首候选一次、新候选9Job共11自然关前0，3非法selector在输出/编译/浏览器前拒绝，累计136557/600000ms。精确2产品+原fixture导入，无新timer/线程/ID/minimum/Loader/恢复次数改动；默认组不重跑，不认历史现场根因、实际Steam/popup/睡眠/CPU收益。旧76fe5d插件未含后继，交下一自然配套构建，不追赶已完成包；未部署，整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\hud-startup-rollback\SOURCE_READY.json。
<!-- /DECKY-HUD-ENTRY-STARTUP-ROLLBACK-20261113 -->


<!-- STEAM-HOME-FINAL-ADMISSION-20261113 -->
2026-11-13 GamingHome独立入口后继已精确合入2产品+1原夹具。固定task/BAT/Steam主页URI/focus共用typed同步出口，日志与COM准备后、每次真实Run/Shell/ShowWindow/SetForegroundWindow前复核原FSE/主页选择/取消/首次持有YMCC进程及原15/60秒。首次身份退出或未知后拒绝替换与复活，原task描述/唯一action/固定路径与参数/当前SID/权限/交互token/零trigger和窗口约束保持。它不是Steam账户/Valve模式/SourceLeaseV2权威。

原67断言、24定向/88断言、4旧源语义反例及3非法选择已核；独立审查发现首次候选snapshot会换持有进程，修后直接消费精确产品snapshot/current并核原候选反例，原24不重跑。各实际Job准入且dispose前0，完整组件私有编译链接及实际依赖图已核；原SDK头漏声明FAIL保留，只读补既有pin后核同raw，真正产品身份变更分别编r2/r3。无新ID/minimum/线程/timer/EXE，无真实任务/Steam/设备/睡眠/部署；全部消费者/SDK有界返回/匹配完整包/物理继续OPEN，整体ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/IndependentHomeAdmission-20261113/SOURCE_READY.json及HANDOFF.json。
<!-- /STEAM-HOME-FINAL-ADMISSION-20261113 -->


<!-- STEAM-SHARED-ENDPOINT-DISCOVERY-R30-20261113 -->
2026-11-13 公共请求局部端点选择已精确导入4产品+8配套/原索引。Live、Documents、Bootstrap共用steamdiscovery::select；同PID/port成功后IPv4/IPv6别名只读一次targets，首失败/空结果仍可试另一别名，不同端点第二成功仍歧义拒绝。原各域fatal词汇、sync/async、4/7/5秒界及账户/持进程/最终鲜读归属保持，取消不被忽略错误吞掉；无跨请求cache/producer/线程/timer或新权限。
实际20定向和真实旧bootstrap反例、原main30/expected身份/doc34/bootstrap118/worker13分列；3native/1PS非法选择及原PS三准备路径核同。4图/26实际准入Job自然关前0，单个双地址成功bootstrap的targets GET 2→1，不作整机CPU或现场修复证明。编译flags/advapi32、anchor/EOL和首导入CAS失败保留；binding只重链，PS精确rebase保留CPU1并仅续生成准备，无已过业务重跑。
原1800秒账不改，本次发布另列；新helper纳入live-transport索引。全部SourceLease/typed/helper、完整Native配套包与物理CPU/睡眠/自愈降频仍OPEN，111700两route未进driver不授恢复成功，guard/proof/终态诊断由原恢复owner续。无部署/真实Steam设备睡眠动作，整体ACTIVE。唯一后继 G:/YeManCC-Work/Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SHARED-CONSUMER-COORDINATION-20261113-R30.json；EndpointDiscovery-20261113/SOURCE_READY.json、QUALIFICATION-r2.json及REPORT.zh-CN.md。
<!-- /STEAM-SHARED-ENDPOINT-DISCOVERY-R30-20261113 -->


<!-- DECKY-REAL-SIBLING-STARTUP-ROLLBACK-20261113 -->
2026-11-13 Decky真实壁纸/保底/更新入口部分初始化回滚已精确修复。上一入口父回滚仅有惰性工厂，本后继以实际三consumer与原MirrorClient/React/Edge复现第二wallpaper patch抛错：socket虽已归还，仍留1 patch+7 foreground监听。现wallpaper同一disposer包住注册、预记自己callback并在正常返回用实际handle替换；fallback和updater各自处理addGlobal部分提交后抛错，晚旧root不重新订阅。原cold-connected-later/already-defined下六故障各12检查及正常6检查通过，回滚后新完整模块2patch/3global/1图片可见；当前新epoch另续原late-home卸载故障/正常各31检查，旧原复现不重跑。

首次实接updater因SP_REACT别名缺失在consumer前失败，补明确real-sibling模式的原React别名后继续，FAIL/raw/费用保留。含旧失败、复现、10新启动/selector及2卸载回归共14Job自然关前0，3非法selector编译前拒绝；累计281627/600000ms。精确3产品+原fixture写回，entry/HUD不改，无新timer/线程/ID/minimum/恢复额度；原默认整组不跑。commit后throw控制仅证明本地原callback身份，不授任意外部不透明handle可回收；实际Steam/popup/视频解码/睡眠/CPU收益与历史掌机根因均未证。旧76fe5d插件不含后继，交下一自然配套构建，不追赶旧包，未部署，整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\sibling-startup-rollback\SOURCE_READY.json。
<!-- /DECKY-REAL-SIBLING-STARTUP-ROLLBACK-20261113 -->


<!-- DECKY-STARTUP-RECOVERY-PLUGIN-BUILD-20261113 -->
2026-11-13 Decky最近壁纸/HUD清理、HUD与三兄弟初始化回滚已进入一次自然主线插件构建。固定当前518源码/678工具，实际mainline根的未改原builder构建至隔离输出，随后5项资产锁内精确提升（只dist/index.js变更），原默认--check实际通过。buildId=e9296f3d904437a8a8825a80125f886f54ea1df136aeb607b02231c176dfbdc0，总512649B，官方v3.3.1 Loader未改；两自有Job自然关前0，已知构建/校验执行21715/240000ms。未沿私有源根构建、不复用旧38ec失败资格、不改指纹或放宽原check。

业务证据仅引用原hud-startup与real-sibling当前源码消费者，构建期间业务重跑0；index原手势/意图/Content/卸载后缀逐字同旧已验证来源，仅源码等价，不称background27/focus20本轮通过。未完整Native/YMCC包/安装或真实Steam、睡眠与CPU收益验收，未追赶后继并行源，整体ACTIVE。当前插件资产以本新build取代76fe5d旧快照；旧FAIL/原件保持。唯一原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\plugin-startup-integrated-build\SOURCE_READY.json。
<!-- /DECKY-STARTUP-RECOVERY-PLUGIN-BUILD-20261113 -->


<!-- DECKY-WIN5-143143-COST-REVIEW-20261113 -->
2026-11-13 Win5 143143包已只读对比130435：今天Decky837→1015，旧全保留，新178仅mode六项全健康，13:04:49.342～14:31:35.043；无新wake/recovery，不签新修复/视觉/睡眠通过。现场插件仍646399，与当前主线e9296f及本机ca4e来源分开。

本次在virtual-gamepad.log/.1找到357个input-idle-window的真实整进程成本，14:25:47.146～14:31:44.034、区间合357891ms、16LP。GetProcessTimes delta/rate与逐PID/birth连续total核同；10个identity-unavailable候选排除。整机口径加权：Steam共用context0.0332896%、候选UI0.0030878%、官方Loader父0.00109146%/子观测delta全0、Native0.5007089%；Nativep95 0.78125%、最大3.90625%/最后625msCPU点完整保留。均为整OS进程、不授DeckyJS独占归因；357份壁纸visible/queued=false、kindunknown，不签视频。

同期同Native父PID/birth有1430个不同HidHide CLI settled、最高5/s，已交CPU任务作为线索，命令/子CPU/因果未证。峰值邻近导出文件名但export事件匹配0，不归因导出；首次JSON默认cp936读坏路径在scan前失败，改显式UTF8后只续只读、原失败和原脚本保持。未产品/fixture改动、未新测量进程/构建/部署/Steam设备动作，主线健康日志去重已存在不重复修。整体ACTIVE；原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\field-win5-143143-20261113\REVIEW.json、PROCESS-COST-REVIEW.json和REPORT.zh-CN.md。
<!-- /DECKY-WIN5-143143-COST-REVIEW-20261113 -->


<!-- STEAM-BOOTSTRAP-DISCOVERY-FAILURE-R31-20261113 -->
2026-11-13 bootstrap发现原AsyncFailure被忽略而只剩context-unavailable的缺口已作有界补强。当前请求最多保留一份已有transport事实，仅最终context-unavailable时投影原steamFailure；成功别名即清空，cancel/deadline/其它后期错误不串旧码，当前transport/CDP优先。原status()已读非200/非101带入Http及discovery/upgrade，reason/循环/期限保持，无额外查询、logger、线程/timer/恢复额度或新权限。
实际12定向/189检查、旧原发现块反例、原endpoint20/bootstrap118分列；3native/3PS非法选择及PS三条prepare逐字核同。首prepare缺私有main的FAIL/原空目录保留，精确补原输入后只续准备，业务重跑0。2实际图/17准入Job含准备失败关前0，结束种类依原件；1产品+3既有配套及原identity-only索引1行精确导入，保留并行Observer/listeners/消费端，不为封存重跑业务或借新来源签旧包。
这项用于下一配套版本追查，不认定111700第二次恢复已修复；guard/proof/路线终态由恢复owner续，全SourceLease/SDK/helper/消费者、完整Native/YMCC包与实机CPU/睡眠/降频仍OPEN。无部署或实际Steam设备睡眠动作，整体ACTIVE。唯一后继 G:/YeManCC-Work/Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/SHARED-CONSUMER-COORDINATION-20261113-R31.json；FailureDiagnostics/DiscoveryRetention-20261113/SOURCE_READY.json、QUALIFICATION.json及REPORT.zh-CN.md。
<!-- /STEAM-BOOTSTRAP-DISCOVERY-FAILURE-R31-20261113 -->


<!-- DECKY-EXISTING-PAIRED-PACKAGE-INTAKE-20261113 -->
2026-11-13 Decky交付链只读核收找到现成完整包，不重编追赶：独立065658/vc111700导出build0902eaf...，ZIP709a8baf...，Native7a635c...，包含完全相同e9296f...插件。518固定插件来源及5载荷逐字同主线；原Assert-ExportBuildIdentity/ArchiveIdentity/PublishedTree/DeckyExportPayload含managed manifest实际通过，3038源/678工具、1Job自然关前0，执行24453/240000ms。原完整包不是业务Full/实机验收。

本机仍9db170.../ca4e插件，其精确前包已找到055102/GuardReady、ZIP213e5b...，供原设置保留/回滚事务核对。根Mainline/Release仍Nov9旧包，不借它覆盖本机；独立已完成包与主线源码/本机三个身份分开。CPU任务同时核现成包是否包含其最新叶，由既有配套部署归属衔接；本轮不建包/不编译、不启停YMCC、不替换安装、无Steam/设备/睡眠动作，原部署计划和读回门未执行/未豁免。目标ACTIVE；原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\paired-package-intake\QUALIFICATION.json与DEPLOYMENT-INPUTS.json。
<!-- /DECKY-EXISTING-PAIRED-PACKAGE-INTAKE-20261113 -->


<!-- STEAM-LIBRARY-SHUTDOWN-ADMISSION-20261113 -->
2026-11-13 自定义游戏库关闭Steam已接原Native typed固定出口。UI确认的PID/birth及Native代次/通知序号/挂起serial按十进制字符串保留，原FIFO内部5秒deadline不退款；纯publication不撤销。Native实际capture并最终复核原power/退出/隔离/实例与单次claim。新增动作通道与只读metadata分离；nonce本地管道的实际server PID由内核核验，引用Host HWND本身不算请求者证明。Host不再自行CreateProcess -shutdown，无硬杀/重启；Unknown不重发，提交不是关闭成功，仍只凭新鲜完整缺席交原离线写者。内部截止字段从原commit合并键剔除，保留首请求截止。

原Host22/Native23/UI17及前候选channel28按各自epoch保持，内核证明后只续受影响channel与合并键控制；完整Host实际TU及真实producer头编译核收，所有实际准入Job关前0。原Native夹具缺Command编译FAIL、准备分隔符拒绝、审阅发现HWND伪造及去重缺口均保留，未把旧28冒称最终认证整父。新正式prepare-only配套生成体逐字等于实跑，不新增canonical/minimum/默认Full。完整installed IPC/跨完整性/祖先ACL、配套全Native/link/包及实机仍OPEN，整体ACTIVE；无真实Steam/设备/睡眠/部署。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/SOURCE_READY.json与HANDOFF.json。
<!-- /STEAM-LIBRARY-SHUTDOWN-ADMISSION-20261113 -->


<!-- DECKY-INSTALLED-AYA-REVIEW-20261113 -->
2026-11-13 本机和AYA新归档只读补核：本机1216模式观察须分六项真/明确不可用/未知，14:32:58～14:34:00四次live-ui-read-timeout后同writer/Steam/power恢复六项真，邻接窗口无UI/BPM高阶开始；缺字段不当断连，间隔不当断线时长。AYA332mode健康字段缺失且末尾desktop，580runtime主要等待，Loader PID均0，不归为Decky崩溃或Steam重启。当前main原准入已在账户解析前不可重试停desktop，仅读源、不代旧包或实机资格。四次超时原位置交公共Steamowner沿现有有界trace追因；无产品/fixture/CPU采样/构建/部署/Steam动作。CPUowner继续配套部署，本机旧9db170/ca4e与新e9296f分列，S0/S3/视频/CPU收益仍OPEN，整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\installed-and-aya-20261113\FACT-CLASSIFICATION.json与REPORT.zh-CN.md。
<!-- /DECKY-INSTALLED-AYA-REVIEW-20261113 -->


<!-- DECKY-DESKTOP-RENEWAL-20261113 -->
2026-11-13 Decky慢速准入清空已确认desktop的漏口已由原canonical offline消费者+真实共享ModeObserver复现：旧retry1/账户未确认/续读1，候选retry0/desktop停留/续读0。只改Decky lambda续读前的桌面保留，不改未知/大屏刷新、原额度或权限；原case其他桌面零截止/事件16ms接续/账户节流/OFF与153→153全部资源归还通过，4Job自然关前0。封存器误期待ui-pending的FAIL保留，同raw按实际account原因前向核收，不重跑旧源。仅1产品+1原fixture及3既有声明精确写回，无公共Steam产品/新ID/minimum/线程/timer/EXE；下一自然配套构建消费，不追改已完成包，无整main/新包/部署/实机睡眠或CPU收益验收，不归因AYA全部现场，整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\desktop-renewal\SOURCE_READY.json与REPORT.zh-CN.md。
<!-- /DECKY-DESKTOP-RENEWAL-20261113 -->


<!-- STEAM-ACTION-REAL-IPC-20261113 -->
2026-11-13 自定义游戏库固定退出通道已补真实本地跨进程：完整原client/serve/Peer/callerOwned/processInventory执行11场景，真实WM_COPYDATA/映射/事件/管道PID，无惯性identity返回。三进程借用真实Host HWND时，内核server PID是实际第三方，零入队/零效果；前候选移除管道核对的原完整头对照实际接纳并触发1个惯性动作。产品头未改，计数动作与NativeCurrent仍端口，真实48字节电源/typed出口不冒称组合。

重复20真实请求各1 claim/效果/内核核验，预热后owner句柄109/109稳定；首异常冷基线83→85原FAIL保留，1次明确自有warm child后异常展开实际自然归还、85/85严格相等。10实际准入Job均自然关前0、5编译图核收，原11/20未重跑、不拼最后源码13整父PASS。只精确接原prepare入口的显式cross-process模式及1模板，默认原32保持，无新ID/minimum/默认Full。独立foreign primitive只准备未重复执行。

所有组件名均为证据目录的惯性fixture PE，私有桌面隐藏窗口；源码无SwitchDesktop/抢前台，未独立采样实际input desktop。same-user/session/integrity范围外的installed ACL/UIPI/UAC、完整Native/link/匹配包和物理仍OPEN，整体ACTIVE。无真实YMCC/Steam/游戏/设备/睡眠/部署。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/CrossProcess-20261113/SOURCE_READY.json、QUALIFICATION.json及HANDOFF.json。
<!-- /STEAM-ACTION-REAL-IPC-20261113 -->


<!-- DECKY-WIN5-153718-FIELD-20261113 -->
2026-11-13 Win5新153718包仅Decky只读核：新增550，15:17起已有e9296f插件自报；4个connected终态含14.625s恢复/4.750s独立启动/5.609s唤醒重绑/19.968s前端client掉线后的Loader回收及页面重建。同最后段Steam12540/birth保持，末尾六健康与快照43ms ready真；不把页面重建当整个Steam重启或完整视觉睡眠通过。原始steamFailure unknown/codeKnownfalse仍缺断连根因；本份无产品改，先好后坏待细分client有限状态。266成本窗/14不可用样本排除、Native265样本中128.047s缺口保留，整机0.8615%/最高7.03125%是整Native非Decky独占；无CPU收益/视频GPU验收。初分析漏独立启动终态的FAIL与原脚本保持，同raw复读4终态，无producer重跑。游戏/手柄/其它任务不改，无部署/Steam动作，整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\field-win5-153718-20261113\QUALIFICATION.json与REPORT.zh-CN.md。
<!-- /DECKY-WIN5-153718-FIELD-20261113 -->


<!-- STEAM-ACTION-EXCEPTION-RECEIPT-20261113 -->
2026-11-13 真实本地IPC夹具后继只补同一异常边界的回执守卫：GetProcessHandleCount均核实际返回，析构实际GetExitCodeProcess为0再通过。原自有warm child后85/85严格相等保持；只续原异常case一次，两实际Job自然关前0/1图，当前phase合计12 Job/6图，原真实11、重复20、前候选真实伪造反例不重跑。只写1夹具、精确hunk保留并行字节，无产品变化/新ID/minimum或默认13整父PASS。原冷FAIL及此前scope原件不改。正式后继同CrossProcess-20261113/EXCEPTION-RECEIPT-SOURCE_READY.json及EXCEPTION-RECEIPT-QUALIFICATION.json；全部Steam目标、installed/跨完整性/真实Native及配套包/实机仍OPEN。
<!-- /STEAM-ACTION-EXCEPTION-RECEIPT-20261113 -->


<!-- STEAM-ACTION-ACK-TIMEOUT-20261113 -->
2026-11-13 Steam关闭请求回执超时夹具后继已精确导入两个原夹具文件：P1两项真实WM_COPYDATA超时、P2仅异常零ACK一项，两个私有语义反例和两个无效选择器均已核。12实际准入Job自然关前0、4实际编译图；原13不重跑，当前默认16未跑，不拼整父。Native Current/最终effect仍为惯性端口；冷句柄+2原因未证，不能代签全资源归还。原准备依赖/编码FAIL与成本保留，无新ID/minimum、产品变更或部署。BAT2仅核随包/独立工具及手动用途，当前BAT1无调用边，现场外部调用未验。完整Native/Host配套组合、包和实机继续OPEN，目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/AckTimeout-20261113/SOURCE_READY.json、REPORT.zh-CN.md。
<!-- /STEAM-ACTION-ACK-TIMEOUT-20261113 -->


<!-- DECKY-FINITE-CLIENT-STATE-20261113 -->
2026-11-13 Win5归档确认backend/hooks正常而client断开后约19.968s恢复，Steam身份不变。已补原观测链的有限deckyClientState（16标签），原布尔连接证明/身份/恢复规则保持；旧前端无字段兼容，坏值不入日志。原client/mode各1定向、readiness42及实际共享UI read/main mode callback60断言通过，2000getter读零clone/send/timer，9成功producer实际Job关前0；私有输入pre-case FAIL及工具声明UNKNOWN保留，无已过业务重跑。只Decky字段+原fixture/声明精确导入，无新轮询/thread/EXE或其他任务修复。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\client-state-diagnostic\SOURCE_READY.json；等待下一自然插件/Native配套包，未部署/实机睡眠/视频/CPU收益或历史根因证明，整体ACTIVE。
<!-- /DECKY-FINITE-CLIENT-STATE-20261113 -->


<!-- DECKY-FINITE-CLIENT-PLUGIN-BUILD-20261113 -->
2026-11-13 Decky有限client-state诊断已进入主线插件载荷，518固定源/678工具，原builder及默认--check实跑通过，两Job自然关前0；锁内只dist/index.js变更，buildId=cabc97f10a4e5563fc4da995954de09489977747efed440aec7426fa6781d803，513240B（增加591B），官方v3.3.1 Loader不改。原业务/Native消费者仅引用前epoch，无已过组重跑。源码与5产物重新逐字核对，整体已知执行156366/600000ms；下一自然Native/完整YMCC配套包消费，不追赶已完成CPU包，本次无部署/Steam操作或实机睡眠、视频、CPU收益证明。原FAIL/UNKNOWN保持，目标ACTIVE，唯一后继G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\client-state-diagnostic\plugin-build\SOURCE_READY.json。
<!-- /DECKY-FINITE-CLIENT-PLUGIN-BUILD-20261113 -->


<!-- DECKY-OPENING-DEADLINE-CALLBACK-20261113 -->
2026-11-13 原Decky客户端已复现timer回调未运行时，原15秒期限到仍checking/ready均true；Native原规则会暂停。现只读getter按原probe实际开始+同15000ms判断，不增timer/轮询/线程/写命令/恢复权限，14999仍等、15000变false；30分钟模拟迟到保持有界，当前相关只读回执仍恢复连接，10秒visibility准备不缩短15秒。原restart canonical1定向通过，旧源实拒绝、三个Job自然0，首候选原件按自身epoch保留，无已过Native重跑。1产品TS+1原fixture精确写回，下一自然配套构建消费；cabc97已完成插件为本叶之前快照，不追编。本次无部署/实机CPU睡眠或历史根因证明，目标ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\opening-deadline-observation\SOURCE_READY.json。
<!-- /DECKY-OPENING-DEADLINE-CALLBACK-20261113 -->


<!-- DECKY-HISTORY-R33-REVIEW-20261113 -->
2026-11-13 28包/36日志只读复核，同raw全局去重13079行，36469唯一；mode 2517明确三true/20至少一false/18932未知，不当事故或时长/CPU。旧资源deadline与cleanup-incomplete各2行属同时间相邻不同事件，未放宽归还门；CSS guard的3条crash-unverified独立，所选无wait-failed，风险只观察不改。唯一checking真不能证明现场timer卡；实际超时回调未执行漏口另原消费者已复现并修，等待配套构建。公共R33协调2060B/SHA及三原件已读核，仅历史完整GameID观察/拒绝诊断，不提供双PID动作授权，不改公共任务。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\historical-runtime-gaps-20261113\QUALIFICATION.json及PUBLIC-R33-INTAKE.json；无新业务测试/部署/Steam操作，目标ACTIVE。
<!-- /DECKY-HISTORY-R33-REVIEW-20261113 -->


<!-- DECKY-PENDING-HEALTH-CADENCE-20261113 -->
2026-11-13 实际当前main健康消费者复现checking=true/cached client=true仍走idle30s漏口；现已确认大屏检查期复用原5s evidence请求，正常健康/未知30s和desktop0保持，无新timer/thread/轮询器/恢复额度。原canonical1旧源实拒绝、候选整case通过，含1000重复0、未知25s1、过期原recycle/迟到current回复结束、原20s慢速/真实桌面Observer及153→153全部归还；4Job自然关前0。模拟clock/read端口不是实机恢复或CPU收益。仅Decky lambda+原fixture精确写回，原地图补既有frontend-state头，审计0缺anchor/40unmapped不当兼容验收。前端15s getter为配套来源，下一自然完整构建消费，不追已完成包/插件，未部署/操作Steam或睡眠，目标ACTIVE。唯一后继G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\opening-health-cadence\SOURCE_READY.json。
<!-- /DECKY-PENDING-HEALTH-CADENCE-20261113 -->


<!-- STEAM-HOST-NATIVE-COMPOSITION-20261113 -->
2026-11-13 Steam关闭请求 Host→Native→Power→Typed 组合模拟后继已导入：8项最终场景、2个必要语义反例、14个自然归零Job、6份编译图及请求资源/目标句柄归还核收。旧成功/失败/来源漂移原件保留，默认16、Native23、Host22未重跑。组合使用自有 inert Steam.exe 与事实适配器，最终Shell为惯性端口；真实Observer/注册表/UI/account、完整Native/Host包、跨完整性、Steam/睡眠/设备仍OPEN。无产品源码改动、部署或真实Steam操作。原件 Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/NativeCompose-20261113/SOURCE_READY.json、REPORT.zh-CN.md。
<!-- /STEAM-HOST-NATIVE-COMPOSITION-20261113 -->


<!-- DECKY-OPENING-PAIR-PLUGIN-BUILD-20261113 -->
2026-11-13 前端opening15秒getter叶已进入一次自然插件构建，原builder/default--check通过，518固定源/678工具、两Job自然关前0，仅dist锁内提升；buildId=e1ab37b7e12f3e69ee69091ec19b5f7af3a1bf934cc3a8047e221eb7ddaae58f，513533B（比cabc97+293B），官方Loader仍v3.3.1未改。原client/native通过只按各自epoch引用，不重跑业务或签整包；Native pending5秒观察叶须完整配套构建。本次不追赶固定后的并行差异，无安装/Steam操作或实机CPU睡眠视频验收，整体ACTIVE，原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\opening-health-cadence\plugin-build\SOURCE_READY.json。
<!-- /DECKY-OPENING-PAIR-PLUGIN-BUILD-20261113 -->


<!-- STEAM-PRESENCE-HOST-COMPOSITION-20261113 -->
2026-11-13 Steam缺席判定组合已接实际collectPresence：P2六项、两必要反例、8实际Job自然关前0、4编译图、48惯性PE及3/6目标句柄归还已核。第二实例仍活、partial、旧行和birth不符均不冒充全部关闭；P1误纳idle PID0的fixture FAIL保留。显式native-presence仅投影选择列表，原8/16未重跑，默认native14只准备未实跑。清单仅自有PID范围，ActiveProcess PID为惯性读适配，不代签全机缺席/真实注册表。产品0变更，无新ID/minimum/发布前置；完整Observer/Host写链/包与Steam睡眠实机仍OPEN。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/PresenceCompose-20261113/SOURCE_READY.json、REPORT.zh-CN.md。
<!-- /STEAM-PRESENCE-HOST-COMPOSITION-20261113 -->


<!-- DECKY-OPENING-FIXED-NATIVE-CLOSURE-20261113 -->
2026-11-13 opening15s前端/5s Native观察修正已进入固定原Native全编译链接与e1ab37插件配套来源。原build_native.bat实际exit0，无私改合同；1491源/3027初工具，1475include/最终54lib，漏OneCore后仅pin后按原main链接/mt/seal补链，不重编C++/其他PE。两实际Job自然关前0，5PE独立final-native，buildId=30e0ec8038a41535f4b9bb59cce68645b2713abf83dca3a5e2216e12b0fd0137。原启动准备UNKNOWN、OneCore及case后置FAIL保留；重链覆盖原路径事故已用逐字备份恢复，RESULT-r3全部原产物pin再匹配，最终另存，SOURCE_READY-r2/ACTUAL-GRAPH-final为当前权威。CPU键盘两绑定确参与全TU，不代业务/实机；两后继source drift只记录不追编。无Web/managed整包/部署或Steam设备睡眠，局部组合不可直接安装，CPU/视频/S0S3和降频仍OPEN，目标ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\opening-health-cadence\native-build\SOURCE_READY-r2.json、ARTIFACT-PATH-CORRECTION.json及REPORT.zh-CN.md。
<!-- /DECKY-OPENING-FIXED-NATIVE-CLOSURE-20261113 -->


<!-- STEAM-LIBRARY-HOST-FULL-BUILD-20261113 -->
2026-11-13 完整游戏库Host固定候选已一次编译原TU、复用对象完成原资源与链接：601实际编译输入/40本地、26实际库、原ICO载荷和版本资源、3Job自然关前0核收；50项shipping来源覆盖全部且末端drift0，未发现漏项，不改guard。省略.lib的准备解析FAIL与费用保留，只续资源/链接，不重编对象。RC21头是声明/前后pin，不冒称实际逐头IO。候选NOT_RUN，无产品变更、发布或部署。dormant steamStop与独立boot/manual出口按原合同分列。完整main/Worker/Web/plugin/包、Steam睡眠实机/CPU仍OPEN，目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostBuildClosure-20261113/SOURCE_READY.json、REPORT.zh-CN.md。
<!-- /STEAM-LIBRARY-HOST-FULL-BUILD-20261113 -->


<!-- STEAM-LIBRARY-HOST-WORKER-PAIR-20261113 -->
2026-11-13 固定来源Host/Worker私有构建配套已核：Worker498实际输入/20本地/17链接库/原资源，复用Host不重编，21共同来源及协议布局一致。3构建+1原来源门Job自然关前0；原门接受固定55来源/两候选，拒绝当前排除头漂移与混配EXE指纹。并行custom_steam_library_exclusions.h变化的当前身份FAIL及核收/准备FAIL、原费用保留，未追赶重编或倒填新pin。currentMainlineMatch=false、publicationEligible=false；产品清单/二进制未写、候选NOT_RUN，不是完整Native/Web/plugin/包或实机通过。后台媒体child属于Host，与SteamArtworkLab分别记录；总目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/WorkerBuildPair-20261113/SOURCE_READY-SOURCE-ADMISSION.json、REPORT.zh-CN.md。
<!-- /STEAM-LIBRARY-HOST-WORKER-PAIR-20261113 -->


<!-- DECKY-OPENING-PAIRED-FULL-EXPORT-20261113 -->
2026-11-13 Decky opening15秒getter/检查期5秒观察已进入同一固定完整YMCC自然导出。原publish -TestExport一次，guard412907ms/root0/Job自然关前0；619文件+5目录逐项CRC/SHA匹配stage，配套Native=fc0dcbe417e12e4c1ea7bfae66a2251cbc36883a05fc95ad4a5959f9b576f582，plugin=e1ab37b7e12f3e69ee69091ec19b5f7af3a1bf934cc3a8047e221eb7ddaae58f，build=1680743b30324a03bed76ac003b68cc5acc99cb7871ead5438a0c52f2f221344。私有PDB GUID/age匹配且不入ZIP，官方v3.3.1原字节、无新交付EXE。固定11072文件来源不追后继；实际1487include/56lib，额外工具取证漏queue头保持false，原导出合同完成与独立forensic缺口分列。旧路径消费FAIL/别名清单及全部费用保留，后继只读原产物不重编；夹具NOT_RUN/正式发布false。未由本聊天部署/操作Steam、游戏、设备或睡眠；交CPU维护owner协调，现场CPU/视频/S0S3与恢复降频仍OPEN，目标ACTIVE。包SHA=9ba10431376d9eb8a16d9543f19bc2cde444ece3b21c5bc2f41bc3219ac278e3；原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\opening-health-cadence\full-export\PACKAGE-READY.json及REPORT.zh-CN.md。
<!-- /DECKY-OPENING-PAIRED-FULL-EXPORT-20261113 -->


<!-- DECKY-CURRENT-LOADER-BASELINE-AND-RAW-TYPES-20261113 -->
2026-11-13 本机原3c12d786安装盘来源只读基线：原日志Loader35656及准确同映像子31748，持2句柄6读/10000.173ms，kernel+user均无可见增量，归还153→153；不是Steam renderer/插件JS/壁纸或整机CPU为零，不代新版收益。原ZIP33条steamFailure实际均dict/known0，原episode嵌套同为dict，str只存在于PowerShell低深度显示，首错误预期FAIL保留；只前向核收不改原件、不造产品日志bug或历史根因。两通道关闭码源码已存在，本次产品0改/业务测试0/新采样仅该10秒，无部署或Steam/游戏/设备/睡眠动作，配套包交原维护owner。S0S3/视频/全UI CPU和恢复降频仍OPEN，目标ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\current-loader-cpu-baseline-20261113\RESULT.json、RAW-FIELD-TYPE-INTAKE.json及REPORT.zh-CN.md。
<!-- /DECKY-CURRENT-LOADER-BASELINE-AND-RAW-TYPES-20261113 -->


<!-- STEAM-TYPED-WINDOWS-OWNER-20261113 -->
2026-11-13 固定Steam typed命令已核Windows用户/SID及SessionId：旧源2反例确证遗漏；原consumer新增8+受影响原7、实际自有进程/token/IPC组合2通过，23Job自然关前0，3成功图288/10、288/10、318/13。仅1产品头+4原夹具精确CAS，36原case正文保持；当前main并行漂移而原4切片/模板仍等于实跑。换行准备、漏helper编译及核收字段FAIL/费用保留，核收复用raw无业务重跑。不冒称AuthenticationId/实际跨session/UAC、完整44/14父或完整Native/Host/Worker/Web/plugin包；旧固定包/来源拒绝保持，无部署/真实Steam/设备/睡眠，总目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/TypedWindowsOwner-20261113/SOURCE_READY.json与REPORT.zh-CN.md。
<!-- /STEAM-TYPED-WINDOWS-OWNER-20261113 -->


<!-- DECKY-OPTIONAL-STEAM-DISPLAY-ISOLATION-20261113 -->
2026-11-13 旧实际客户端复现可选gameSteam坏显示误断合法核心；现在只剔除无效/迟到展示DTO，原run/generation/revision/game identity/权限及操作owner保持，合法当前/缓存展示保留，已下发TDP不因展示异常取消或重放。原3canonical/29边界与3非法selector、完整实际client类型72输入已核；484固定源/1052工具、9Job自然关前0。私有缺settings-baseline的2307 FAIL保留，补原字节仅续类型不重跑业务。仅1产品TS+1原fixture鲜读独占精确写回/逆hunk保并行字节；共享DTO和游戏/PID/动作层不改，无新ID/minimum/请求/timer/thread/恢复额。此叶晚于1680743b包，下一自然插件/包消费，不追旧包；无部署/Steam设备睡眠动作，历史根因及CPU/视频/S0S3/恢复降频仍OPEN，目标ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\optional-display-isolation-20261113\SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /DECKY-OPTIONAL-STEAM-DISPLAY-ISOLATION-20261113 -->


<!-- STEAM-HOST-COMMIT-CONSUMER-20261113 -->
2026-11-13 原库提交no-restart夹具过时签名已实编译复现并仅修2原文件：默认24保持，定向原8+异常4/反例2/selector拒绝2，20Job自然关前0，3图各400来源/7库。真实2574字节分支与当前正式默认提取输出一致，Buffer保CR/LF及source/slice/output/generator pins，wx/fsync/读回。关闭拒绝/未授权/不确定零写、离线无需关闭，本次completion覆盖旧snapshot；重新插restart和旧completion分别被原断言拒绝。旧编译FAIL/费用保留，精确CAS逆hunk保并行字节。只证明原命令分支与内存端口，不冒称实际FIFO/worker写盘/Native关闭/默认24整父。产品0、无部署或真实Steam/设备/睡眠；整目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostCommitConsumer-20261113/SOURCE_READY.json与REPORT.zh-CN.md。
<!-- /STEAM-HOST-COMMIT-CONSUMER-20261113 -->


<!-- DECKY-OPTIONAL-DISPLAY-PLUGIN-BUILD-20261113 -->
2026-11-13 可选Steam显示隔离已进入主线Decky载荷，原builder/default--check实际通过，518固定源/1052工具、2Job自然关前0；仅dist变化，buildId=afaf3a7797cda33e4b549fe87ac86a848619d81f34fe44a72476742c85294699，总516342B/增加2809B。当前共享cached group显示配套已编入，官方v3.3.1未改，无新增EXE。此前业务3case/29边界/类型72按原epoch引用，不重跑、不改旧失败；此首次载荷生产与已封存源码修复各自计费，原起点全墙钟另留，不重置预算。固定后差异不追赶，未改已完成1680743b或CPU10:42固定包；本叶下次自然整包消费，本聊天无安装/Steam设备睡眠动作。现场CPU/视频/S0S3及恢复降频仍OPEN，目标ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\optional-display-isolation-20261113\plugin-build\SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /DECKY-OPTIONAL-DISPLAY-PLUGIN-BUILD-20261113 -->


<!-- DECKY-INSTALLED-CPU-PACKAGE-RECOVERY-20261113 -->
2026-11-13 已独立核CPU维护owner实际安装：build ca3a45a3/EXE edf3701a/Native7ed91a47/plugin4392a414，PID29756/birth134390417050277906；原包5条Decky/Native条目匹配，不代本轮全619。编译源确含opening15s/Native pending5s，晚于固定的gameSteam隔离未装。新writer19:08:31.188恢复、38.197重建页/38.283 mutated，51.809三层true与state connected、51.835完成；原elapsed20641/日志配对20647分列。普通主备额度reserved且未申请upgrade，未见reopen大屏；Steam30612/birth134389420928041607当前仍原实例，2只读句柄归还。原source字面量预期FAIL保留，只续常量核收，无重编/测试/采样/本聊天安装或Steam设备睡眠动作。日志静默不代持续健康，inventory不代视频；CSS DOM失败单列不修外域。物理S0S3/视频/CPU与恢复降频仍OPEN，目标ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\installed-cpu-package-recovery-20261113\INTAKE.json及REPORT.zh-CN.md。
<!-- /DECKY-INSTALLED-CPU-PACKAGE-RECOVERY-20261113 -->


<!-- STEAM-ROLE-SIMULATION-ASSIST-20261113 -->
2026-11-13 原role组合失败协助：确证atomicJson目标247字符而.next-PID-TID临时路径越界status-open，旧EXE同等根长度反例复现；仅该函数Win32扩展路径修复精确CAS写回。r4私有与当前完整引擎翻译单元分别原单case21通过（20持久拒绝回执+1真值正例），不重跑已过角色63/观察8/旧46。原final探针命名冲突候选复用后一次编译/首次10检查通过，两真实Prepare保单次门/0publisher；PSS11阶段最终+2句柄槽（Key与未识别）未完整归因，不冒称全资源归还。9实际Job自然关前0；空Job准备UNKNOWN及准备/核收FAIL、原账保留，未导入未核角色引擎/driver/DLL。SOURCE_READY仅状态IO修复，角色动作/消费端/完整包和实机仍OPEN，总目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/GameProcessGroup-20261113/RoleLease/SimulationAssist-20261113/SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /STEAM-ROLE-SIMULATION-ASSIST-20261113 -->


<!-- DECKY-DEPLOYED-PEAK-ROOT-TRACE-INTAKE-20261113 -->
2026-11-13 Decky独立消费CPU当前edf/29756原trace：原StackWalk明确PID+UTC100ns/TID/IP交汇，293 ETLX仅280确认、触发33、未知68保持；这280已解析Native方法/源位置无Decky名字，SessionObserver17/触发3分列，不是函数独占成本或Decky零CPU，Loader/Steam renderer不在root范围。原丢事件0不签完整采样；无据不放慢恢复检查。只读同raw，不重采/重跑解析或测试/改其它域；源因/物理S0S3/视频/CPU和恢复降频仍OPEN，目标ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\deployed-peak-decky-attribution-20261113\ATTRIBUTION-INTAKE.json及REPORT.zh-CN.md。
<!-- /DECKY-DEPLOYED-PEAK-ROOT-TRACE-INTAKE-20261113 -->


<!-- STEAM-HOST-FIFO-COMPOSITION-20261113 -->
2026-11-13 原Host FIFO/close helper/commit消费者已沿原no-restart夹具接显式变体：8450/4123/2574原字节切片、真实worker/join，定向12通过；续deadline和Unknown误放行两反例均实触错误writer并拒绝，原N2后置文字FAIL复用raw核收不重跑。2非法选择/8真实文件系统提取控制，21Job自然关前0，3图各411源/11库，初编ADL FAIL及费用保留。仅3fixture精确CAS，默认24/原4正文保持，默认提取仍两输出，--fifo才加header与pins。facts/shutdown/clock/UI/磁盘受控，不代实际Observer/nativeIPC/worker写盘或完整Host/WebView/包，原跨epoch不拼父PASS。产品0，无部署/真实Steam设备睡眠，总目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostFifoComposition-20261113/SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /STEAM-HOST-FIFO-COMPOSITION-20261113 -->


<!-- DECKY-WAIT-ORIGIN-ERROR-20261113 -->
2026-11-13 Decky等待失败原始Win32错误已精确保留：wait后立即捕获、清理后仍发布原值。真实Windows两错误6/87×四惰性清理覆盖0/5/995/258，逐字原片段旧8错/候选0错，正常等待各1与句柄归还保持；只签原结果/失败消费片段，未签完整Runtime/清理实现或历史根因。首次EXE tool pin缺失在Resume前UNKNOWN及第二次输出占用拒绝保留，旧编译复用，4实际Job自然关前0。只1产品两hunk，无新增恢复/查询/线程/永久ID；下一自然Native配套，不追赶已完成包，无部署/Steam/睡眠动作或CPU收益证明，整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\wait-error-preservation-20261113\SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /DECKY-WAIT-ORIGIN-ERROR-20261113 -->


<!-- STEAM-WORKER-FACTS-ISOLATION-20261113 -->
2026-11-13 原Worker公共facts切换后，旧Toolhelp bool不再控制Steam guard，已由原datachain旧EXE复现Unknown拒绝。只修2原fixture：正式types+Worker include局部facts端口、自有安装根嵌套归还，默认无安装/不伪造动作身份；7新增/4原定向跨固定epoch通过，2必要反例/2非法选择，21真实Job自然关前0。5图各511源/17库；旧编译漏ACL头后置FAIL保留，原EXE不重编，后继4图完整预声明。新成功case的recent更新误断言只续1项纠正；准备与后置FAIL/费用保留，不拼默认整父。两fixture fresh CAS和逆hunk，导入后并行成员诊断后继保留，本段hunk/新setup/4旧case块逐字保持，原完整文件读回拒绝留证，不追赶重编。facts受控、原真实文件事务，非实际公共provider/nativeIPC/Host组合，另3runtime consumer套未跑。产品0，无部署或真实Steam/YMCC设备睡眠，总目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/WorkerFactsIsolation-20261113/SOURCE_READY.json与REPORT.zh-CN.md。
<!-- /STEAM-WORKER-FACTS-ISOLATION-20261113 -->


<!-- DECKY-TERMINAL-RECONNECT-REFUSAL-20261113 -->
2026-11-13 新Win5 194242尾部已加载afaf并17203ms三路恢复；检查/结果双信号不当重复点击，未证明视觉/睡眠。原Runtime明确非重试准入拒绝仍空等7s已由实际typed原消费者复现。仅1产品hunk将明确失败交原阶梯，待确认和有效retained保持；原注册新增定向参数3场景，候选两terminal step观察[16, 16]ms、retained1计划0页面，OFF/资源/句柄归还与2非法选择均核。首候选漏fixture健康通知FAIL保留，修后只续候选，旧反例不重跑；默认注册/原case正文保持，无新ID/minimum/恢复额度或计时变更。两文件精确写回，无完整Native/包/部署、现场收益/物理睡眠或CPU改善证明，整体ACTIVE。原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\field-win5-194242-20261113\SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /DECKY-TERMINAL-RECONNECT-REFUSAL-20261113 -->


<!-- STEAM-WORKER-NATIVE-FACTS-IPC-20261113 -->
2026-11-13 原datachain显式YMCC_LIBRARY_FACTS_IPC变体已接正式client/read/serve与Wire、真实进程/token/父子/nonce/WM_COPYDATA/映射，再到原磁盘事务。Q定向6通过：缺席真提交、初始在场/late Unknown/late实际惯性Steam/重复provider/错误client拒绝且原字节保持；禁最终guard反例实际错误提交并拒绝，2非法选择。13真实Job自然关前0、18内部child正常join、32惯性PE pins、3图520/521/521源各17库。原P冷总handle+25/+2 FAIL保留；Q复用原真实API ledger归还0，但冷总差额仍OPEN，不代全kernel/SDK。2原fixture+2显式include精确CAS，默认未定义宏600/88796词法token等价不算默认实跑，无新ID/minimum/父门。实际callerOwned用真清单父子；Presence只本方清单、ActiveProcess读取拒绝，不代全机缺席/实际Observer/typed shutdown/完整Host FIFO链。导入后置TU文件名拒绝修正不重跑；完整包/实机仍OPEN，总目标ACTIVE，无产品或部署/真实Steam设备睡眠。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/WorkerNativeIpc-20261113/SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /STEAM-WORKER-NATIVE-FACTS-IPC-20261113 -->


<!-- DECKY-TERMINAL-PAIRED-EXPORT-20261113 -->
2026-11-13 Decky125154Z一次原完整主线TestExport已核：终止准入不空等7s/Win32原错保留/afaf配套，5PE、1495实际include/56lib、未闭合输入0、匹配PDB；619文件CRC/SHA及完整stage集合核收，两个构建/finish Job自然关前0。仅再读5关键ZIP成员，未重跑producer/默认夹具；NOT_RUN与正式发布未验收保持，旧定向/FAIL按epoch引用。共享层实际8参默认组关闭/官方ABI6配套已消费。包G:\YeManCC-Work\Deliverables\YMCC-Decky-Recovery-Test-20261113-125154Z\YeManCC.zip，SHAe0e5632c549e91867d1c49aeab1f951edf516be85db0be3c1414eb924c8f674e，build 0d2c6518946514fa1dceef825159fc008a444d460eed4bf7e76b48a6779c77f9；未由本聊天部署/操作Steam睡眠或测实机收益，目标ACTIVE。末次主线差异单列不追编。唯一原件G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyRetainedBackendRecovery-20261111\verified-health-wake-20261112\shared-admission-review-20261112\recovery-cause-trace-20261113\paired-export-20261113\NaturalExport\EXPORT-INTAKE.json及REPORT.zh-CN.md。
<!-- /DECKY-TERMINAL-PAIRED-EXPORT-20261113 -->


<!-- STEAM-HOST-WORKER-FIFO-IPC-20261113 -->
2026-11-13 原Host FIFO/commit/runProcess/真实管道与Job/完成reader已接原Worker CLI及正式facts IPC到原磁盘事务。六定向按Q前三/R后三分来源核收，不拼完整父；去重/关闭保留接受写/最终Unknown与本方Steam出现保护文件/退出77后FIFO接续均核。必要旧快照反例真实提交成功而原Host回执拒绝，2非法选择。23真实Job自然关前0、8成功编译图、32内部child、50PE pins；请求API归还不代全kernel/SDK，冷handle欠账仍OPEN。5原fixture+2显式include精确CAS，逆hunk保并行字节；datachain并行literal默认正文保留，合并全文件未冒称重编。初提取UNKNOWN/编译及两资源观察FAIL和后置换行FAIL均保留，只续受影响/未执行，不重跑前三。UI/target/snapshot为受控端口，Presence仅owned清单、callerOwned真实OS父子，typed Shutdown/实际Observer/完整Host WebView/全消费者与匹配包及实机OPEN。无新ID/minimum/默认父门，无产品/部署或真实Steam设备睡眠动作，整体ACTIVE。唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /STEAM-HOST-WORKER-FIFO-IPC-20261113 -->


<!-- STEAM-TYPED-HOST-WORKER-IPC-20261113 -->
2026-11-13 原Host FIFO/Worker真实IPC磁盘组合已接正式Shutdown sender/producer、原main handler/typed dispatch与公共capture/current片段。TR固定TH2/TP三项通过：一次动作确认本方目标退出后一次真提交；原Dispatch排队后Suspend撤销、queue拒绝均一次请求且零claim/动作/Worker，两原文件保持、零暂存/备份。必要单项将shared current置true后实际迟到退出，原消费者拒绝，不能用文件保护替动作安全；反例raw originalPowerBodies标签按变异来源明确无效。2非法选择/Unknown未实现拒绝，Unknown业务NOT_RUN。10真实Job自然关前0、4图、13内部child、20PE；观察API归还不代全kernel/SDK，旧冷差额OPEN。4原fixture+1typed include精确CAS，原generator三include在私有snapshot生成，无新ID/minimum/默认父；并行literal正文保留、合并整文件未重编。旧准备pin漂移EVIDENCE_INCOMPLETE/后置可选bytes FAIL及全部费用保持，无业务封存重跑。Observer为明确端口、最终Shell仅本方SetEvent+自然join，不签实际Steam退出/全机absence/完整Host WebView/全消费者与包及实机。整体ACTIVE，无产品/部署或真实Steam设备睡眠。唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/typed-shutdown/SOURCE_READY.json与REPORT.zh-CN.md。
<!-- /STEAM-TYPED-HOST-WORKER-IPC-20261113 -->


<!-- STEAM-ACK-UNKNOWN-HOST-WORKER-20261113 -->
2026-11-13 两个真实action ack迟到Unknown组合已接原Host FIFO/Worker：cancel在真实sender返回后Wire cancelled1/claimed0/completed0，再由原pending拒绝；submitted已自然退出本方target一次，Wire cancelled1/claimed1/completed1/Submitted，但客户端仍Unknown。两者一次请求/真实返回信号、零Worker/暂存/备份、原两文件保持。实际SMTO/Outcome未替换，原1500/5000/20000ms合同保持，Returned event仅观察真实shutdown返回、nonce只读mapping和借用Host句柄均按实际归还。必要反例只删Host outcome拒绝段，真实Unknown后实际Worker退出0并错误提交文件，原消费者准确拒绝；非资源误报。8真实Job自然关前0、3图423/423/531各17库、10内部child/15PE、2非法选择。4原fixture精确CAS并与实编字节相同，无新ID/minimum/默认父，不重跑旧3/6。共享原generator/native切片不计本轮实跑，完整kernel/SDK/冷差额仍OPEN；Observer明确端口、Shell只本方事件+自然join，完整Host WebView/全消费者/配套包与实机OPEN，整体ACTIVE，无产品/部署或真实Steam设备睡眠动作。唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/typed-shutdown/ack-unknown/SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /STEAM-ACK-UNKNOWN-HOST-WORKER-20261113 -->


<!-- STEAM-ACK-UNKNOWN-HOST-WORKER-CORRECTION-20261113 -->
2026-11-13 核收后继：两正例/反例provider总handle128→133，+5仍未归因。反例首waiter断言失败后未到达Host nested Job查询/原终态断言，raw初始0不作实际查零证明；此前typed TNC同类范围亦不声称已查零。8外层真实原子Job自然关前0保持；反例仍实际Unknown后一次动作、Worker退出0及错误事务，两文件已变，语义控制有效。SMTO未替换且真实Wire/Returned握手支持未取得及时ack和Unknown，不额外声称直接观测ERROR_TIMEOUT。QUALIFICATION-r2/SOURCE_READY-r2/REPORT-r2当前，旧原件/费用保留，无业务重跑。唯一原件同ack-unknown/EVIDENCE-CORRECTION.json及SOURCE_READY-r2.json，整体ACTIVE。
<!-- /STEAM-ACK-UNKNOWN-HOST-WORKER-CORRECTION-20261113 -->


<!-- STEAM-REAL-OBSERVER-HOST-WORKER-20261113 -->
2026-11-13 真实隔离Observer已接原main完整信息分支/原collect/真实IPC到Host FIFO/typed/Worker文件事务。原类首次宏隔离、每PID新测试HKCU子键actual publication绑定PID/birth/root/account42/Windows session；正常6collect完成一次动作/提交，第二collect原读后Dispatch撤销时原facts仍available却由最终main current拒绝发布，动作/Worker0且文件保持。仅删最后current反例实际发布旧facts并尝试一次action，原消费者拒绝，后续action仍阻断零实际动作/Worker/写；反例nested查询observed=false，两正例实际查0。8外层真实Job自然关前0、3图423/534/534各17库、10child/15PE/3子键删除及不存在复查。Observer原stop join/身份清空不代线程/事件/HKEY/hook逐对象归还；128→134的+6及祖先容器恢复/全SDK仍OPEN。原generator显式--information-reader默认四体逐字同、第五原分支重建，6内存拒绝非业务；2非法选择。4原fixture+1原generator+1新include精确CAS，无新ID/minimum/默认父、旧2/3/6不重跑。准备schema/缺配置未准入/可选字段核收FAIL与费用保留。Observer实类为真但Presence仍owned census、mode端口未调用，Session/Installations/global及完整Host WebView/全消费者/包与实机OPEN，整体ACTIVE，无产品/部署或真实Steam设备睡眠。唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/observer-chain/SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /STEAM-REAL-OBSERVER-HOST-WORKER-20261113 -->


<!-- STEAM-OBSERVER-RESOURCE-ATTRIBUTION-20261114 -->
2026-11-14 本地后继（UTC原件11-13）：Observer直接资源ledger与本进程table已接原撤销case。事件3/进程4/Key1/Hook1正例全归还；保留事件控制进程2次，真实1遗留被拒绝后实际关闭恢复，不固定实时inventory次数。std Observer线程ID匹配live handle并于原stop/join后消失。128→134剩余类型2Key/3EtwRegistration/1File，私有Desktop关闭另减1，具体SDK/cache所有者仍OPEN、不签全部kernel。snapshot分拆保原65535B，按新raw重建旧聚合206205B；旧三producer FAIL/更早首因不明、RP5未准入owner-query UNKNOWN及准备/后置FAIL费用保留。13实际Job自然关前0、6编译图，成功/控制6内部child原归还；失败不借Job0签owner。Resource宏只允许原撤销case，2选择拒绝，普通Observer仅另编译；正常typed跨close资源组合未签，旧normal/2/3/6不重跑。2原fixture+2include精确CAS、无新ID/minimum/默认父，无产品/部署或真实Steam设备睡眠。全局/SDK/Host WebView/配套包/现场仍OPEN，整体ACTIVE。唯一后继Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/observer-chain/resource-attribution/SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /STEAM-OBSERVER-RESOURCE-ATTRIBUTION-20261114 -->


<!-- STEAM-OBSERVER-TYPED-RETURN-PAIRING-20261114 -->
2026-11-14环境标签／11-13 UTC原件：正常typed Process获取与析构关闭的Observer观察归属已配对，同锁查找/真实关闭/原记录更新，其他请求HANDLE解锁后转原账本。原normal case实际完整Host/typed/Worker文件提交，Observer直接Event3/Process5/Key1/Hook1全归还，配对1/转发4；Process计数按实际逐行核，不固定。旧错配控制同样真实退出和提交但留1观察账本Process，被资源门拒，不称kernel leak或再次关闭旧值。7外层Job自然关前0、3编译图、6内部child归还；空/未知2项拒绝，普通分支仅编译，旧revoked不重跑。3既有fixture精确CAS等于实跑源，无产品/新ID/minimum/默认父/部署。旧cwd未准入及只读线程归属核收FAIL保留，后继仅复读同raw；中断后不重启已完编译。差额2Key/3ETW/1File所有者、全局/Host WebView/SourceLease/包/现场OPEN，整体ACTIVE。权威Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/observer-chain/resource-attribution/typed-return/SOURCE_READY.json及REPORT.zh-CN.md。
<!-- /STEAM-OBSERVER-TYPED-RETURN-PAIRING-20261114 -->


<!-- STEAM-OBSERVER-CNG-PEER157-20261114 -->
2026-11-14后继：原sha256函数及Observer self census逐字进入独立冷诊断；无CNG对照零变化，首次CNG新增2ETW/1File/1HKLM Key且bcryptprimitives首次加载，后续3次无增减，32真实CNG返回成功。加强首次初始化区间证据，不证明原Provider具体SDK对象所有者；原2HKCU Key/3ETW/1File完整归属仍OPEN。1编译/2诊断Job实际关前0，诊断2自然，编译仅准确身份vctip工具限定回收，不能称全自然；未执行草稿及2只读FAIL保留，只复读同raw。中央157原OFF94/exit63已核native writer迟到归还→distinct recovery→同managed Host/pipe→最终清理，3owner重取/2旧pipe缺席/9坏回执拒绝为引用，本读入0 producer/编译。当前源码后继独立，原子receipt naturalExitObserved=null由native父exit0证明，累计spawn1/2不误当重建；两个核收FAIL保留。无产品/主线fixture/新ID/minimum/包/部署/现场操作，currentWholeSourceReady=false，整体ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/residual-cng/EVIDENCE_READY.json及peer-intake/native-host-combined157/READBACK.json。
<!-- /STEAM-OBSERVER-CNG-PEER157-20261114 -->


<!-- STEAM-NATIVE-ROOT-CLOSE-FAILURE-INTAKE-20261114 -->
2026-11-14中央真实受保护root失败已核原件：child退出/writer durable settled/Job0与原root CloseHandle成功分开。实际Poll遗忘仍有效root及pending，被原独立root断言拒绝，INCOMPLETE/exit3、36到达及确切FAIL保留。测试控制后置解除保护/checkedClose、owner独占归还不视作产品已修。157正常软件快照不重跑、不改范围；lease关闭失败、当前产品修复/ProcessExit/真实SDK/现场唯一原因仍OPEN，由原手柄owner接续。此读入0编译/producer/产品写/包/部署/现场，无新ID/minimum。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/peer-intake/native-root-close-denied/READBACK.json。整体ACTIVE。
<!-- /STEAM-NATIVE-ROOT-CLOSE-FAILURE-INTAKE-20261114 -->


<!-- STEAM-CURRENT-RECOVERY-TUPLE-INTAKE-20261114 -->
2026-11-14新恢复tuple实际组合已前向核收：固定原OFF94/exit63共157 PASS，各creation后validated HELLO共4条successor-bound；同raw原writer attempt/currentRequest3/newRecovery4与Host epoch/run/instance/actual PID/pipePID相关。21错绑/缺证拒绝及同身份重复HELLO接受是同raw消费者控制，非21新产品场景、不拼旧20。710来源/1477include沿原生产者范围，当前主线后继分列、不追编或授整父/cache；vctip工具收尾与业务自然exit0分开。前向关闭仅此快照“新tuple无组合证据”的旧限定，历史FAIL及真实root关闭失败不覆盖；lease失败、产品ProcessExit、设备/实机OPEN。本读入0编译/producer/产品写/包/现场，新ID/minimum0，整体ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/peer-intake/current-recovery-tuple/READBACK.json。
<!-- /STEAM-CURRENT-RECOVERY-TUPLE-INTAKE-20261114 -->


<!-- STEAM-LAUNCH-LEASE-CLOSE-FAILURE-INTAKE-20261114 -->
2026-11-14父继承租约真实关闭失败已前向核收。creationReturned清parent duplicate字段后，真实受保护副本仍持share0；child退出/精确Job0/root关闭/writer durable settled/record.active=null均成立，Session仍32 owner-open-or-lock-failed。仅解保护并关闭同一确切副本后Session独立重开ready，原归还断言拒绝；INCOMPLETE/exit3、40到达及1FAIL保留，产品正例通过0，wrapper0不当业务PASS。仅前向关闭“lease失败尚未实证”，产品fix和其余异常/析构/shared Session/ProcessExit/物理/现场唯一原因继续OPEN。旧157/新tuple/root负例不重跑，本读入0编译/producer/包/现场，无新ID/minimum。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/peer-intake/launch-lease-close-denied/READBACK.json。整体ACTIVE。
<!-- /STEAM-LAUNCH-LEASE-CLOSE-FAILURE-INTAKE-20261114 -->


<!-- STEAM-LEASE-CLEANUP-INDEPENDENT-REVIEW-INTAKE-20261114 -->
2026-11-14租约独立review和私有清理纠正已核原件：review支持root/Job0/durable record不能代parentduplicate归还，原INCOMPLETE/exit3/40到达/唯一Session check36失败不变。私有提前别名风险现按原Scope归属处理：仍归owner只解保护/放弃alias，已遗失才checkedClose。精确helper在真实Win32缩减端口组件safe3通过/old前2反例失败，第三同结果不加唯一覆盖；future生成器/支持片段来源回执与字节核对，原full-TU/raw不改，无40/157重跑。仅私有清理纠正，不代产品fix或完整early exception/析构/shared Session；后置projection FAIL/历史费用保留。此读入0编译/producer/产品写/新探针/包/现场，无新ID/minimum，整体ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/peer-intake/launch-lease-close-denied/cleanup-review-successor/READBACK.json。
<!-- /STEAM-LEASE-CLEANUP-INDEPENDENT-REVIEW-INTAKE-20261114 -->


<!-- STEAM-OWNER-ROOT-FORMAL-COUNT-CORRECTION-INTAKE-20261114 -->
2026-11-14正式root/ClearLease2hunk与ROG/GPD限域后继已核，8既有+1新文件37精确hunk正逆重建；2289refs沿原核收范围引用。ROG76/GPD83各3真实managed child归还，原父FAILED_WITH_TOOL_CLEANUP/exit2和三家整父INCOMPLETE不改绿。MSI原46 trace=44PASS/2FAIL，第33 cached late proof/第46 recovery波次分列，只有前32连续PASS；旧45摘要原件保留，owner独占后继，本方不重复生产。中央tuple消费者并行覆盖02dca原件保留，四exact操作恢复3b686并逆核，既有157/21坏回执仅复读。六prehost拒绝选择27各/162已固定快照实跑，2.652秒无native编译，不与旧90拼父。本读入0编译/producer/产品fixture写/包/现场；父duplicate修复独立OPEN，旧FAIL/UNKNOWN/metadata越窗和费用保留，全目标ACTIVE。原件Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/LibraryShutdownAdmission-20261113/HostWorkerFifoIpc-20261113/peer-intake/owner-root-mainline/READBACK.json。
<!-- /STEAM-OWNER-ROOT-FORMAL-COUNT-CORRECTION-INTAKE-20261114 -->
