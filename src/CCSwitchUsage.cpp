// Local extension: provider name and remaining balance via the configured usage script.
// CCSwitchUsage - TrafficMonitor 插件
// 任务栏自绘两行: 第一行消耗金额, 第二行消耗 tokens; 悬停显示近 7/30 天汇总
// 数据来源: cc-switch 本地数据库 (只读), 路径解析与 cc-switch 保持一致
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "PluginInterface.h"
#include "ProviderBalance.h"
#include <windows.h>
#include <shlobj.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdio.h>
#include <string>
#include <sqlite3.h>

// ---------------- 数据 ----------------
struct UsageData {
    long long req = 0;
    long long freshInputTok = 0;
    long long outTok = 0;
    long long cacheCreationTok = 0;
    long long cacheReadTok = 0;
    double cost = 0;
};

// cc-switch 的“真实消耗 Tokens”口径：
// fresh input + output + cache creation + cache read。
// Codex / Gemini / GrokBuild 的 input_tokens 可能已经包含缓存部分，
// 需要先归一化，否则会把 cache read 再算一遍。
static std::string FreshInputSql(const std::string& p, bool hasSemantics) {
    if (hasSemantics) {
        return "CASE "
               "WHEN " + p + "input_token_semantics = 2 THEN " + p + "input_tokens "
               "WHEN " + p + "app_type IN ('codex','gemini','grokbuild') "
               "AND " + p + "input_token_semantics = 1 "
               "AND " + p + "input_tokens >= (" + p + "cache_read_tokens + " + p + "cache_creation_tokens) "
               "THEN " + p + "input_tokens - " + p + "cache_read_tokens - " + p + "cache_creation_tokens "
               "WHEN " + p + "app_type IN ('codex','gemini','grokbuild') "
               "AND " + p + "input_token_semantics = 0 "
               "AND " + p + "input_tokens >= " + p + "cache_read_tokens "
               "THEN " + p + "input_tokens - " + p + "cache_read_tokens "
               "ELSE " + p + "input_tokens END";
    }
    // schema v13 之前的旧库没有 input_token_semantics 列, 按旧口径只减 cache read
    return "CASE "
           "WHEN " + p + "app_type IN ('codex','gemini','grokbuild') "
           "AND " + p + "input_tokens >= " + p + "cache_read_tokens "
           "THEN " + p + "input_tokens - " + p + "cache_read_tokens "
           "ELSE " + p + "input_tokens END";
}

static long long RealTokens(const UsageData& d) {
    return d.freshInputTok + d.outTok + d.cacheCreationTok + d.cacheReadTok;
}

static UsageData g_today, g_week, g_month;
static std::wstring g_line1 = L"$0.00";   // 第一行: 消耗金额
static std::wstring g_line2 = L"0M"; // 第二行: 消耗 tokens
static std::wstring g_tooltipText = L"cc-switch \u7528\u91cf";
static ULONGLONG g_lastQueryTick = 0;
static bool g_hasData = false;             // 是否成功读到过数据

// ---------------- 路径解析(与 cc-switch 对齐) ----------------
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static bool GetShellDir(int csidl, std::wstring& out) {
    wchar_t buf[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf)) || !buf[0])
        return false;
    out = buf;
    return true;
}

static std::wstring GetEnvW(const wchar_t* name) {
    const wchar_t* v = _wgetenv(name);
    return v ? std::wstring(v) : std::wstring();
}

static bool FileExistsUtf8(const std::string& utf8) {
    DWORD attr = GetFileAttributesW(Utf8ToWide(utf8).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirExistsUtf8(const std::string& utf8) {
    DWORD attr = GetFileAttributesW(Utf8ToWide(utf8).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string JoinUtf8(const std::string& dir, const char* name) {
    if (dir.empty()) return std::string(name);
    char last = dir[dir.size() - 1];
    if (last == '\\' || last == '/') return dir + name;
    return dir + "\\" + name;
}

static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    out.clear();
    char buf[4096];
    DWORD rd = 0;
    while (ReadFile(f, buf, sizeof(buf), &rd, nullptr) && rd > 0)
        out.append(buf, rd);
    CloseHandle(f);
    return true;
}

// 用 SQLite 内置 JSON 函数解析 Tauri Store 文件, 解析失败时回退默认路径
static std::string JsonGetString(const std::string& json, const char* key) {
    sqlite3* db = nullptr;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return std::string();
    }
    std::string result;
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("select json_extract(?1, '$.") + key + "')";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, json.c_str(), (int)json.size(), SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) == SQLITE_TEXT) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) result = (const char*)text;
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

