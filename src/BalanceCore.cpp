#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0603
#endif
#include "ProviderBalance.h"
#include <windows.h>
#include <winhttp.h>
#include <quickjs.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <memory>
#include <regex>

namespace balance {
std::wstring Utf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"?";
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), out.data(), n);
    return out;
}
std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}
namespace {
std::string Text(sqlite3_stmt* stmt, int col) {
    const auto* p = sqlite3_column_text(stmt, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}
struct Js {
    JSRuntime* runtime = nullptr;
    JSContext* ctx = nullptr;
    ULONGLONG deadline = GetTickCount64() + 1500;
    Js() {
        runtime = JS_NewRuntime();
        if (!runtime) return;
        JS_SetMemoryLimit(runtime, 16 * 1024 * 1024);
        JS_SetMaxStackSize(runtime, 256 * 1024);
        JS_SetInterruptHandler(runtime, [](JSContext*, void* p) -> int {
            return GetTickCount64() >= static_cast<Js*>(p)->deadline;
        }, this);
        ctx = JS_NewContext(runtime);
    }
    ~Js() {
        if (ctx) JS_FreeContext(ctx);
        if (runtime) JS_FreeRuntime(runtime);
    }
    bool Eval(const std::string& code, std::string& result) {
        if (!ctx) return false;
        JSValue v = JS_Eval(ctx, code.data(), code.size(), "usage-script", JS_EVAL_TYPE_GLOBAL);
        bool ok = !JS_IsException(v) && JS_IsString(v);
        if (ok) {
            const char* text = JS_ToCString(ctx, v);
            ok = text != nullptr;
            if (text) { result = text; JS_FreeCString(ctx, text); }
        }
        JS_FreeValue(ctx, v);
        return ok;
    }
};
std::string String(JSContext* ctx, JSValueConst obj, const char* key) {
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    const char* s = JS_IsString(v) ? JS_ToCString(ctx, v) : nullptr;
    std::string out = s ? s : "";
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, v);
    return out;
}
bool Number(JSContext* ctx, JSValueConst obj, const char* key, double& number) {
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    bool ok = JS_IsNumber(v) && JS_ToFloat64(ctx, &number, v) == 0 && std::isfinite(number);
    JS_FreeValue(ctx, v);
    return ok;
}
std::string Escape(const std::string& text) {
    std::string out;
    const char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c < 32 || c == '\\' || c == '"' || c == '\'') {
            out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
        } else out += char(c);
    }
    return out;
}
void Replace(std::string& text, const std::string& key, const std::string& value) {
    size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string::npos) {
        text.replace(pos, key.size(), value); pos += value.size();
    }
}
std::string Script(const Provider& p) {
    std::string code = p.code;
    Replace(code, "{{apiKey}}", Escape(p.apiKey));
    Replace(code, "{{baseUrl}}", Escape(p.baseUrl));
    Replace(code, "{{accessToken}}", Escape(p.accessToken));
    Replace(code, "{{userId}}", Escape(p.userId));
    return "globalThis.__usageConfig = " + code + "\n;";
}
std::string Lower(std::string text) {
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
struct Url {
    std::wstring host, path;
    INTERNET_PORT port = 0;
    bool secure = false;
};
bool ParseUrl(const std::string& input, Url& out) {
    std::wstring text = Utf8(input);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
    if (!WinHttpCrackUrl(text.c_str(), 0, 0, &parts) || !parts.dwHostNameLength ||
        parts.dwUserNameLength || parts.dwPasswordLength ||
        (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP)) return false;
    out.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    out.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (out.path.empty()) out.path = L"/";
    if (parts.dwExtraInfoLength) out.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (out.path.find(L'#') != std::wstring::npos) return false;
    out.port = parts.nPort;
    out.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const auto host = Lower(Utf8(out.host));
    return out.secure || host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]";
}
struct InternetCloser { void operator()(void* p) const { if (p) WinHttpCloseHandle(p); } };
using Internet = std::unique_ptr<void, InternetCloser>;
bool NetworkError(std::wstring& error, const wchar_t* operation) {
    error = std::wstring(operation) + L" failed (" + std::to_wstring(GetLastError()) + L")";
    return false;
}
bool Http(const Request& req, const Provider& provider, const Config& config,
          std::string& response, std::wstring& error) {
    Url url, base;
    if (!ParseUrl(req.url, url)) { error = L"Invalid request URL; HTTPS required"; return false; }
    if (!provider.baseUrl.empty() && provider.templateType != "custom") {
        if (!ParseUrl(provider.baseUrl, base) || _wcsicmp(base.host.c_str(), url.host.c_str()) ||
            base.port != url.port || base.secure != url.secure) {
            error = L"Request URL does not match provider origin"; return false;
        }
    }
    DWORD mode = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
    if (config.proxyMode == "direct") mode = WINHTTP_ACCESS_TYPE_NO_PROXY;
    if (config.proxyMode == "manual") mode = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    const std::wstring proxy = Utf8(config.proxyServer);
    Internet session(WinHttpOpen(L"CCSwitchUsageBalance/2.0", mode,
        mode == WINHTTP_ACCESS_TYPE_NAMED_PROXY ? proxy.c_str() : WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return NetworkError(error, L"WinHttpOpen");
    int timeout = int(std::clamp(provider.timeoutSeconds, 2u, 30u) * 1000);
    WinHttpSetTimeouts(session.get(), timeout, timeout, timeout, timeout);
    Internet connection(WinHttpConnect(session.get(), url.host.c_str(), url.port, 0));
    if (!connection) return NetworkError(error, L"WinHttpConnect");
    const auto method = Utf8(req.method);
    Internet request(WinHttpOpenRequest(connection.get(), method.c_str(), url.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) return NetworkError(error, L"WinHttpOpenRequest");
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    DWORD disable = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
    for (const auto& header : req.headers) {
        const auto line = Utf8(header.first + ": " + header.second);
        if (!WinHttpAddRequestHeaders(request.get(), line.c_str(), DWORD(-1), WINHTTP_ADDREQ_FLAG_ADD))
            return NetworkError(error, L"WinHttpAddRequestHeaders");
    }
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        req.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(req.body.data()),
        DWORD(req.body.size()), DWORD(req.body.size()), 0)) return NetworkError(error, L"WinHttpSendRequest");
    if (!WinHttpReceiveResponse(request.get(), nullptr)) return NetworkError(error, L"WinHttpReceiveResponse");
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        return NetworkError(error, L"WinHttpQueryHeaders");
    if (status < 200 || status >= 300) { error = L"Balance endpoint returned HTTP " + std::to_wstring(status); return false; }
    response.clear();
    char buffer[8192];
    DWORD read = 0;
    const auto deadline = GetTickCount64() + timeout;
    for (;;) {
        if (GetTickCount64() >= deadline) { error = L"Response timed out"; return false; }
        if (!WinHttpReadData(request.get(), buffer, sizeof(buffer), &read)) return NetworkError(error, L"WinHttpReadData");
        if (!read) break;
        if (response.size() + read > 4 * 1024 * 1024) { error = L"Response exceeds 4 MiB"; return false; }
        response.append(buffer, read);
    }
    return true;
}
} // namespace

