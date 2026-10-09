# v10.1交付状态 · 本机软件门已接受

本机PS7 7.6.5 runner 144/144 PASS（原140＋新增4），实际进程exit0；collector重新验证41/41、758检查、exit0。原两条PS5失败的错误契约保持，新增模拟抛错的惰性normalizer与AST排序门、合法relative/abs/UNC/中文空格、NUL安全映射。审计副本故意错误排序得到142/144和预期exit1，正确抓住两条排序回退，不是发布源码失败。

本机PS5 5.1.26100.8875 parser7/7、exit0；默认Restricted下完整-File入口及惰性sentinel均exit1，完整runtime执行0 case、BLOCKED。没有Bypass/改策略/动态加载绕过，不等同PS7覆盖；ROG PS5完整复测尚未运行。

源码hash/长度与报告绑定，详见DELIVERY-STATUS.json和validation/runner-validation-v10.1-final-freeze-20261004.json。collector/worker/lifecycle/Fan源码未改；本轮无真实产品、token/native查询、提权、CPU采样或端口调用。包装的validate是独立文件/manifest烟测，不重跑完整套件时不冒称完整搬运回归。

R6产品任务已经启动且由另一AI执行，本工具不冻结它。原v10 ROG validate raw已收，collector/runner raw仍待交；本修正版在ROG的工具/产品/CPU回归均NOT_RUN，不能继承原版结果。本包不包含产品、不确认最终A/B选择、不代签产品日志或CPU改善。
