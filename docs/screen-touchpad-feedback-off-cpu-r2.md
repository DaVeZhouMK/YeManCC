# 触摸反馈关闭时的距离计算优化

范围只限触摸板/触摸按键；监控问题已交接，此轮未触碰监控。沿用用户允许模拟的授权，使用生产头文件和替身输入API，不创建窗口、不注入输入、不运行手柄或震动设备。

## 确定的多余工作

当触摸发生在YMCC自己前台，或native输出暂不可用时，手势进入FeedbackOnlyContacts。这份状态同时支撑触点外观和震动。之前即便touchFeedback=Off，MOVE仍调用Motion::move：超过噪声阈值时会更新锚点、计算平方根、累加距离、计算detent及时间门。调用者只在最后因strength=0不发脉冲，前面的计算仍发生。

正常Core事件早已在feedbackStrength>0时才调用Motion::move；本轮使反馈专用路由遵循相同开关边界。

## 修改

native/touch_haptic_dispatch.h 为FeedbackOnlyContacts::event增加默认true的feedback参数。所有路径仍校验ID/范围、夹取坐标并维护接触所有权；DOWN保持原begin初始化，UP直接释放，MOVE先更新x/y，再仅在feedback为true时执行Motion::move。

native/screen_touchpads.h 的唯一生产调用传入当前strength>0。开启反馈时表达式执行同一个Motion::move算法，没有改强度、距离、时间门或队列。关闭时保留真实触点位置、按下轮廓和光圈。配置中touchFeedback改变本来属于reset，会先release旧接触后重用，因此重新开启不会从旧关闭期间的坐标补发积压脉冲。

没有新增线程、定时器或日志热路径；不改变采样/报告频率、键鼠映射、前台规则或光标policy8。

## 实际头文件模拟

新增tools/screen_touchpads_feedback_off_cpu_simulation.cpp与.ps1。程序包含完整screen_touchpads.h，虚拟时间、输入和光标查询为替身；基线/修复使用同一个程序与相同输入序列。

关闭反馈时10,000次超过阈值的移动，基线的motion锚点改变10,000次，修正后为0；两者的触点x/y都保持更新。该指标是距离状态更新计数，不是整机CPU百分比。

25项模拟通过：关闭时位置与surfacePressed、无电机请求、错误指针、UP释放；三档DOWN/MOVE脉冲、静止无追加、UP释放；native输出阻塞；PS同面双指；开启反馈前旧手势释放及新手势单脉冲。上一轮实际按钮/输入输出模拟另16项回归通过，重复映射撤销仍为0。

复跑当前版：

```powershell
& tools/screen_touchpads_feedback_off_cpu_simulation.ps1 -OutputDirectory <新的输出目录>
```

历史基线可附加-BaselineDirectory指向本轮Before/native并传-ExpectBaseline。输出目录不得包含已有receipt.json。隔离编译/模拟不等于完整应用构建、实机CPU测量或SD消闪验收。

证据目录：G:/YeManCC-Work/Build/Analysis/Touchpad-CPU-Simulation-R2。保留Before、Feedback-Off-Before/After、Buttons-Regression及交付哈希。
