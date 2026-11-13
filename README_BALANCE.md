# cc-switch 供应商余额版 TrafficMonitor 插件（2.1.1-balance）

基于 Blackn-L/ccswitch-usage-plugin（上游提交 7770bf69cb2ffdbf751fa6036cd64ee46c5be255）修改。余额功能分支为 `codex/provider-balance`。

新增 **CC余额** 显示项：默认第一行显示供应商名称，第二行显示剩余余额；2.1 版可通过图形设置自定义两行内容或使用单行模式。默认跟随 cc-switch 当前启用的 **Codex** 供应商。原有金额/Token 项保留为 **CC用量(余额版)**，可按需勾选；它沿用上游聚合范围，不按余额监控对象筛选。

## 安装

1. 按下方“构建与验证”生成 `bin/CCSwitchUsageBalance.dll`，再将其和 `bin/CCSwitchUsageBalance.ini` 放到 TrafficMonitor 的 `plugins` 目录。增强版 DLL 不随源码提交。
2. 退出后重新启动 TrafficMonitor。升级时备份并替换 DLL，保留原有 INI；缺失的新配置使用默认值。
3. 在任务栏显示设置中勾选 **CC余额**。本插件使用独立项目 ID，可以与原 CCSwitchUsage.dll 及 CodexUsage.dll 共存。
4. 首次查询通常需要几秒。任务栏上方显示供应商名，下方显示如 `$123.45` 的余额；悬停显示完整名称、套餐、最后成功更新时间和错误。

已验证 Windows x64 构建，适合 x64 TrafficMonitor 1.86 或更高版本。

## 悬停排版（2.1.1）

当前供应商余额置顶，本机全部应用/供应商的用量汇总放在独立段落。以下为示例数据：

```text
[供应商余额]
Codex / 示例供应商
余额: $75.00
已用 / 总额: $25.00 / $100.00
剩余比例: 75.00%
套餐: Plan A
上次成功: 2026-11-13 10:00:00

[本机用量: 所有应用 / 供应商]
今日: 花费 $1.20 | Token 1.5M
近 7 天: 花费 $5.60 | Token 7.0M
近 30 天: 花费 $20.00 | Token 25.0M
```

- 余额详情跳过缺失的已用、总额、比例和套餐；实际零值仍显示。任务栏选中了缺失字段时仍显示 `--`。
- 悬停始终使用完整供应商名称、字段名与数值单位，不受任务栏别名或隐藏单位设置影响。
- 在“悬停信息与提醒”中关闭“本机全部花费 / Token 统计”可隐藏第二组；关闭“余额详情”后只显示供应商和余额，以及仍启用的时间、错误与提醒。
- 查询失败时，余额后只显示一次“上次成功值”，不在各字段重复追加星号。关闭“查询错误详情”会隐藏错误原文，仍保留旧值标记；任务栏的 `*` 标记不变。
- 供应商名称、套餐和错误中的换行会合并为空格，避免打乱提示排版。

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

在 TrafficMonitor 的“插件管理”中选中“cc-switch 用量与余额”，点击“选项”即可打开原生设置窗口：

- **数据源与刷新**：选择 Codex、Claude Code、Gemini 等应用；从数据库列出的供应商中固定选择，或跟随该应用当前供应商。可设置刷新间隔、套餐索引、系统/直连/手动 HTTP 代理。
- **任务栏显示**：独立选择两行内容（供应商、应用与供应商、余额、已用、总额、剩余百分比、套餐、组合字段）；将一行设为“不显示”可用单行模式。可设置快捷布局、名称别名、宽度、对齐、小数位数、单位和字段名称。
- **悬停信息与提醒**：控制花费/Token 统计、完整余额详情、更新时间及错误说明；可启用低余额变色提醒。
- 底部预览和任务栏使用相同绘制逻辑。当前监控对象的数据可用时使用真实数据，其他选择显示明确标注的示例数据，不会在保存前查询其他供应商。
- “应用”保存并生效；“确定”保存并关闭；“取消”放弃本次未应用的修改。已点击“应用”的设置会保留。“恢复默认”只更改窗口草稿，保存后生效。
- 不修改 cc-switch 的当前供应商或数据库。未启用查询、无脚本及已删除的供应商会明确标记，不会悄悄换成其他账户。仅监控配置了兼容 request/extractor 脚本的供应商。

配置仍保存在插件同目录 INI；窗口也提供“打开 INI”入口。手动保存后约 2 秒内重新读取。旧 INI 自动兼容缺省值。

```ini
[Balance]
app_type=codex
provider_id=
refresh_seconds=60
proxy_mode=system
proxy_server=
width=150
plan_index=0
line1=provider
line2=remaining
decimals=2
alignment=0
display_name=
show_unit=1
show_labels=0
warn_enabled=0
warn_below=10
tooltip_usage=1
tooltip_details=1
tooltip_time=1
tooltip_errors=1
```

