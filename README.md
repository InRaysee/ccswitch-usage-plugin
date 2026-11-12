[English](README_EN.md) | **中文**

# CCSwitch Usage Balance · TrafficMonitor 供应商余额版

基于 [Blackn-L/ccswitch-usage-plugin](https://github.com/Blackn-L/ccswitch-usage-plugin) 修改。

- **CC余额**：上行供应商名称，下行剩余余额，默认跟随 cc-switch 当前 Codex 供应商。
- **CC用量(余额版)**：保留上游的今日花费、Token 和近 7/30 天悬停汇总。
- 余额后台查询每 60 秒一次；复用 cc-switch 已配置的 JavaScript `request` / `extractor` 用量脚本。
- 支持系统/手动代理、固定供应商、多套餐选择、错误提示和上次成功更新时间。

安装：先按下方命令构建，再把生成的 `bin/CCSwitchUsageBalance.dll` 和同名 INI 放入 TrafficMonitor 的 `plugins`，重启并勾选 **CC余额**。增强版 DLL 是构建产物，不随源码提交。

构建：`powershell -NoProfile -ExecutionPolicy Bypass -File .\script\build-balance.ps1`，或运行 `script\build.bat`。

**完整安装、支持范围、配置、构建和验证见 [README_BALANCE.md](README_BALANCE.md)。** 本版本支持配置了查询脚本的供应商余额；原生官方订阅模板不在支持范围。

原插件与接口许可证见 [LICENSE](LICENSE)。QuickJS-ng 许可证见 [LICENSE.quickjs.txt](LICENSE.quickjs.txt)。
