这是一个典型的 Qt Widgets + Windows API + CMake 的桌面增强类应用，目标是“接管/叠加 Windows 桌面”，自己绘制壁纸与图标层，并在此基础上做整理、分组、右键菜单、系统监控和插件扩展。主工程是可执行程序 LinDesk，平台能力被拆到一个独立静态库 OS，另外还有一个独立构建的 OCR 插件 DLL。

从实际代码看，它不是严格的 MVC，而是“UI 宿主 + 大型视图控制器 + 平台适配层 + 插件层”的架构。当前最核心的运行时中心其实是 src/LinDesktop.cpp 和 src/ListView.cpp。

启动与主链路

程序入口在 src/main.cpp。这里创建 QApplication 后，真正启动的是 LinDesktop，不是旧的 LinDesk 单例窗口。

主链路大致是：

main.cpp 启动 LinDesktop
LinDesktop 负责作为“桌面宿主窗口”嵌入真实桌面窗口
LinDesktop 创建 ListView 作为图标管理与交互核心
ListView 加载桌面项目、布局、右键菜单、持久化数据、文件系统监听、插件和全局热键
插件由 PluginManager 从运行目录下的 plugins/*.dll 动态加载
这说明整个系统是“壳层宿主 + 功能控制器集中在 ListView”的结构，而不是多个业务服务均匀分层。

模块分层

宿主层
src/LinDesktop.h / src/LinDesktop.cpp
职责很明确：

创建全屏透明窗口
通过 CSystemApi::FindRealDesktop() 把自己嵌进 Windows 桌面
绘制壁纸
监听壁纸变化并做过渡动画
管理选择框 SelectArea
这层更像“桌面容器”。

交互与业务核心层
src/ListView.h / src/ListView.cpp
这是当前项目最重的类，几乎承载了全部核心业务：

桌面图标加载与渲染
排序、自动布局、网格布局
多选、拖拽、拖放
右键菜单协调
持久化读写
Box 分组管理
系统图标显隐同步
目录变化监听
插件初始化
全局热键分发
从架构角度说，ListView 是“胖控制器/胖视图模型”，耦合度非常高，但也确实是系统的业务中心。

平台适配层
OS/CMakeLists.txt
OS/SystemApi/SystemApi.h
OS 被编译成静态库，再链接进主程序。它封装了大量 Windows 专属能力：

查找真实桌面窗口
读写桌面图标信息
Shell 右键菜单
文件复制/移动/删除
特殊目录与 CLSID 处理
缩略图和资源访问
这层本质上是一个 Win32/Shell API 封装库。名字叫 OS，但当前实现明显是 Windows-only，不是通用跨平台抽象。

监听与系统状态层
src/Monitor/DesktopInfoMonitor.h / src/Monitor/DesktopInfoMonitor.cpp
src/Monitor/DirectoryMonitor.h / src/Monitor/DirectoryMonitor.cpp
这层负责两类事情：

监听注册表中的“桌面系统图标显示状态”和壁纸变更
监听桌面目录文件变化
其中 DesktopInfoMonitor 的职责比较清晰，属于“系统状态源”。而目录监听目前有两套思路并存：

DirectoryMonitor：基于 SHChangeNotifyRegister
LinDirectoryMonitor：基于目录监控实现
实际在 ListView 中启用的是 LinDirectoryMonitor，DirectoryMonitor 更像半废弃方案。
插件层
src/Plugins/ILinPlugin.h
src/Plugins/PluginManager.cpp
Plugins/OCRModule/CMakeLists.txt
Plugins/OCRModule/OCR_Baidu.h
插件机制基于 Qt 插件系统：

主程序运行时扫描 applicationDir/plugins/*.dll
用 QPluginLoader 加载
通过 ILinPlugin / IOCRPlugin 接口识别能力
自动注册插件热键
按插件 ID 做触发分发
OCR 插件是独立 MODULE 动态库，编译后拷贝到主程序 plugins 目录。这块设计是目前架构里最清晰、最像标准扩展点的一层。

数据流

比较关键的数据流是这几条：

系统状态流
Windows 注册表 / 桌面状态
→ DesktopInfoMonitor
→ Qt signal
→ LinDesktop 或 ListView
→ UI 刷新

文件变化流
桌面目录变化
→ LinDirectoryMonitor
→ ListView::onFileSystemChanged
→ 重新加载或更新图标

用户交互流
鼠标/拖拽/右键/快捷键
→ ListView
→ 文件操作、布局更新、菜单操作、插件触发

插件触发流
全局热键
→ HotkeyManager
→ ListView::nativeEvent
→ PluginManager::triggerPlugin()
→ 插件 onTrigger()

持久化流
图标位置、Box、设置
↔ data.json / data.dat
↔ ListView