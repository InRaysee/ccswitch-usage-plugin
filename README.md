[English](README_EN.md) | **中文**

# CCSwitch Usage Balance · TrafficMonitor 供应商余额版

在 Windows 任务栏显示 cc-switch 供应商的剩余余额和用量。基于 [Blackn-L/ccswitch-usage-plugin](https://github.com/Blackn-L/ccswitch-usage-plugin) 的 fork，当前增强版为 **2.1.1-balance**。

默认第一行显示供应商名称，第二行显示余额，跟随 cc-switch 当前启用的 **Codex** 供应商。可在设置窗口切换应用、固定供应商并自定义显示内容。

## 显示内容

| 显示项 | 内容 |
| --- | --- |
| **CC余额** | 供应商名称、应用、余额、已用、总额、剩余百分比、套餐名称或组合字段；每行可单独选择，也可使用单行模式 |
| **CC用量(余额版)** | 保留上游的今日花费、Token 和近 7/30 天悬停汇总 |

余额默认每 60 秒后台刷新。接口未提供且无法计算的数据会显示 `--`；同一供应商查询失败时保留上次成功数据并标记 `*`。切换监控供应商后清除旧数据。

## 环境与支持范围

- Windows 10 / 11，x64 TrafficMonitor 1.86 或更高版本。
- 本机已安装并配置 [cc-switch](https://github.com/farion1231/cc-switch)。
- 对应供应商已在 cc-switch 中启用并测试 JavaScript `request` / `extractor` 用量查询。
- **官方订阅、Coding Plan 等不带兼容查询脚本的内置模板不支持余额查询。**

插件直接读取 cc-switch 的本地配置，不依赖 Codex CLI、Codex 桌面版或 Node.js。使用独立显示项 ID，可与原插件共存。

## 从源码构建

需要 MinGW-w64（GCC/G++、windres）、CMake 3.21+ 和 Ninja。安装 Python 3 可运行完整四项测试，缺少 Python 时会跳过 HTTP 测试。

```powershell
git clone --branch codex/provider-balance https://github.com/InRaysee/ccswitch-usage-plugin.git
cd ccswitch-usage-plugin
powershell -NoProfile -ExecutionPolicy Bypass -File .\script\build-balance.ps1
```

也可运行 `script\build.bat`。脚本优先检测 `C:\msys64\mingw64\bin`，否则从 PATH 寻找构建工具；可用 `-CompilerRoot` 指定工具目录。首次构建会下载固定版本的 SQLite 和 QuickJS-ng。

增强版产物为 `bin/CCSwitchUsageBalance.dll`。该 DLL 不随源码提交；仓库中原有的 `bin/CCSwitchUsage.dll` 是上游插件。

## 安装与升级

1. 从 TrafficMonitor 托盘菜单退出程序。
2. 首次安装：将构建生成的 `bin/CCSwitchUsageBalance.dll` 和 `bin/CCSwitchUsageBalance.ini` 复制到 TrafficMonitor 的 `plugins` 目录。
3. 升级：备份后替换 `CCSwitchUsageBalance.dll`，**保留已有 INI**。新增配置自动使用默认值。
4. 重新启动 TrafficMonitor，在任务栏“显示设置”中勾选 **CC余额**；原有统计可另外勾选 **CC用量(余额版)**。

例如安装目录为 `C:\TrafficMonitor\plugins`。配置文件保存在 DLL 同目录，请确保该目录可写。

## 图形选项设置

进入 **TrafficMonitor > 插件管理 > cc-switch 用量与余额 > 选项**。

| 标签页 | 可配置内容 |
| --- | --- |
| **数据源与刷新** | Codex、Claude Code、Gemini 等应用；跟随当前或固定供应商；刷新间隔；套餐索引；系统代理、直连或手动 HTTP 代理 |
| **任务栏显示** | 两行显示字段、单行模式、快捷布局、名称别名、宽度、对齐、小数位数、单位和字段名称 |
| **悬停信息与提醒** | 花费/Token 统计、完整余额详情、更新时间、错误说明及低余额变色阈值 |

底部提供预览，切换尚未查询的监控对象时使用标明的示例数据。**应用**保存并生效，**确定**保存并关闭，**取消**放弃未应用的修改。**恢复默认**只修改窗口草稿，保存后生效。

设置仅改变本插件的监控对象，不会切换 cc-switch 的供应商或写入其数据库。原有花费/Token 统计保留上游的聚合口径，不随余额供应商筛选。

悬停提示按“当前供应商余额 -> 本机全部用量”分组。余额详情只显示接口提供的字段，已用与总额合并一行；花费和 Token 分别标注。悬停始终显示完整供应商名称及数值单位，不受任务栏别名、隐藏单位等样式影响。查询失败时只标注一次“上次成功值”。

想精简悬停内容，可在“悬停信息与提醒”中取消“本机全部花费 / Token 统计”；再取消“余额详情”后，只保留供应商、余额，以及仍勾选的时间/错误说明。

完整配置字段、数据来源、故障排查与测试说明见 [README_BALANCE.md](README_BALANCE.md)。

## 验证

```powershell
ctest --test-dir build --output-on-failure
```

已通过四项测试：余额脚本与供应商选择、配置保存和显示格式、原生设置对话框、仅本机 HTTP 请求。测试使用合成数据；对话框测试覆盖创建、联动、应用与草稿行为及预览绘制。

## 许可证与致谢

保留上游 [Anti 996 License](LICENSE) 及原作者署名。`src/PluginInterface.h` 的版权归 zhongyang219 所有。QuickJS-ng 的 MIT 许可见 [LICENSE.quickjs.txt](LICENSE.quickjs.txt)，SQLite 为公有领域。

感谢 [Blackn-L](https://github.com/Blackn-L/ccswitch-usage-plugin)、[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 和 [cc-switch](https://github.com/farion1231/cc-switch)。
