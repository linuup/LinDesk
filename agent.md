# LinDesk AI 代理约束（agent.md）

## 1. 工程硬约束

### 1.1 源文件编码
- 所有源文件（`.cpp / .h / .hpp / .cmake / .json / .md`）一律 **UTF-8（无 BOM）**；
- 禁止引入或保留乱码注释；发现历史乱码注释直接清理为正确 UTF-8 或删除；
- 新增文件必须显式以 UTF-8 写入；工具链输出（脚本重定向、生成文件）不得经过系统代码页转码；
- 可维护 `scripts/` 下的编码检查脚本，作为提交前兜底（不允许有 BOM 或非 UTF-8 字节的文件进入提交）。


### 1.4 日志与崩溃处理（执行 `docs/LinDesk_Logging_Plan.html`）
- 日志库统一 **spdlog**（header-only），经 `qInstallMessageHandler` 转发；**禁止**引入 glog / Boost.Log / log4cplus；
- 崩溃捕获：主进程用 `SetUnhandledExceptionFilter` + `MiniDumpWriteDump`，CEF 子进程用 CEF 自带 crash dumps；**禁止**完整集成 breakpad / crashpad；
- 多进程日志**分文件**（desktop.log / cef.log / updater.log …），统一行格式，禁止多进程共享同一日志文件句柄；
- 日志落盘目录统一 `%LOCALAPPDATA%/LinDesk/logs/`，轮转按大小+保留份数，**禁止**"超限直接删文件"。

### 1.5 日志书写规范（AI 加日志的行为要求）
写代码时把"加日志"当作功能的一部分，而不是事后补丁：

- **在合适的地方加**：新功能或改动至少覆盖三处——入口（这一步在做什么）、关键中间结果（参数/数据到达哪一步）、成功/失败结论（最终结果如何）。以下场景**必须**有日志：状态变化、错误分支、耗时操作、异步回调、跨进程/子程序交互、组件生命周期（加载/崩溃/自动重启）、设置项变更生效。
- **级别怎么选**：失败与异常用 `WARN`/`CRIT`；值得留痕的正常状态变化用 `INFO`；过程细节用 `DEBUG`（默认不输出，命令行 `-debug` 开启）。
- **怎么写**：日志要让人（或 AI 排查时）**不看代码也能明白发生了什么**——包含"谁在做什么 + 关键参数 + 结果"；错误日志必须带可定位信息（模块/组件/文件/操作名 + 失败原因），禁止只写 `failed` / `error` 这类没有上下文的短语。
- **什么时候不加**：热循环与高频路径（每帧绘制、逐图标遍历、批量行处理）不加 `INFO` 及以上日志，需要时用 `DEBUG`；同一事件不重复刷屏；release 构建不留临时调试输出。
- **日志落点**：一律走 `LogManager`（`qDebug`/`qWarning`/`qCritical`），禁止 `std::cout` / `printf` 裸输出；组件 JS 日志用 `console`（自动收口进主日志并带 `[widget:xx]` 来源）；CEF 自身日志归 `cef.log`，不在主进程代码里重复打印。
