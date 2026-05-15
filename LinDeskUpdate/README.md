# LinDeskUpdate 使用说明

这个子项目负责执行 LinDesk 的安装包更新。主程序只负责检查更新、下载安装包、校验 SHA256，然后启动 `LinDeskUpdate.exe`。真正覆盖安装由 `LinDeskUpdate.exe` 调用 Inno Setup 安装包完成。

## 一、整体更新流程

1. 修改 LinDesk 主项目版本号。

   版本号在主项目根目录的 `CMakeLists.txt` 中维护：

   ```cmake
   set(LINDESK_VERSION_MAJOR 1)
   set(LINDESK_VERSION_MINOR 0)
   set(LINDESK_VERSION_PATCH 3)
   set(LINDESK_VERSION_TAG "Stable")
   ```

2. 使用 Release 配置构建 LinDesk。

   构建完成后，目标目录应为：

   ```text
   D:\Desktop\LinDesk\build\build_64\Release
   ```

   这个目录中需要包含：

   ```text
   LinDesk.exe
   LinDeskUpdate.exe
   resources
   translations
   Qt 运行库
   OpenSSL DLL
   其他运行依赖
   ```

3. 使用 `installer/package.iss` 生成完整安装包。

   安装包命名规则：

   ```text
   LinDesk_Setup_1.0.3.exe
   ```

4. 计算安装包 SHA256。

   PowerShell 命令：

   ```powershell
   Get-FileHash .\LinDesk_Setup_1.0.3.exe -Algorithm SHA256
   ```

   输出中的 `Hash` 字段填入 `update.json` 的 `sha256`。

5. 创建 `update.json`。

   `update.json` 必须和安装包版本一致，并且 `url` 必须指向 GitHub Release 中的安装包下载地址。

6. 打开更新仓库：

   ```text
   https://github.com/linuup/LinDesk_Update
   ```

7. 创建新的 GitHub Release。

   推荐 Release tag 使用：

   ```text
   v1.0.3
   ```

   Release title 可以使用：

   ```text
   LinDesk 1.0.3
   ```

8. 上传两个 Release 资产文件。

   第一个文件：

   ```text
   LinDesk_Setup_1.0.3.exe
   ```

   第二个文件：

   ```text
   update.json
   ```

9. 发布 Release。

10. 打开 LinDesk 设置页，进入 About 页面，点击 `Check for Updates`。

11. 主程序会请求最新 Release 中的：

   ```text
   https://github.com/linuup/LinDesk_Update/releases/latest/download/update.json
   ```

12. 如果远程版本大于本地版本，设置页会显示新版本和更新说明。

13. 点击下载后，主程序会把安装包下载到：

   ```text
   %LOCALAPPDATA%\LinDesk\Updates
   ```

14. 下载完成后，主程序会计算安装包 SHA256，并和 `update.json` 里的 `sha256` 对比。

15. 校验通过后，安装按钮可用。

16. 点击安装后，主程序启动：

   ```text
   LinDeskUpdate.exe
   ```

   参数格式：

   ```text
   LinDeskUpdate.exe --installer "<安装包路径>" --app "<LinDesk.exe路径>" --process "LinDesk.exe" --language zh --restart true
   ```

17. 安装目录中的 `LinDeskUpdate.exe` 会先复制自身到：

   ```text
   %LOCALAPPDATA%\LinDesk\UpdaterRuntime
   ```

   然后启动这个临时副本并退出，避免安装器提示 `LinDeskUpdate.exe` 正在运行。

18. 临时副本会关闭主程序，执行安装包：

   ```text
   /NORESTART /LANG=zh /LINDESKUPDATE=1
   ```

   安装器会正常显示出来，并根据用户在 LinDesk 中配置的语言自动选择安装器语言。更新模式会跳过创建快捷方式页面和安装完成后启动页面。

19. 安装器启动后，临时副本会等待安装器结束。安装成功后会清理更新缓存，包括下载目录里的安装包、空的 `Updates` 目录、旧的临时更新程序副本，并安排当前临时副本退出后自删除。后续启动由可见安装器流程处理。

20. 更新程序日志写入：

   ```text
   %LOCALAPPDATA%\LinDesk\Logs\updater.log
   ```