static std::string ExpandTilde(const std::string& path, const std::string& homeUtf8) {
    if (path == "~") return homeUtf8;
    if (path.compare(0, 2, "~/") == 0 || path.compare(0, 2, "~\\") == 0)
        return JoinUtf8(homeUtf8, path.c_str() + 2);
    return path;
}

struct DbLocation {
    std::string utf8;
    std::wstring wide;
    bool found = false;
};

static DbLocation ResolveDbPath() {
    DbLocation loc;
    std::wstring homeW;
    if (!GetShellDir(CSIDL_PROFILE, homeW)) homeW = GetEnvW(L"USERPROFILE");
    std::string home = WideToUtf8(homeW);
    loc.utf8 = JoinUtf8(home, ".cc-switch\\cc-switch.db");
    loc.wide = Utf8ToWide(loc.utf8);
    if (home.empty()) return loc;

    // 1) cc-switch 设置的自定义数据目录 (Tauri Store)
    std::wstring roamingW;
    if (!GetShellDir(CSIDL_APPDATA, roamingW)) roamingW = GetEnvW(L"APPDATA");
    if (!roamingW.empty()) {
        std::string json;
        if (ReadFileUtf8(roamingW + L"\\com.ccswitch.desktop\\app_paths.json", json)) {
            std::string dir = Trim(JsonGetString(json, "app_config_dir_override"));
            if (!dir.empty()) {
                dir = ExpandTilde(dir, home);
                if (DirExistsUtf8(dir)) {
                    std::string db = JoinUtf8(dir, "cc-switch.db");
                    loc.utf8 = db;
                    loc.wide = Utf8ToWide(db);
                    loc.found = FileExistsUtf8(db);
                    return loc;
                }
            }
        }
    }

    // 2) 默认路径: <用户目录>\.cc-switch\cc-switch.db
    if (FileExistsUtf8(loc.utf8)) {
        loc.found = true;
        return loc;
    }

    // 3) 兼容 cc-switch v3.10.3: HOME 指向其他目录时数据库可能落在 %HOME%\.cc-switch
    std::wstring legacyHomeW = GetEnvW(L"HOME");
    if (!legacyHomeW.empty()) {
        std::string legacy = JoinUtf8(WideToUtf8(legacyHomeW), ".cc-switch\\cc-switch.db");
        if (FileExistsUtf8(legacy)) {
            loc.utf8 = legacy;
            loc.wide = Utf8ToWide(legacy);
            loc.found = true;
        }
    }
    return loc;
}

// 时间: 今日零点 / N 天前零点 (本地时区, unix 秒)
static long long StartOfDaysAgo(int daysAgo) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    st.wHour = 0; st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 0;
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER ul;
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    long long midnight = (long long)(ul.QuadPart / 10000000ULL) - 11644473600LL;
    return midnight - (long long)daysAgo * 86400LL;
}

struct DbSchema {
    bool hasLogs = false;
    bool hasSemantics = false;        // proxy_request_logs.input_token_semantics
    bool hasRollups = false;          // usage_daily_rollups
    bool rollupHasSemantics = false;
};

static bool TableExists(sqlite3* db, const char* table) {
    sqlite3_stmt* stmt = nullptr;
    bool exists = false;
    if (sqlite3_prepare_v2(db, "select 1 from sqlite_master where type in ('table','view') and name = ?",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, table, -1, SQLITE_STATIC);
        exists = (sqlite3_step(stmt) == SQLITE_ROW);
    }
    if (stmt) sqlite3_finalize(stmt);
    return exists;
}

static bool ColumnExists(sqlite3* db, const char* table, const char* column) {
    std::string sql = std::string("select 1 from pragma_table_info('") + table + "') where name = ?";
    sqlite3_stmt* stmt = nullptr;
    bool exists = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, column, -1, SQLITE_STATIC);
        exists = (sqlite3_step(stmt) == SQLITE_ROW);
    }
    if (stmt) sqlite3_finalize(stmt);
    return exists;
}

