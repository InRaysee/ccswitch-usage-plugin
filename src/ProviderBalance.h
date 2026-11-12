#pragma once
#include "PluginInterface.h"
#include <sqlite3.h>
#include <string>
#include <utility>
#include <vector>

namespace balance {
struct Config {
    std::string app = "codex";
    std::string providerId;
    std::string proxyMode = "system";
    std::string proxyServer;
    unsigned refreshSeconds = 60;
    unsigned width = 150;
    unsigned planIndex = 0;
};
struct Provider {
    std::string id, app, name, meta, settings;
    std::string code, apiKey, baseUrl, accessToken, userId, templateType;
    unsigned timeoutSeconds = 10;
};
struct Request {
    std::string url, method = "GET", body;
    std::vector<std::pair<std::string, std::string>> headers;
};
struct Value {
    double remaining = 0;
    std::string unit, plan;
};
struct Snapshot {
    std::string providerId, name;
    Value value;
    bool hasValue = false;
    bool loading = false;
    std::wstring error;
    unsigned long long updatedAt = 0;
};

std::wstring Utf8(const std::string& text);
std::string Utf8(const std::wstring& text);
bool LoadProvider(sqlite3* db, const Config& config, const std::string& settingsJson,
                  Provider& provider, std::wstring& error);
bool BuildRequest(const Provider& provider, Request& request, std::wstring& error);
bool ExtractValue(const Provider& provider, const std::string& response, unsigned planIndex,
                  Value& value, std::wstring& error);
bool Fetch(const Provider& provider, const Config& config, Value& value, std::wstring& error);
std::wstring FormatValue(const Value& value);
void Refresh(const std::string& dbPath);
Snapshot GetSnapshot();
std::wstring Tooltip();
IPluginItem* Item();
ITMPlugin::OptionReturn ShowOptions(void* parent);
} // namespace balance