- `app_type`：默认 `codex`。也可选择 `claude`、`gemini` 等有兼容脚本的应用。
- `provider_id`：留空跟随当前供应商；填入数据库中的供应商 ID 可固定某个供应商，不按名称模糊匹配。
- `refresh_seconds`：余额查询间隔，15 到 3600 秒；默认 60 秒。独立于 cc-switch 卡片的查询间隔。
- `proxy_mode`：`system`（默认）使用 Windows 当前用户系统代理；`direct` 直连；`manual` 使用 `proxy_server` 指定的 HTTP 代理。
- `proxy_server`：例如 `127.0.0.1:7890`。只有 manual 模式使用，不支持 SOCKS 端点。
- `width`：任务栏显示宽度，按 96 DPI 配置，范围 80 到 600。长名称会省略，悬停可看完整名称。
- `plan_index`：提取器返回数组时选择的套餐，从 0 开始。不把不同货币的多套餐余额强行相加。
- `line1` / `line2`：`none`、`provider`、`app_provider`、`remaining`、`used`、`total`、`percent`、`plan`、`used_total`、`balance_percent`。至少一行需显示内容。
- `decimals`：0 到 6；`alignment`：0 左对齐、1 居中、2 右对齐；`display_name`：可选供应商显示别名。
- `show_unit`、`show_labels`：是否显示单位或字段名；`warn_enabled` / `warn_below`：低余额变色开关和阈值。
- `tooltip_usage`、`tooltip_details`、`tooltip_time`、`tooltip_errors`：分别控制全部用量汇总、有效余额详情、成功时间、错误详情，1 启用、0 关闭；供应商名称和余额始终保留。

已用或总额仅在接口返回数值，或可由余额和另一数值计算时提供；缺失字段显示 `--`。总额大于 0 时才计算剩余百分比。不把多套餐、不同货币余额相加。

如果提示 `Enable usage query`，请在 cc-switch 对应供应商卡片启用并测试用量查询。如果提示 `HTTP 401/403`，检查查询专用凭据及供应商权限。如果提示 WinHTTP 错误，检查该电脑的代理，不能假定另一台电脑也使用 7890 端口。

## 常见问题

| 问题 | 处理方式 |
| --- | --- |
| “选项”仍打开 INI，或看不到新设置窗口 | 确认替换的是 `CCSwitchUsageBalance.dll`，完全退出 TrafficMonitor 后再重开；插件版本应为 `2.1.1-balance` |
| 新增供应商未出现在列表 | 点击“重新读取列表”，并检查所选应用类型与 cc-switch 数据目录 |
| 提示 `Enable usage query` 或无兼容脚本 | 在 cc-switch 中为该供应商启用、测试兼容 `request` / `extractor` 查询；官方订阅内置模板不适用 |
| `HTTP 401/403` | 检查供应商查询凭据及接口权限 |
| WinHTTP 错误 | 检查本机系统代理或在设置中指定正确的 HTTP 代理；端口取决于本机配置 |
| 已用、总额或百分比显示 `--` | 查询结果缺少相应数值，或总额不大于 0；更改显示字段不会额外产生接口未提供的数据 |
| 显示 `*` | 当前供应商的最新查询失败，保留的是上一次成功数据；悬停查看错误与更新时间 |
| 保存失败 | 确认 INI 未设为只读且插件目录可写；保存失败不会替换原配置 |

## 构建与验证

需要 Windows MinGW-w64（GCC/G++、windres）、CMake 3.21+、Ninja；Python 3 用于 HTTP 测试。未安装 Python 时运行其余三项测试。运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\script\build-balance.ps1
```

脚本下载 SQLite 3.53.4 和固定提交的 QuickJS-ng，输出 `bin/CCSwitchUsageBalance.dll`。不依赖 Node.js、Codex CLI 或 Codex 桌面版。

```powershell
ctest --test-dir build --output-on-failure
.\build\plugin_probe.exe .\bin\CCSwitchUsageBalance.dll
```

第一条运行 `balance_core`、`balance_settings`、`balance_options`、`balance_http` 四项测试，使用合成数据及仅监听本机的 HTTP 服务，覆盖余额解析、配置持久化、显示格式和原生对话框交互，不读取真实密钥。第二条是显式的本机联调：实际加载 DLL、读取当前供应商，并查询其余额接口，仅打印名称和余额。

## 许可证

原插件和接口的许可保留在 `LICENSE` 与源码中。QuickJS-ng 的 MIT 许可见 `LICENSE.quickjs.txt`。SQLite 为公有领域。余额扩展沿用原插件许可。
