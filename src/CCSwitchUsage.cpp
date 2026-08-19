// CCSwitchUsage - TrafficMonitor 插件
// 任务栏自绘两行: 第一行消耗金额, 第二行消耗 tokens; 悬停显示近 7/30 天汇总
// 数据来源: %USERPROFILE%\.cc-switch\cc-switch.db (只读)
#include "PluginInterface.h"
#include <windows.h>
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
#define FRESH_INPUT_SQL \
    "CASE " \
    "WHEN input_token_semantics = 2 THEN input_tokens " \
    "WHEN app_type IN ('codex','gemini','grokbuild') " \
    "AND input_token_semantics = 1 " \
    "AND input_tokens >= (cache_read_tokens + cache_creation_tokens) " \
    "THEN input_tokens - cache_read_tokens - cache_creation_tokens " \
    "WHEN app_type IN ('codex','gemini','grokbuild') " \
    "AND input_token_semantics = 0 " \
    "AND input_tokens >= cache_read_tokens " \
    "THEN input_tokens - cache_read_tokens " \
    "ELSE input_tokens END"

static long long RealTokens(const UsageData& d) {
    return d.freshInputTok + d.outTok + d.cacheCreationTok + d.cacheReadTok;
}

static UsageData g_today, g_week, g_month;
static std::wstring g_line1 = L"$0.00";   // 第一行: 消耗金额
static std::wstring g_line2 = L"0M"; // 第二行: 消耗 tokens
static std::wstring g_tooltipText = L"cc-switch \u7528\u91cf";
static ULONGLONG g_lastQueryTick = 0;

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
static void QueryRange(sqlite3* db, long long startTs, UsageData& d) {
    const char* sql =
        "select count(*), ifnull(sum(" FRESH_INPUT_SQL "),0),"
        " ifnull(sum(output_tokens),0), ifnull(sum(cache_creation_tokens),0),"
        " ifnull(sum(cache_read_tokens),0), ifnull(sum(cast(total_cost_usd as real)),0)"
        " from proxy_request_logs where created_at >= ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, startTs);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            d.req = sqlite3_column_int64(stmt, 0);
            d.freshInputTok = sqlite3_column_int64(stmt, 1);
            d.outTok = sqlite3_column_int64(stmt, 2);
            d.cacheCreationTok = sqlite3_column_int64(stmt, 3);
            d.cacheReadTok = sqlite3_column_int64(stmt, 4);
            d.cost = sqlite3_column_double(stmt, 5);
        }
        sqlite3_finalize(stmt);
    }
}

static void FormatTok(long long n, wchar_t* buf, size_t cap) {
    if (n >= 1000000000) swprintf(buf, cap, L"%.2fB", n / 1e9);
    else if (n >= 1000000) swprintf(buf, cap, L"%.1fM", n / 1e6);
    else if (n >= 1000) swprintf(buf, cap, L"%.1fK", n / 1e3);
    else swprintf(buf, cap, L"%lld", n);
}

static void RefreshData(bool force) {
    ULONGLONG now = GetTickCount64();
    if (!force && now - g_lastQueryTick < 5000) return;  // 最少 5 秒刷新一次
    g_lastQueryTick = now;

    char dbPath[MAX_PATH * 2];
    const char* profile = getenv("USERPROFILE");
    if (!profile) { g_line1 = L"--"; g_line2 = L"--"; return; }
    snprintf(dbPath, sizeof(dbPath), "%s\\.cc-switch\\cc-switch.db", profile);

    sqlite3* db = nullptr;
    int flags = SQLITE_OPEN_READONLY;
    if (sqlite3_open_v2(dbPath, &db, flags, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        g_line1 = L"--";
        g_line2 = L"--";
        g_tooltipText = L"cc-switch: \u6570\u636e\u5e93\u4e0d\u53ef\u7528";
        return;
    }
    sqlite3_busy_timeout(db, 3000);

    QueryRange(db, StartOfDaysAgo(0), g_today);
    QueryRange(db, StartOfDaysAgo(6), g_week);
    QueryRange(db, StartOfDaysAgo(29), g_month);
    sqlite3_close(db);

    // 第一行: cc：金额 / 第二行: 消耗 tokens
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
    wchar_t tt[512];
    swprintf(tt, 512,
        L"cc-switch \u4f7f\u7528\u7edf\u8ba1\n\u4eca\u65e5: $%.2f \u6d88\u8017%ls\n\u8fd17\u5929: $%.2f \u6d88\u8017%ls\n\u8fd130\u5929: $%.2f \u6d88\u8017%ls",
        g_today.cost, t1,
        g_week.cost, t2,
        g_month.cost, t3);
    g_tooltipText = tt;
}

// ---------------- 显示项目(自绘) ----------------
class CCSwitchUsageItem : public IPluginItem {
public:
    virtual const wchar_t* GetItemName() const { return L"CC\u7528\u91cf"; }
    virtual const wchar_t* GetItemId() const { return L"ccswitch_usage"; }
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
        return nullptr;
    }
    virtual void DataRequired() { RefreshData(false); }
    virtual const wchar_t* GetInfo(PluginInfoIndex index) {
        switch (index) {
        case TMI_NAME: return L"cc-switch \u7528\u91cf";
        case TMI_DESCRIPTION: return L"\u4efb\u52a1\u680f\u81ea\u7ed8\u4e24\u884c: \u7b2c\u4e00\u884c\u6d88\u8017\u91d1\u989d, \u7b2c\u4e8c\u884c\u6d88\u8017 tokens\u3002\u60ac\u505c\u67e5\u770b\u8fd1 7/30 \u5929\u6c47\u603b\u3002";
        case TMI_AUTHOR: return L"Blackn-L";
        case TMI_VERSION: return L"1.3";
        case TMI_URL: return L"https://github.com/Blackn-L/ccswitch-usage-plugin";
        default: return L"";
        }
    }
    virtual const wchar_t* GetTooltipInfo() { RefreshData(false); return g_tooltipText.c_str(); }

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