## 二、update.json 字段说明

`update.json` 固定包含以下字段：

```json
{
  "version": "1.0.3",
  "channel": "stable",
  "url": "https://github.com/linuup/LinDesk_Update/releases/download/v1.0.3/LinDesk_Setup_1.0.3.exe",
  "sha256": "把安装包的 SHA256 填到这里",
  "notes": "更新说明文本",
  "publishedAt": "2026-05-14T00:00:00Z",
  "minSupportedVersion": "1.0.0"
}
```

字段含义：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `version` | 是 | 远程版本号。必须使用数字语义版本，例如 `1.0.3`。客户端会按数字比较，`1.0.10` 大于 `1.0.3`。 |
| `channel` | 是 | 更新通道。第一版只支持 `stable`。 |
| `url` | 是 | 安装包完整下载地址。必须是 GitHub Release asset 的直接下载地址。 |
| `sha256` | 是 | 安装包 SHA256。客户端下载完成后会校验，不一致会删除安装包并提示失败。 |
| `notes` | 是 | 更新说明。会显示在设置页。 |
| `publishedAt` | 是 | 发布时间。推荐使用 UTC ISO 8601 格式。 |
| `minSupportedVersion` | 是 | 最低支持升级的本地版本。第一版先作为元数据保留。 |

## 三、update.json 完整示例

假设当前要发布 `1.0.3`，安装包文件名为：

```text
LinDesk_Setup_1.0.3.exe
```

GitHub Release tag 为：

```text
v1.0.3
```

PowerShell 计算出的 SHA256 为：

```text
2F6B4B0F6F8E0B1F0D4A7C9E28C0A1B5C7D8E9F00112233445566778899AABBCC
```

则 `update.json` 可以写成：

```json
{
  "version": "1.0.3",
  "channel": "stable",
  "url": "https://github.com/linuup/LinDesk_Update/releases/download/v1.0.3/LinDesk_Setup_1.0.3.exe",
  "sha256": "2f6b4b0f6f8e0b1f0d4a7c9e28c0a1b5c7d8e9f00112233445566778899aabbcc",
  "notes": "修复已知问题，优化桌面组件显示效果，提升稳定性。",
  "publishedAt": "2026-05-14T00:00:00Z",
  "minSupportedVersion": "1.0.0"
}
```

说明：

1. `sha256` 可以使用大写或小写，客户端会统一转成小写比较。
2. `url` 里的版本号和文件名必须和 Release 资产一致。
3. `version` 必须大于本地 `LINDESK_VERSION_STRING`，客户端才会提示发现新版本。
4. `channel` 目前必须填 `stable`。

## 四、发布时建议检查清单

1. `CMakeLists.txt` 中的版本号已经更新。
2. `build\build_64\Release` 中存在 `LinDeskUpdate.exe`。
3. 安装包可以在干净目录中完成安装。
4. 安装包文件名符合 `LinDesk_Setup_版本号.exe`。
5. `update.json` 中的 `version` 和安装包版本一致。
6. `update.json` 中的 `url` 可以在浏览器中直接下载安装包。
7. `update.json` 中的 `sha256` 和安装包实际 SHA256 一致。
8. GitHub Release 中同时上传了安装包和 `update.json`。
9. 设置页手动检查更新可以看到新版本。
10. 下载完成后 SHA256 校验通过。
11. 点击安装后可以启动 `LinDeskUpdate.exe`。

## 五、常见问题

### 检查更新提示已经是最新版本

检查 `update.json` 的 `version` 是否大于本地版本。版本比较按数字语义执行，例如：

```text
1.0.10 > 1.0.3
```

### 下载后提示 SHA256 校验失败

重新计算安装包 SHA256，并更新 `update.json`。如果发布后重新上传了安装包，SHA256 也必须重新填写。

### 点击安装后没有反应

确认安装目录中存在：

```text
LinDeskUpdate.exe
```

并查看日志：

```text
%LOCALAPPDATA%\LinDesk\Logs\updater.log
```

### Release 中 update.json 改了，但是客户端还是旧内容

GitHub Release asset 有时会被缓存。建议重新上传 `update.json` 后等待一会儿再测试，或者创建新的 Release tag。
