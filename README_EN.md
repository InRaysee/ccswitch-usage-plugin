# CCSwitch Usage Balance for TrafficMonitor

A provider balance extension of [Blackn-L/ccswitch-usage-plugin](https://github.com/Blackn-L/ccswitch-usage-plugin).

Adds a two-line **CC余额** item: provider name and remaining balance. Follows the current Codex provider in cc-switch by default. The original cost/token statistics remain available under a distinct item ID.

Build the plugin first, then copy the generated `bin/CCSwitchUsageBalance.dll` and `bin/CCSwitchUsageBalance.ini` into TrafficMonitor's `plugins` directory, restart, and enable the balance item. The balance DLL is a build output and is not committed to this source repository.

Build on Windows with MinGW-w64, CMake and Ninja using `script/build.bat` or `script/build-balance.ps1`. Python 3 enables the loopback HTTP test.

The balance feature reads cc-switch configuration in read-only mode and runs the configured JavaScript request/extractor using embedded QuickJS. Network requests execute in a background worker, defaulting to Windows system proxy and a 60-second interval. Zero/negative balances are supported. Errors preserve the last value only for the same provider; switching providers clears it immediately. Native subscription templates without a request/extractor script are not supported.

See [README_BALANCE.md](README_BALANCE.md) for configuration fields and full instructions. The INI and distribution contain no credentials; authentication is read from the local cc-switch database and sent only with its configured balance request.

Original licenses remain in LICENSE and source headers. QuickJS MIT license is in LICENSE.quickjs.txt; SQLite is public domain.