static DbSchema DetectSchema(sqlite3* db) {
    DbSchema s;
    s.hasLogs = TableExists(db, "proxy_request_logs");
    if (s.hasLogs) s.hasSemantics = ColumnExists(db, "proxy_request_logs", "input_token_semantics");
    s.hasRollups = TableExists(db, "usage_daily_rollups");
    if (s.hasRollups) s.rollupHasSemantics = ColumnExists(db, "usage_daily_rollups", "input_token_semantics");
    return s;
}

static bool StepUsageQuery(sqlite3* db, const std::string& sql, long long startTs, UsageData& d) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    bool ok = false;
    sqlite3_bind_int64(stmt, 1, startTs);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        d.req += sqlite3_column_int64(stmt, 0);
        d.freshInputTok += sqlite3_column_int64(stmt, 1);
        d.outTok += sqlite3_column_int64(stmt, 2);
        d.cacheCreationTok += sqlite3_column_int64(stmt, 3);
        d.cacheReadTok += sqlite3_column_int64(stmt, 4);
        d.cost += sqlite3_column_double(stmt, 5);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

// cc-switch 会把超过保留期的明细归档进 usage_daily_rollups 并从明细表删除,
// 只读明细表会让 7/30 天统计在部分机器上凭空变少, 这里和 cc-switch 一样合并两份数据
static bool QueryRange(sqlite3* db, const DbSchema& schema, long long startTs, UsageData& d) {
    std::string detailSql =
        "select count(*), ifnull(sum(" + FreshInputSql("", schema.hasSemantics) + "),0),"
        " ifnull(sum(output_tokens),0), ifnull(sum(cache_creation_tokens),0),"
        " ifnull(sum(cache_read_tokens),0), ifnull(sum(cast(total_cost_usd as real)),0)"
        " from proxy_request_logs where created_at >= ?";
    if (!StepUsageQuery(db, detailSql, startTs, d)) return false;

    if (schema.hasRollups) {
        // 起点是本地零点, 统计范围覆盖整天; rollup 按 cc-switch 的本地日期存储
        std::string rollupSql =
            "select ifnull(sum(request_count),0), ifnull(sum(" + FreshInputSql("", schema.rollupHasSemantics) + "),0),"
            " ifnull(sum(output_tokens),0), ifnull(sum(cache_creation_tokens),0),"
            " ifnull(sum(cache_read_tokens),0), ifnull(sum(cast(total_cost_usd as real)),0)"
            " from usage_daily_rollups where date >= date(?, 'unixepoch', 'localtime')";
        if (!StepUsageQuery(db, rollupSql, startTs, d)) return false;
    }
    return true;
}

static void FormatTok(long long n, wchar_t* buf, size_t cap) {
    if (n >= 1000000000) swprintf(buf, cap, L"%.2fB", n / 1e9);
    else if (n >= 1000000) swprintf(buf, cap, L"%.1fM", n / 1e6);
    else if (n >= 1000) swprintf(buf, cap, L"%.1fK", n / 1e3);
    else swprintf(buf, cap, L"%lld", n);
}

static void BuildDisplay(const std::wstring& status) {
    wchar_t cost[32];
    swprintf(cost, 32, L"$%.2f", g_today.cost);
    wchar_t tok[32];
    FormatTok(RealTokens(g_today), tok, 32);
    g_line1 = cost;
    g_line2 = tok;

    // 悬停提示(不显示请求次数)
    wchar_t t1[32], t2[32], t3[32];
    FormatTok(RealTokens(g_today), t1, 32);
    FormatTok(RealTokens(g_week), t2, 32);
    FormatTok(RealTokens(g_month), t3, 32);
    wchar_t tt[768] = {};
    int n = swprintf(tt, 768,
        L"cc-switch \u4f7f\u7528\u7edf\u8ba1\n\u4eca\u65e5: $%.2f \u6d88\u8017\uff1a%ls\n\u8fd17\u5929: $%.2f \u6d88\u8017\uff1a%ls\n\u8fd130\u5929: $%.2f \u6d88\u8017\uff1a%ls",
        g_today.cost, t1,
        g_week.cost, t2,
        g_month.cost, t3);
    if (!status.empty() && n > 0 && n < 767)
        swprintf(tt + n, 768 - n, L"\n\u72b6\u6001: %ls", status.c_str());
    g_tooltipText = tt;
}

