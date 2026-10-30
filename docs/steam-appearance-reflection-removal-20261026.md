# Steam 外观：永久移除封面倒影（2026-10-26）

用户要求永久移除 Game Cover Reflections。该主题不再包含在 UI、默认预设、渲染回执计划、资源清单或导出载荷中。主线现在管理 7 个效果，新用户与恢复默认均为 7/7 全开；Footer Options=Default、Mini Carousel Size=0.7。用户已经保存的其他主题开关和参数在升级时保留。

旧配置迁移沿用现有的有界资源更新/备份/重读事务，只移除倒影的 theme.json、config_USER.json、shared.css、LICENSE。可识别商店下载的同名异目录；其他活跃外部主题仍维持原接管规则。删除失败恢复已删除文件及 owner；显式恢复旧配置不重引入倒影，历史原始备份保持不变。CSSLoader Desktop 编辑保护及唯一常驻后台合同继续生效，没有增加轮询、进程扫描或 Steam 启动/模式切换。

预装目录：D:/Win11/TEST/野蛮整合优化系统/1C盘主文件/SOFT/CSSLoader Desktop/homebrew/themes

单文件包：同层 homebrew/YMCC-Steam-Themes-7-AllOn.zip，内部是 themes/，包含 60 文件，其中 43 CSS、7 份 config_USER.json 全部 active=true。不包含本机 backend.path、owner.json 或启动命令；可复制到目标用户 %USERPROFILE%/homebrew/themes。覆盖已有旧目录时需要移除原 Game Cover Reflections，文件夹合并复制不会自动删除它。

验证范围：已有 owned-upgrade 消费者（PS5.1）80 项、render-plan 317 项、导出门 24 项、草稿 42 项、Edge 焦点稳定性 5 项；Vite 前端构建通过。保留中间失败证据并以 final/corrected 回执为准。仅为本功能专项验证，不代表完整发布验收或实机 GPU 性能收益测量。

证据：G:/YeManCC-Work/Testing/YMCC/Evidence/SteamReflectionRemoval-20261026。预装 ZIP SHA256：1bc00ab5c6ca4eb27ef098f009fdf871979cfff471cbdcffdcd2a1abaaad54e1。

本轮写入主线及预装主题目录，未替换本机 YMCC 程序或变更正在运行的 Steam/CSSLoader。
