**English** | [中文](README.md)

# CCSwitch Usage Balance for TrafficMonitor

Display cc-switch provider balances and usage on the Windows taskbar. This is a fork of [Blackn-L/ccswitch-usage-plugin](https://github.com/Blackn-L/ccswitch-usage-plugin); the current extension version is **2.1-balance**.

By default, the first line shows the provider name and the second line shows its remaining balance, following the active **Codex** provider in cc-switch. The settings dialog can select another app, pin a provider, and customize the displayed fields. The dialog labels are in Chinese.

## Display items

| Item | Content |
| --- | --- |
| **CC余额** | Provider, app, remaining balance, used amount, total, remaining percentage, plan name, or combined fields; each line is configurable, including a single-line layout |
| **CC用量(余额版)** | The upstream daily cost/token item, with 7/30-day tooltip summaries |

Balance queries run in a background worker every 60 seconds by default. Unavailable fields display `--`. A failed query retains the last successful data for the same provider with a `*` marker; changing providers clears old data.

## Requirements and supported providers

- Windows 10 / 11 and x64 TrafficMonitor 1.86 or newer.
- [cc-switch](https://github.com/farion1231/cc-switch) installed and configured locally.
- An enabled, working JavaScript `request` / `extractor` usage query for the selected provider.
- **Native subscription and Coding Plan templates without a compatible query script are not supported for balance queries.**

Codex CLI, the Codex desktop app, and Node.js are not required. Separate item IDs allow this extension to coexist with the original plugin.

## Build from source

Install MinGW-w64 (GCC/G++ and windres), CMake 3.21+, and Ninja. Python 3 enables the full four-test suite; without it, the HTTP test is skipped.

```powershell
git clone --branch codex/provider-balance https://github.com/InRaysee/ccswitch-usage-plugin.git
cd ccswitch-usage-plugin
powershell -NoProfile -ExecutionPolicy Bypass -File .\script\build-balance.ps1
```

Alternatively, run `script\build.bat`. The script detects `C:\msys64\mingw64\bin` first, then uses PATH; pass `-CompilerRoot` to select another tool directory. The first build downloads pinned SQLite and QuickJS-ng dependencies.

The output is `bin/CCSwitchUsageBalance.dll`, which is not committed to this source repository. The existing `bin/CCSwitchUsage.dll` is the upstream plugin.

## Install or upgrade

1. Exit TrafficMonitor from its tray menu.
2. On first install, copy the generated `bin/CCSwitchUsageBalance.dll` and `bin/CCSwitchUsageBalance.ini` into TrafficMonitor's `plugins` directory.
3. On upgrade, back up and replace the balance DLL; **keep your existing INI**. Missing settings use defaults.
4. Restart TrafficMonitor and enable **CC余额** in its taskbar display settings. Enable **CC用量(余额版)** separately if desired.

For example, use `C:\TrafficMonitor\plugins`. The INI resides beside the DLL, so that directory must be writable to save settings.

## Native settings dialog

Open **Plugin Manager > cc-switch 用量与余额 > Options**.

| Tab | Settings |
| --- | --- |
| **数据源与刷新** (source and refresh) | Codex, Claude Code, Gemini, and other apps; follow or pin a provider; refresh interval; plan index; system/direct/manual HTTP proxy |
| **任务栏显示** (taskbar display) | Per-line fields, single-line mode, layout presets, provider alias, width, alignment, decimal precision, units, and field labels |
| **悬停信息与提醒** (tooltips and alerts) | Cost/token summaries, balance details, update time, errors, and low-balance highlighting |

The preview uses current data when available, otherwise clearly marked example data. **Apply** saves changes, **OK** saves and closes, and **Cancel** discards unapplied edits. **Restore defaults** changes the draft until Apply/OK.

These choices affect this plugin only; they do not switch the active cc-switch provider or modify its database. The original cost/token statistics keep their upstream aggregation scope and are not filtered by the balance provider selection.

See [README_BALANCE.md](README_BALANCE.md) for full configuration keys, data sources, troubleshooting, and test details (Chinese).

## Validation

```powershell
ctest --test-dir build --output-on-failure
```

Four tests cover script parsing/provider selection, settings persistence/display formatting, the native dialog, and loopback HTTP requests. Tests use synthetic data. The dialog test checks resource creation, app/provider changes, draft/apply behavior, validation, and preview rendering.

Credentials are read at runtime from local cc-switch configuration and used for the configured provider request. The default INI contains no credentials.

## License and credits

The upstream [Anti 996 License](LICENSE) and attribution are retained. `src/PluginInterface.h` is copyright zhongyang219. QuickJS-ng uses the MIT license in [LICENSE.quickjs.txt](LICENSE.quickjs.txt); SQLite is public domain.

Thanks to [Blackn-L](https://github.com/Blackn-L/ccswitch-usage-plugin), [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor), and [cc-switch](https://github.com/farion1231/cc-switch).
