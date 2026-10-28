# Steam 主题：保留比例与动态壁纸，取消附加特效（2026-10-28）

用户最终要求是“画面显示比例不变，附加类全部关闭，只保留布局和动态壁纸”。以后维护应沿用此规则；不得再次添加封面滤镜、渐变遮罩、光晕、阴影、立体键盘或呼吸/闪光动画。必要静态焦点框与功能导航保留。

## 最终改动

- Art Hero：原字体、宽高、位置、object-fit/object-position、Footer参数与Mini缩放不动；取消全幅图片identity brightness滤镜、双层渐变mask、背景动效/预提升和封面装饰光晕。
- Darken Unfocused Games：取消暗化、饱和度处理与动画，保留旧参数schema以免已有配置报错；旧值不再恢复滤镜。界面说明改为静态焦点提示。
- NEX-85 Keyboard：两套明暗主题和全部7种焦点配色改平面色，移除图标滤镜、按键阴影及装饰伪层；按键/文字尺寸与间距保留。
- Round：保留原半径和静态轮廓、去阴影、按钮闪光和动画。YMCC Game Details：原高度与min-height公式逐字保留，只追加背景去滤镜/mask/动效。
- Mini Carousel、Centered Game Text全部文件以及7份默认config_USER.json与改前逐字节相同。7/7默认仍开启，Footer Default、轮播Size0.7继续保留。用户自己的active/参数未重置。
- 没有新增轮询、JS动画、额外覆盖进程或Steam启动/重启/模式切换代码；没有改动态壁纸播放器或官方Decky Loader。

## 原因和现场证据

Decky任务独占现场。它提供的C1单变量对照：仅把Art Hero IMG的brightness(100%)变为none，CANDIDATE-ART-FILTER-NONE为0条warning/0次tile OOM；原样恢复ORIGINAL-AFTER-FILTER-CANDIDATE为240/263；FILTER-NONE-REPEAT再次0/0。该证据支持身份滤镜与特定触发路径的因果关联，不能外推到所有内存问题。

最终平面版由Decky任务统一部署，FINAL-THEME-DEPLOY-STATE.json确认manifest匹配、7份用户配置值保留、现有CSSLoader重读成功。FINAL-INSTALLED-PRE-RESTART.json的1567个tile状态记录中OOM为0，新增warning为0；这是有限现场场景，后续冷启动/长时/远端掌机仍归其最终验收，不能把此前单变量对照替代最终版验收。

## 本任务专项验证

已有stable-focus消费者48项通过，其中1280×720/1280×800、5档轮播尺寸×3档hero布局共30组修改前后DOM几何deepEqual，检查了首页/封面/详情/按钮/footer/键盘/壁纸尺寸与变换；43份CSS的所有选项没有附加filter/mask/shadow/渐变/命名animation。动态壁纸fixture仍保持自己的wallpaperOnly动画，主题没有命中它。已有cover-focus消费者16项保留真实点击与焦点变换检查；禁止封面滤镜反例被正确拒绝。render-plan317、export24、owned-upgrade86、前端生产构建通过。Decky同期修的preset空退役目录归Decky，另用原owned-upgrade消费者同源86项复核，回执见FINAL-IDENTITY。

用户扩大范围后不再要求像素完全相等：取消渐变/阴影允许颜色、抗锯齿差异，原几何断言严格保留。C1单点旧像素相等证据仍单独保留。没有把DOM夹具的视频节点当真实解码/呈现验收。完整发布夹具NOT_RUN。

## 交付

- 主线manifest SHA256：bf307446d53beba339af2a2e55859369cec3bfa73edda10d0fdd1b7ec20730db。
- 预装目录：D:/Win11/TEST/野蛮整合优化系统/1C盘主文件/SOFT/CSSLoader Desktop/homebrew/themes。
- 最新平面版压缩包：同层homebrew/YMCC-Steam-Themes-Layout-Wallpaper-20261028.zip，60文件、43CSS、7份配置全部active=true，SHA256 82eb3201503166e8561e6f125aaa3947e90f745248e12e67b00723217fad889e。
- 旧YMCC-Steam-Themes-7-AllOn.zip保留作为前版，最终用上面的有日期平面包；预装themes目录本身已经是最终版。

自动审批拒绝了替换旧ZIP并删除临时回滚副本的命令，工具只给“blocked by policy”；本轮改为保留原包、另存版本化新ZIP。没有继续尝试删除/替换原包。临时pending文件保留。

独占现场与最终部署回执由Decky任务维护。主题侧只控制源码、离线验证和预装目录；不把其现场操作伪装为本任务执行。精确文件清单、原失败、前后包和所有回执位于G:/YeManCC-Work/Testing/YMCC/Evidence/Decky-RenderPressure-20261028/Theme-Side。
