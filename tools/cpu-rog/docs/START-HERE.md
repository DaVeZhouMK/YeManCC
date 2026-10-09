# 从这里开始 · v10.1 · 2026-10-04

## 不再把产品任务误退回HOLD

用户已启动CPU优化＋错误修复，由另一执行AI按R6接收当前主线、先修bug/日志/计量并优化，最后集中ROG性能验证。风扇/手柄暂停不是完成验收，但不再成为本机开工的等待条件。本工具仅做工具补正，不触碰产品制作线。

## 搬运

只复制ROG-CPU-COLLECTOR-20261004-v10.1目录或同名ZIP，解压到新目录；保留v10/v9，不覆盖旧包、不混旧Results，不复制整个桌面CPU根或settings备份。工具包没有YMCC产品，产品由主线执行AI另交可信完整包和A/B身份。

先核工具ZIP/manifest和Action validate，按OPERATOR-COMMANDS离线复测。结果与DELIVERY-STATUS区分：原v10 ROG validate原始结果已收到；原v10 collector PASS/runner138/140 FAIL仍待各自原始JSON。v10.1在ROG的验证不能继承原v10结果。

## 不变的动作门

默认validate不查真实token/目标；preflight/status是只读发现，可无事先可信产品hash/PID生辰，默认不提权。明确对应动作授权且确需权限才有限RunAs。发现hash不是产品授权，不自动串联启停/采样。

真正启停、observe/capture与功能测试按R6批次及现行身份/授权/场景门。无UAC提示不等于高token，也不要求必须出现UAC提示。未知功能不签S0/S4/A-B，mock不签物理。ROG只集中补必要性能证据，不作为本机每个bug的前置。