bool ListProviders(sqlite3* db, std::vector<ProviderChoice>& choices, std::wstring& error) {
    choices.clear();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,app_type,name,CASE WHEN json_valid(meta) THEN "
        "COALESCE(json_extract(meta,'$.usage_script.enabled'),0) ELSE 0 END,"
        "CASE WHEN json_valid(meta) THEN length(COALESCE(json_extract(meta,'$.usage_script.code'),'')) ELSE 0 END "
        "FROM providers ORDER BY app_type,name COLLATE NOCASE,id";
    if (sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr) != SQLITE_OK) {
        error = L"Cannot list cc-switch providers"; return false;
    }
    int result = SQLITE_OK;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
        choices.push_back({Text(stmt,0),Text(stmt,1),Text(stmt,2),sqlite3_column_int(stmt,3) != 0,sqlite3_column_int(stmt,4) > 0});
    sqlite3_finalize(stmt);
    if (result != SQLITE_DONE) { error = L"Cannot finish reading provider list"; choices.clear(); return false; }
    error.clear();
    return true;
}

bool LoadProvider(sqlite3* db, const Config& config, const std::string& settingsJson,
                  Provider& p, std::wstring& error) {
    p = {};
    std::string id = config.providerId;
    if (id.empty()) {
        std::string key = config.app == "codex" ? "currentProviderCodex" :
            config.app == "claude" ? "currentProviderClaude" :
            config.app == "gemini" ? "currentProviderGemini" :
            config.app == "grokbuild" ? "currentProviderGrokbuild" :
            config.app == "claude_desktop" ? "currentProviderClaudeDesktop" :
            config.app == "opencode" ? "currentProviderOpencode" :
            config.app == "openclaw" ? "currentProviderOpenclaw" :
            config.app == "hermes" ? "currentProviderHermes" :
            config.app == "agy" ? "currentProviderAgy" :
            config.app == "copilot" ? "currentProviderCopilot" : "";
        sqlite3_stmt* settings = nullptr;
        if (!key.empty() && !settingsJson.empty() && sqlite3_prepare_v2(db,
            "SELECT json_extract(?1,?2) WHERE json_valid(?1)", -1, &settings, nullptr) == SQLITE_OK) {
            std::string path = "$." + key;
            sqlite3_bind_text(settings, 1, settingsJson.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(settings, 2, path.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(settings) == SQLITE_ROW) id = Text(settings, 0);
        }
        if (settings) sqlite3_finalize(settings);
    }
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,app_type,name,meta,settings_config FROM providers "
        "WHERE app_type=?1 AND ((?2<>'' AND id=?2) OR (?2='' AND is_current=1)) LIMIT 1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = L"Cannot read cc-switch providers table"; return false;
    }
    sqlite3_bind_text(stmt, 1, config.app.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) { p.id = Text(stmt, 0); p.app = Text(stmt, 1); p.name = Text(stmt, 2); p.meta = Text(stmt, 3); p.settings = Text(stmt, 4); }
    sqlite3_finalize(stmt);
    if (!found) { error = L"No selected provider for " + Utf8(config.app); return false; }
    Js js;
    if (!js.ctx) { error = L"Cannot initialize script runtime"; return false; }
    JSValue meta = JS_ParseJSON(js.ctx, p.meta.data(), p.meta.size(), "provider-meta");
    JSValue settings = JS_ParseJSON(js.ctx, p.settings.data(), p.settings.size(), "provider-settings");
    JSValue usage = JS_GetPropertyStr(js.ctx, meta, "usage_script");
    JSValue enabled = JS_GetPropertyStr(js.ctx, usage, "enabled");
    bool ok = JS_IsBool(enabled) && JS_ToBool(js.ctx, enabled) == 1;
    JS_FreeValue(js.ctx, enabled);
    p.code = String(js.ctx, usage, "code");
    p.templateType = String(js.ctx, usage, "templateType");
    p.apiKey = String(js.ctx, usage, "apiKey");
    p.baseUrl = String(js.ctx, usage, "baseUrl");
    p.accessToken = String(js.ctx, usage, "accessToken");
    p.userId = String(js.ctx, usage, "userId");
    double timeout = 10;
    Number(js.ctx, usage, "timeout", timeout);
    p.timeoutSeconds = unsigned(std::clamp(timeout, 2.0, 30.0));
    JSValue auth = JS_GetPropertyStr(js.ctx, settings, "auth");
    JSValue env = JS_GetPropertyStr(js.ctx, settings, "env");
    JSValue options = JS_GetPropertyStr(js.ctx, settings, "options");
    if (p.apiKey.empty()) p.apiKey = String(js.ctx, auth, "OPENAI_API_KEY");
    if (p.apiKey.empty()) p.apiKey = String(js.ctx, env, "ANTHROPIC_AUTH_TOKEN");
    if (p.apiKey.empty()) p.apiKey = String(js.ctx, env, "ANTHROPIC_API_KEY");
    if (p.apiKey.empty()) p.apiKey = String(js.ctx, env, "GEMINI_API_KEY");
    if (p.apiKey.empty()) p.apiKey = String(js.ctx, options, "apiKey");
    if (p.baseUrl.empty()) p.baseUrl = String(js.ctx, env, "ANTHROPIC_BASE_URL");
    if (p.baseUrl.empty()) p.baseUrl = String(js.ctx, env, "GOOGLE_GEMINI_BASE_URL");
    if (p.baseUrl.empty()) p.baseUrl = String(js.ctx, options, "baseURL");
    if (p.baseUrl.empty()) {
        std::string toml = String(js.ctx, settings, "config");
        std::smatch match;
        std::regex basePattern(R"((?:^|\n)\s*base_url\s*=\s*["']([^"'\r\n]+)["'])");
        if (std::regex_search(toml, match, basePattern)) p.baseUrl = match[1].str();
    }
    while (!p.baseUrl.empty() && p.baseUrl.back() == '/') p.baseUrl.pop_back();
    for (JSValue v : {options, env, auth, usage, settings, meta}) JS_FreeValue(js.ctx, v);
    if (!ok) { error = L"Enable usage query on this provider in cc-switch"; return false; }
    if (p.code.empty()) { error = L"This provider needs a request/extractor usage script; native subscription templates are not supported"; return false; }
    error.clear();
    return true;
}

