# 已知采集错误与修正 · v10.1

## 新增补正

原v10 PS7 140/140不覆盖PS5路径API差异。ROG报告PS5 runner138/140，wildcard/ADS两例在GetFullPath先抛，未到描述符门；当前源码与本机纯API探针独立支持该顺序问题。它破坏固定错误/阶段契约，不证明安全门被绕过，也不是低CPU数据。

v10.1要求原始字面量先拒绝，再合法归一化；异常安全固定映射；非法路径不读token/目标或RunAs。负例不通过Join-Path构造，排序回归防止PS7容忍非法字符掩盖再引入。合法relative/abs/UNC/中文空格维持。实际实现和覆盖只以本次代码/validation为准。

## 继承并保持

- 只读preflight/status有限同用户提权与身份发现，默认不提权；不先要求要发现的hash/birth，observed不转授权。
- 低token parent只做有限参数/描述符与非只读trusted/selector门，实际file/hash/reparse/owner/birth在可读上下文、任何动作前核验。
- PID兼容有界int/uint32/long，不扩大协议sequence；权限未请求/未授权/未尝试/拒绝/worker失败分别记。
- 全owned子树、WebView2/Fan/Input/辅助唯一计量、gyro共享PID单算、拒读/null/倒退不补0；实际间隔/覆盖/自耗，整体分位数先逐样本求和。
- UTC不文化字符串化；LogFile显式最多32个元数据，不扫内容；不导出commandLine/原始握手。

## 已纠正的文书排程

产品CPU优化＋错误修复已由R6启动，风扇/手柄暂停不再阻挡本机开工；原文书等待全结束的HOLD不沿用。不要求UAC提示必须可用，实际高token/同用户/单次worker才是事实门。本机可先修bug，ROG最后集中必要性能验证。

原v10 validate raw已收到，collector/runner raw仍待交；v9文件在ROG生成不等于已交回。复制已有证据即可，不为补件重试产品。collector源码未改，其继承PS7软件证据不是ROG通过；本机PS5默认策略限制也不等于该运行环境下接口不可用。YMCC产品bug/日志/性能由另一执行AI主线处理，本工具不代签修完。
