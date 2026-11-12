# cc-switch 供应商余额版 TrafficMonitor 插件

基于 Blackn-L/ccswitch-usage-plugin（上游提交 7770bf69cb2ffdbf751fa6036cd64ee46c5be255）修改。余额功能分支为 `codex/provider-balance`。

新增 **CC余额** 显示项：第一行显示供应商名称，第二行显示剩余余额。默认跟随 cc-switch 当前启用的 **Codex** 供应商。原有金额/Token 项保留为 **CC用量(余额版)**，可按需勾选。

## 安装

1. 按下方“构建与验证”生成 `bin/CCSwitchUsageBalance.dll`，再将其和 `bin/CCSwitchUsageBalance.ini` 放到 TrafficMonitor 的 `plugins` 目录。增强版 DLL 不随源码提交。
2. 退出后重新启动 TrafficMonitor。
3. 在任务栏显示设置中勾选 **CC余额**。本插件使用独立项目 ID，可以与原 CCSwitchUsage.dll 及 CodexUsage.dll 共存。
4. 首次查询通常需要几秒。任务栏上方显示供应商名，下方显示如 `$123.45` 的余额；悬停显示完整名称、套餐、最后成功更新时间和错误。

已验证 Windows x64 构建，适合 x64 TrafficMonitor 1.86 或更高版本。

## 数据来源与支持范围

- 只读读取 cc-switch 的 `providers` 表和设备本地 `settings.json`；设备本地当前供应商选择优先于数据库 `is_current` 标志。
- 复用供应商启用的 `meta.usage_script.code`：`request` 描述 HTTP 请求，`extractor(response)` 返回余额对象或套餐数组。
- 支持现代 JavaScript，包括可选链 `?.`、空值合并 `??` 和箭头函数；支持 `{{apiKey}}`、`{{baseUrl}}`、`{{accessToken}}`、`{{userId}}` 占位符以及独立查询凭据。
- 支持提取器返回的数字 `remaining`，或用数字 `total - used` 求余额；支持 `unit`、`planName`、`isValid`。余额为 0 或负数时照常显示；缺少字段不会被显示成 0。
- 原生官方订阅、Coding Plan 等不带 request/extractor 的内置模板不在本版本支持范围。此时会提示先配置兼容的用量脚本。原来的 CodexUsage 插件仍可显示官方订阅额度。
- 余额缓存仅在进程内保存。查询会发往已配置的供应商接口，不会把密钥写入 INI、日志或发布包。
- 查询使用独立后台线程；网络请求不会阻塞任务栏绘制。检测到供应商或查询配置变化后立即清除旧余额；旧请求即使随后成功，也不会覆盖新供应商。
- 同一供应商短暂查询失败时保留上一次成功的余额，追加 `*`，悬停说明原因及旧值更新时间。

## 配置

从插件管理打开本插件“选项”会打开同目录 INI。配置保存后约 2 秒内重新读取。

```ini
[Balance]
app_type=codex
provider_id=
refresh_seconds=60
proxy_mode=system
proxy_server=
width=150
plan_index=0
```

- `app_type`：默认 `codex`。也可选择 `claude`、`gemini` 等有兼容脚本的应用。
- `provider_id`：留空跟随当前供应商；填入数据库中的供应商 ID 可固定某个供应商，不按名称模糊匹配。
- `refresh_seconds`：余额查询间隔，15 到 3600 秒；默认 60 秒。独立于 cc-switch 卡片的查询间隔。
- `proxy_mode`：`system`（默认）使用 Windows 当前用户系统代理；`direct` 直连；`manual` 使用 `proxy_server` 指定的 HTTP 代理。
- `proxy_server`：例如 `127.0.0.1:7890`。只有 manual 模式使用，不支持 SOCKS 端点。
- `width`：任务栏显示宽度，按 96 DPI 配置，范围 80 到 400。长名称会省略，悬停可看完整名称。
- `plan_index`：提取器返回数组时选择的套餐，从 0 开始。不把不同货币的多套餐余额强行相加。

如果提示 `Enable usage query`，请在 cc-switch 对应供应商卡片启用并测试用量查询。如果提示 `HTTP 401/403`，检查查询专用凭据及供应商权限。如果提示 WinHTTP 错误，检查该电脑的代理，不能假定另一台电脑也使用 7890 端口。

## 构建与验证

需要 Windows MinGW-w64（GCC/G++）、CMake、Ninja，测试还需要 Python 3。运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\script\build-balance.ps1
```

脚本下载 SQLite 3.53.4 和固定提交的 QuickJS-ng，输出 `bin/CCSwitchUsageBalance.dll`。不依赖 Node.js、Codex CLI 或 Codex 桌面版。

```powershell
ctest --test-dir build --output-on-failure
.\build\plugin_probe.exe .\bin\CCSwitchUsageBalance.dll
```

第一条使用合成数据及仅监听本机的 HTTP 服务测试，不读取真实密钥。第二条是显式的本机联调：实际加载 DLL、读取当前供应商，并查询其余额接口，仅打印名称和余额。

## 许可证

原插件和接口的许可保留在 `LICENSE` 与源码中。QuickJS-ng 的 MIT 许可见 `LICENSE.quickjs.txt`。SQLite 为公有领域。余额扩展沿用原插件许可。