bool BuildRequest(const Provider& p, Request& req, std::wstring& error) {
    Js js;
    std::string json;
    if (!js.Eval(Script(p) + "JSON.stringify(__usageConfig.request)", json)) {
        error = L"Cannot evaluate usage request (script error or execution limit)"; return false;
    }
    JSValue obj = JS_ParseJSON(js.ctx, json.data(), json.size(), "request");
    req = {};
    req.url = String(js.ctx, obj, "url");
    const std::string method = String(js.ctx, obj, "method");
    if (!method.empty()) req.method = method;
    req.body = String(js.ctx, obj, "body");
    JSValue headers = JS_GetPropertyStr(js.ctx, obj, "headers");
    JSPropertyEnum* props = nullptr;
    uint32_t count = 0;
    bool ok = true;
    if (JS_IsObject(headers) && JS_GetOwnPropertyNames(js.ctx, &props, &count, headers,
        JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        for (uint32_t i = 0; i < count; ++i) {
            const char* key = JS_AtomToCString(js.ctx, props[i].atom);
            std::string name = key ? key : "";
            if (key) JS_FreeCString(js.ctx, key);
            std::string val = String(js.ctx, headers, name.c_str());
            if (name.empty() || name.find_first_not_of("!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos ||
                val.find_first_of("\r\n") != std::string::npos) ok = false;
            req.headers.emplace_back(name, val);
            JS_FreeAtom(js.ctx, props[i].atom);
        }
        js_free(js.ctx, props);
    }
    JS_FreeValue(js.ctx, headers);
    JS_FreeValue(js.ctx, obj);
    if (!ok || req.url.empty() || (req.method != "GET" && req.method != "POST")) {
        error = L"Invalid usage request; only GET/POST and valid headers are supported"; return false;
    }
    return true;
}

bool ExtractValue(const Provider& p, const std::string& response, unsigned planIndex,
                  Value& out, std::wstring& error) {
    Js js;
    std::string json;
    if (!js.ctx) { error = L"Cannot initialize script runtime"; return false; }
    JSValue input = JS_ParseJSON(js.ctx, response.data(), response.size(), "response");
    if (JS_IsException(input)) { error = L"Balance response is not valid JSON"; return false; }
    JSValue global = JS_GetGlobalObject(js.ctx);
    JS_SetPropertyStr(js.ctx, global, "__usageResponse", input);
    JS_FreeValue(js.ctx, global);
    if (!js.Eval(Script(p) + "JSON.stringify(__usageConfig.extractor(__usageResponse))", json)) {
        error = L"Cannot evaluate balance extractor (script error or execution limit)"; return false;
    }
    JSValue result = JS_ParseJSON(js.ctx, json.data(), json.size(), "balance-result");
    JSValue plan = JS_IsArray(result) ? JS_GetPropertyUint32(js.ctx, result, planIndex) : JS_DupValue(js.ctx, result);
    JSValue valid = JS_GetPropertyStr(js.ctx, plan, "isValid");
    bool ok = JS_IsObject(plan) && !(JS_IsBool(valid) && JS_ToBool(js.ctx, valid) == 0);
    if (!ok) error = L"Account/plan unavailable, or plan_index is out of range";
    JS_FreeValue(js.ctx, valid);
    Value value;
    value.hasUsed = Number(js.ctx, plan, "used", value.used);
    value.hasTotal = Number(js.ctx, plan, "total", value.total);
    if (ok && !Number(js.ctx, plan, "remaining", value.remaining)) {
        double total = 0, used = 0;
        ok = Number(js.ctx, plan, "total", total) && Number(js.ctx, plan, "used", used);
        if (ok) { value.remaining = total - used; ok = std::isfinite(value.remaining); }
        if (!ok) error = L"Usage result has no numeric remaining balance";
    }
    if (ok && value.hasTotal && !value.hasUsed) {
        value.used = value.total - value.remaining;
        value.hasUsed = std::isfinite(value.used);
    }
    if (ok && value.hasUsed && !value.hasTotal) {
        value.total = value.used + value.remaining;
        value.hasTotal = std::isfinite(value.total);
    }
    value.unit = String(js.ctx, plan, "unit");
    value.plan = String(js.ctx, plan, "planName");
    JS_FreeValue(js.ctx, plan);
    JS_FreeValue(js.ctx, result);
    if (ok) { out = value; error.clear(); }
    return ok;
}
bool Fetch(const Provider& p, const Config& config, Value& value, std::wstring& error) {
    Request req;
    std::string response;
    return BuildRequest(p, req, error) && Http(req, p, config, response, error) &&
        ExtractValue(p, response, config.planIndex, value, error);
}
std::wstring FormatValue(const Value& v) {
    wchar_t number[96];
    swprintf(number, 96, L"%.2f", v.remaining);
    const auto unit = Lower(v.unit);
    if (unit == "usd" || unit == "$") return L"$" + std::wstring(number);
    if (unit == "cny" || unit == "rmb" || unit == "\xc2\xa5" || unit == "\xe5\x85\x83") return L"\u00a5" + std::wstring(number);
    return std::wstring(number) + (v.unit.empty() ? L"" : L" " + Utf8(v.unit));
}
} // namespace balance