static void SetFailure(const std::wstring& msg) {
    if (g_hasData) {
        // 保留上一次成功的数据, 避免 cc-switch 写库时的短暂锁导致任务栏数字闪烁
        BuildDisplay(msg);
    } else {
        g_line1 = L"--";
        g_line2 = L"--";
        g_tooltipText = L"cc-switch \u7528\u91cf\n\u72b6\u6001: " + msg;
    }
}

static void RefreshData(bool force) {
    ULONGLONG now = GetTickCount64();
    if (!force && now - g_lastQueryTick < 5000) return;  // 最少 5 秒刷新一次
    g_lastQueryTick = now;

    DbLocation loc = ResolveDbPath();
    if (!loc.found) {
        SetFailure(L"\u672a\u627e\u5230\u6570\u636e\u5e93: " + loc.wide);
        return;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(loc.utf8.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::wstring err = Utf8ToWide(db ? sqlite3_errmsg(db) : "cannot open database");
        if (db) sqlite3_close(db);
        SetFailure(L"\u6570\u636e\u5e93\u4e0d\u53ef\u7528: " + err);
        return;
    }
    sqlite3_busy_timeout(db, 3000);

    DbSchema schema = DetectSchema(db);
    if (!schema.hasLogs) {
        sqlite3_close(db);
        SetFailure(L"\u672a\u627e\u5230\u65e5\u5fd7\u8868 (cc-switch \u7248\u672c\u8fc7\u65e7?)");
        return;
    }

    UsageData today, week, month;
    bool ok = QueryRange(db, schema, StartOfDaysAgo(0), today)
           && QueryRange(db, schema, StartOfDaysAgo(6), week)
           && QueryRange(db, schema, StartOfDaysAgo(29), month);
    std::wstring queryErr = Utf8ToWide(sqlite3_errmsg(db));
    sqlite3_close(db);
    if (!ok) {
        SetFailure(L"\u67e5\u8be2\u5931\u8d25: " + queryErr);
        return;
    }

    g_today = today;
    g_week = week;
    g_month = month;
    g_hasData = true;

    // 长期没有记录大概率是读到了另一个空库, 把路径带出来方便排查
    std::wstring status;
    if (month.req == 0)
        status = L"\u8fd130\u5929\u65e0\u8bb0\u5f55: " + loc.wide;
    BuildDisplay(status);
}

// ---------------- 显示项目(自绘) ----------------
class CCSwitchUsageItem : public IPluginItem {
public:
    virtual const wchar_t* GetItemName() const { return L"CC\u7528\u91cf(\u4f59\u989d\u7248)"; }
    virtual const wchar_t* GetItemId() const { return L"ccswitch_balance_usage"; }
    virtual const wchar_t* GetItemLableText() const { return L""; }
    virtual const wchar_t* GetItemValueText() const { return g_line2.c_str(); }
    virtual const wchar_t* GetItemValueSampleText() const { return L"\u6d88\u8017 999.9M"; }
    virtual bool IsCustomDraw() const { return true; }
    virtual int GetItemWidth() const { return 80; }  // DPI 96 时的宽度(回退值)

    virtual int GetItemWidthEx(void* hDC) const {
        HDC dc = (HDC)hDC;
        SIZE s1{}, s2{};
        if (!GetTextExtentPoint32W(dc, g_line1.c_str(), (int)g_line1.size(), &s1)) return 0;
        if (!GetTextExtentPoint32W(dc, g_line2.c_str(), (int)g_line2.size(), &s2)) return 0;
        int w = (s1.cx > s2.cx ? s1.cx : s2.cx) + 12;
        // 临时调试日志
        {
            LOGFONTW lfdbg{};
            int fh = 0;
            HGDIOBJ cur = GetCurrentObject(dc, OBJ_FONT);
            if (cur && GetObjectW(cur, sizeof(lfdbg), &lfdbg))
                fh = lfdbg.lfHeight < 0 ? -lfdbg.lfHeight : lfdbg.lfHeight;
            char buf[256];
            snprintf(buf, sizeof(buf), "[CCSwitch] MeasureEx fontH=%d w1=%d w2=%d ret=%d\n", fh, s1.cx, s2.cx, w);
            OutputDebugStringA(buf);
        }
        return w > 30 ? w : 30;
    }

    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) {
        HDC dc = (HDC)hDC;
        int lineH = h / 2;
        if (lineH < 4 || w <= 0) return;

        // 沿用 DC 当前字体族, 高度适配半行高
        int maxH = lineH - 2;
        HFONT font = nullptr;
        LOGFONTW lf{};
        HGDIOBJ cur = GetCurrentObject(dc, OBJ_FONT);
        if (cur && GetObjectW(cur, sizeof(lf), &lf) && lf.lfFaceName[0]) {
            int fh = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
            if (fh > maxH || fh <= 0) lf.lfHeight = -maxH;
            font = CreateFontIndirectW(&lf);
        }
        if (!font) {
            font = CreateFontW(-maxH, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"\u5fae\u8f6f\u96c5\u9ed1");
            if (!font) return;  // 无字体可用, 放弃绘制
        }
        HGDIOBJ oldFont = SelectObject(dc, font);
        int oldBk = SetBkMode(dc, TRANSPARENT);
        COLORREF oldCol = SetTextColor(dc, dark_mode ? RGB(255, 255, 255) : RGB(20, 20, 20));

        RECT r1 = { x, y, x + w, y + lineH };
        RECT r2 = { x, y + lineH, x + w, y + h };
        DrawTextW(dc, g_line1.c_str(), -1, &r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextW(dc, g_line2.c_str(), -1, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 临时调试日志: 绘制后实测宽度
        {
            SIZE s1{}, s2{};
            GetTextExtentPoint32W(dc, g_line1.c_str(), (int)g_line1.size(), &s1);
            GetTextExtentPoint32W(dc, g_line2.c_str(), (int)g_line2.size(), &s2);
            char buf[256];
            snprintf(buf, sizeof(buf), "[CCSwitch] Draw x=%d w=%d h=%d lineH=%d ext1=%d ext2=%d\n", x, w, h, lineH, s1.cx, s2.cx);
            OutputDebugStringA(buf);
        }
        SetTextColor(dc, oldCol);
        SetBkMode(dc, oldBk);
        SelectObject(dc, oldFont);
        DeleteObject(font);
    }
};

// ---------------- 插件主体 ----------------
class CCSwitchPlugin : public ITMPlugin {
public:
    CCSwitchPlugin() : m_item() {}

    virtual IPluginItem* GetItem(int index) {
        if (index == 0) return &m_item;
        if (index == 1) return balance::Item();
        return nullptr;
    }
    virtual void DataRequired() { RefreshData(false); balance::Refresh(ResolveDbPath().utf8); }
    virtual OptionReturn ShowOptionsDialog(void* parent) { return balance::ShowOptions(parent, ResolveDbPath().utf8); }
    virtual const wchar_t* GetInfo(PluginInfoIndex index) {
        switch (index) {
        case TMI_NAME: return L"cc-switch \u7528\u91cf\u4e0e\u4f59\u989d";
        case TMI_DESCRIPTION: return L"CC\u4f59\u989d: \u4f9b\u5e94\u5546\u540d\u79f0 / \u5269\u4f59\u4f59\u989d. CC\u7528\u91cf: \u91d1\u989d / Tokens.";
        case TMI_AUTHOR: return L"Blackn-L; local balance extension";
        case TMI_VERSION: return L"2.1-balance";
        case TMI_URL: return L"https://github.com/Blackn-L/ccswitch-usage-plugin";
        default: return L"";
        }
    }
    virtual const wchar_t* GetTooltipInfo() {
        RefreshData(false); balance::Refresh(ResolveDbPath().utf8);
        static std::wstring combined; combined = balance::GetDisplayConfig().tooltipUsage ? g_tooltipText + L"\n\n" : L"";
        combined += balance::Tooltip(); return combined.c_str();
    }

private:
    CCSwitchUsageItem m_item;
};

static CCSwitchPlugin g_plugin;

#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif

ITMPlugin* TMPluginGetInstance() {
    return &g_plugin;
}
