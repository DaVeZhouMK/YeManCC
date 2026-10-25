# G3E：35W以上资料核验

核验日期：2026-10-17。结论仅修正功耗证据与曲线可信区间，不修改其他机型数值。

| 项目 | 原始来源与结论 |
|---|---|
| X2 Mini | 壹号本官方产品页标称45W持续性能。https://onexplayerstore.com/collections/new-arrival/products/onexplayer-x2-mini-with-intel%C2%AE-arc%E2%84%A2-g3-extreme-8-8-oled-3-in-1-gaming-handheld |
| APEX Air | 壹号本官方对比文章将其标为G3 Extreme、45W持续释放。https://onexplayerstore.com/blogs/weekly-blog/apex-vs-apex-air-comparison |
| 处理器 | Intel ARK列出可保证基础功耗35W、可配置TDP8–35W、最大睿频功耗80W；这些项目不能代替整机持续释放。https://www.intel.com/content/www/us/en/products/sku/245625/intel-arc-g3-extreme-processor-12m-cache-up-to-4-70-ghz/specifications.html |

所以35W不能写成G3E硬件或GPU性能的确定上限。官方45W配置也不能单独证明GPU在35–45W必然提高多少。

本轮未取得相同样机、内存配置、图形驱动与测试条件下的35/40/45W Time Spy显卡分扫描。产品总分、游戏FPS、不同机型成绩与最大睿频规格都不能直接生成这些GPU点。机主在不同负载下也有高于35W收益很小的反馈，不把个案当统一曲线。

保留现有模拟数值作为明确标记的占位，不编造45W TS分。能力库为G3E增加powerEvidence与unverifiedGpuAboveWatts=35。机型列表不再将35W记录为确认的平台起点，图像在高于35W区域显示未校准提示。模型上限仍为原80W范围，用户设150W仍受同一模拟封顶限制。
