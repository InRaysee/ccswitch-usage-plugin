#pragma once
#include "PluginInterface.h"
#include <sqlite3.h>
#include <string>
#include <utility>
#include <vector>

namespace balance {
enum class Field { None, Provider, AppProvider, Remaining, Used, Total, Percent, Plan, UsedTotal, BalancePercent };
struct Config {
    std::string app = "codex", providerId, proxyMode = "system", proxyServer;
    unsigned refreshSeconds = 60, width = 150, planIndex = 0, decimals = 2, alignment = 0;
    Field line1 = Field::Provider, line2 = Field::Remaining;
    std::string displayName;
    bool showUnit = true, showLabels = false, warnEnabled = false;
    double warnBelow = 10;
    bool tooltipUsage = true, tooltipDetails = true, tooltipTime = true, tooltipErrors = true;
};
struct Provider {
    std::string id, app, name, meta, settings;
    std::string code, apiKey, baseUrl, accessToken, userId, templateType;
    unsigned timeoutSeconds = 10;
};
struct ProviderChoice {
    std::string id, app, name;
    bool enabled = false, hasScript = false;
};
struct Request {
    std::string url, method = "GET", body;
    std::vector<std::pair<std::string, std::string>> headers;
};
struct Value {
    double remaining = 0;
    std::string unit, plan;
    double used = 0, total = 0;
    bool hasUsed = false, hasTotal = false;
};
struct Snapshot {
    std::string providerId, name, app;
    Value value;
    bool hasValue = false, loading = false;
    std::wstring error;
    unsigned long long updatedAt = 0;
};

std::wstring Utf8(const std::string& text);
std::string Utf8(const std::wstring& text);
bool ListProviders(sqlite3* db, std::vector<ProviderChoice>& choices, std::wstring& error);
bool LoadProvider(sqlite3* db, const Config& config, const std::string& settingsJson,
                  Provider& provider, std::wstring& error);
bool BuildRequest(const Provider& provider, Request& request, std::wstring& error);
bool ExtractValue(const Provider& provider, const std::string& response, unsigned planIndex,
                  Value& value, std::wstring& error);
bool Fetch(const Provider& provider, const Config& config, Value& value, std::wstring& error);
std::wstring FormatValue(const Value& value);
const char* FieldKey(Field field);
Field ParseField(const std::string& key, Field fallback);
std::wstring AppName(const std::string& app);
std::wstring FormatField(Field field, const Snapshot& snapshot, const Config& config);
std::vector<std::wstring> DisplayLines(const Snapshot& snapshot, const Config& config);
bool IsWarning(const Snapshot& snapshot, const Config& config);
std::wstring ConfigPath();
Config ReadConfigFile(const std::wstring& path);
bool SaveConfigFile(const std::wstring& path, const Config& config, std::wstring& error);
Config GetDisplayConfig();
void DrawSnapshot(void* dc, int x, int y, int width, int height, bool dark,
                  const Snapshot& snapshot, const Config& config);
void Refresh(const std::string& dbPath);
void ForceRefresh(const std::string& dbPath);
Snapshot GetSnapshot();
std::wstring Tooltip();
IPluginItem* Item();
ITMPlugin::OptionReturn ShowOptions(void* parent, const std::string& dbPath);
} // namespace balance
