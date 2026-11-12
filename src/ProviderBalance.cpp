#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0603
#endif
#include "ProviderBalance.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace balance {
namespace {
struct State {
    std::mutex mutex;
    Snapshot snapshot;
    std::string identity;
    unsigned long long generation = 0, lastPoll = 0, nextQuery = 0;
    bool running = false;
    unsigned width = 150;
};
// The worker holds a DLL reference. State intentionally lives until process exit.
State& Data() { static auto* state = new State; return *state; }
std::wstring IniPath() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&IniPath), &module);
    wchar_t path[32768]{};
    DWORD size = GetModuleFileNameW(module, path, 32768);
    if (!size || size >= 32768) return {};
    std::wstring file(path, size);
    return file.substr(0, file.find_last_of(L"\\/") + 1) + L"CCSwitchUsageBalance.ini";
}
std::wstring ReadIni(const std::wstring& file, const wchar_t* key, const wchar_t* fallback) {
    wchar_t text[2048]{};
    GetPrivateProfileStringW(L"Balance", key, fallback, text, 2048, file.c_str());
    return text;
}
Config ReadConfig() {
    const auto file = IniPath();
    Config c;
    c.app = Utf8(ReadIni(file, L"app_type", L"codex"));
    c.providerId = Utf8(ReadIni(file, L"provider_id", L""));
    c.proxyMode = Utf8(ReadIni(file, L"proxy_mode", L"system"));
    c.proxyServer = Utf8(ReadIni(file, L"proxy_server", L""));
    if (c.proxyMode != "manual" && c.proxyMode != "direct") c.proxyMode = "system";
    c.refreshSeconds = std::clamp(GetPrivateProfileIntW(L"Balance", L"refresh_seconds", 60, file.c_str()), 15u, 3600u);
    c.width = std::clamp(GetPrivateProfileIntW(L"Balance", L"width", 150, file.c_str()), 80u, 400u);
    c.planIndex = std::min(GetPrivateProfileIntW(L"Balance", L"plan_index", 0, file.c_str()), 1000u);
    return c;
}
std::string SettingsFile(const std::string& dbPath) {
    auto end = dbPath.find_last_of("\\/");
    if (end == std::string::npos) return {};
    const auto path = Utf8(dbPath.substr(0, end + 1) + "settings.json");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    DWORD size = GetFileSize(file, nullptr), read = 0;
    std::string text;
    if (size != INVALID_FILE_SIZE && size < 1024 * 1024) {
        text.resize(size);
        if (!ReadFile(file, text.data(), size, &read, nullptr)) text.clear();
        else text.resize(read);
    }
    CloseHandle(file);
    return text;
}
unsigned long long UnixNow() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime; value.HighPart = ft.dwHighDateTime;
    return value.QuadPart / 10000000ULL - 11644473600ULL;
}
struct Job { Provider provider; Config config; unsigned long long generation; HMODULE module = nullptr; };
DWORD WINAPI Worker(void* input) {
    std::unique_ptr<Job> job(static_cast<Job*>(input));
    Value result;
    std::wstring error;
    bool ok = false;
    try { ok = Fetch(job->provider, job->config, result, error); }
    catch (...) { error = L"Balance query failed unexpectedly"; }
    auto& state = Data();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.running = false;
        if (job->generation == state.generation) {
            state.snapshot.loading = false;
            state.snapshot.error = error;
            if (ok) { state.snapshot.value = result; state.snapshot.hasValue = true; state.snapshot.updatedAt = UnixNow(); }
            state.nextQuery = GetTickCount64() + job->config.refreshSeconds * 1000ULL;
        }
    }
    HMODULE module = job->module;
    job.reset();
    FreeLibraryAndExitThread(module, 0);
    return 0;
}
std::wstring Name(const Snapshot& snapshot) { return snapshot.name.empty() ? L"Codex" : Utf8(snapshot.name); }
std::wstring Amount(const Snapshot& snapshot) {
    return snapshot.hasValue ? FormatValue(snapshot.value) + (snapshot.error.empty() ? L"" : L" *") :
        snapshot.loading ? L"\u67e5\u8be2\u4e2d..." : L"\u4f59\u989d --";
}
class BalanceItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"CC\u4f59\u989d"; }
    const wchar_t* GetItemId() const override { return L"ccswitch_provider_balance"; }
    const wchar_t* GetItemLableText() const override { return L""; }
    const wchar_t* GetItemValueText() const override {
        static thread_local std::wstring value;
        auto snapshot = GetSnapshot();
        value = Name(snapshot) + L" " + Amount(snapshot);
        return value.c_str();
    }
    const wchar_t* GetItemValueSampleText() const override { return L"Provider $9999.99"; }
    bool IsCustomDraw() const override { return true; }
    int GetItemWidth() const override { auto& state = Data(); std::lock_guard<std::mutex> lock(state.mutex); return int(state.width); }
    int GetItemWidthEx(void* hdc) const override { return MulDiv(GetItemWidth(), GetDeviceCaps((HDC)hdc, LOGPIXELSX), 96); }
    void DrawItem(void* hdc, int x, int y, int w, int h, bool dark) override {
        if (w <= 0 || h < 12) return;
        auto snapshot = GetSnapshot();
        auto name = Name(snapshot), amount = Amount(snapshot);
        HDC dc = (HDC)hdc;
        LOGFONTW lf{};
        HFONT font = nullptr;
        HGDIOBJ current = GetCurrentObject(dc, OBJ_FONT);
        if (current && GetObjectW(current, sizeof(lf), &lf)) {
            lf.lfHeight = -std::max(6, std::min(h / 2 - 1, int(std::abs(lf.lfHeight))));
            font = CreateFontIndirectW(&lf);
        }
        HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
        int oldBk = SetBkMode(dc, TRANSPARENT);
        COLORREF oldColor = SetTextColor(dc, dark ? RGB(255,255,255) : RGB(20,20,20));
        RECT top{x+3,y,x+w-3,y+h/2}, bottom{x+3,y+h/2,x+w-3,y+h};
        const UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
        DrawTextW(dc, name.c_str(), -1, &top, format);
        if (!snapshot.error.empty() && snapshot.hasValue) SetTextColor(dc, dark ? RGB(255,195,90) : RGB(150,85,0));
        DrawTextW(dc, amount.c_str(), -1, &bottom, format);
        SetTextColor(dc, oldColor); SetBkMode(dc, oldBk);
        if (font) { SelectObject(dc, oldFont); DeleteObject(font); }
    }
};
} // namespace
void Refresh(const std::string& dbPath) {
    auto& state = Data();
    auto now = GetTickCount64();
    { std::lock_guard<std::mutex> lock(state.mutex); if (now - state.lastPoll < 2000) return; state.lastPoll = now; }
    const Config config = ReadConfig();
    Provider provider;
    std::wstring error;
    sqlite3* db = nullptr;
    bool ok = sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK;
    if (ok) { sqlite3_busy_timeout(db, 50); ok = LoadProvider(db, config, SettingsFile(dbPath), provider, error); }
    else error = L"Cannot open cc-switch database";
    if (db) sqlite3_close(db);
    std::unique_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.width = config.width;
        std::string identity = config.app + '\0' + provider.id + '\0' + provider.meta + '\0' + provider.settings + '\0' +
            config.proxyMode + '\0' + config.proxyServer + '\0' + std::to_string(config.planIndex) + ':' + std::to_string(config.refreshSeconds);
        if (identity != state.identity) {
            state.identity = std::move(identity); ++state.generation;
            state.snapshot = {}; state.snapshot.providerId = provider.id; state.snapshot.name = provider.name;
            state.nextQuery = 0;
        }
        state.snapshot.name = provider.name;
        if (!ok) { state.snapshot.error = error; state.snapshot.loading = false; return; }
        if (state.running || now < state.nextQuery) return;
        state.running = true; state.snapshot.loading = !state.snapshot.hasValue;
        job = std::make_unique<Job>(Job{provider, config, state.generation});
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&Worker), &job->module)) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.running = false; state.snapshot.loading = false; state.snapshot.error = L"Cannot reference plugin module"; return;
    }
    HMODULE module = job->module;
    HANDLE thread = CreateThread(nullptr, 0, Worker, job.get(), 0, nullptr);
    if (thread) { job.release(); CloseHandle(thread); }
    else {
        FreeLibrary(module);
        std::lock_guard<std::mutex> lock(state.mutex);
        state.running = false; state.snapshot.loading = false; state.snapshot.error = L"Cannot start balance query worker";
    }
}
Snapshot GetSnapshot() { auto& state = Data(); std::lock_guard<std::mutex> lock(state.mutex); return state.snapshot; }
std::wstring Tooltip() {
    auto snapshot = GetSnapshot();
    std::wstring text = L"\n\ncc-switch \u4f9b\u5e94\u5546\u4f59\u989d\n\u4f9b\u5e94\u5546: " + Name(snapshot);
    text += L"\n\u5269\u4f59: " + Amount(snapshot);
    if (!snapshot.value.plan.empty()) text += L"\n\u5957\u9910: " + Utf8(snapshot.value.plan);
    if (snapshot.updatedAt) {
        ULARGE_INTEGER t{}; t.QuadPart = (snapshot.updatedAt + 11644473600ULL) * 10000000ULL;
        FILETIME utc{t.LowPart,t.HighPart}, local{}; SYSTEMTIME st{};
        FileTimeToLocalFileTime(&utc, &local); FileTimeToSystemTime(&local, &st);
        wchar_t time[80]; swprintf(time, 80, L"\n\u66f4\u65b0: %04u-%02u-%02u %02u:%02u:%02u", st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        text += time;
    }
    if (!snapshot.error.empty()) text += L"\n\u72b6\u6001: " + snapshot.error;
    if (snapshot.hasValue && !snapshot.error.empty()) text += L"\n* \u4fdd\u7559\u4e0a\u6b21\u6210\u529f\u4f59\u989d";
    return text;
}
IPluginItem* Item() { static BalanceItem item; return &item; }
ITMPlugin::OptionReturn ShowOptions(void* parent) {
    const auto file = IniPath();
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const wchar_t defaults[] = L"[Balance]\r\napp_type=codex\r\nprovider_id=\r\nrefresh_seconds=60\r\nproxy_mode=system\r\nproxy_server=\r\nwidth=150\r\nplan_index=0\r\n";
        HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written = 0; const wchar_t bom = 0xfeff;
            WriteFile(f, &bom, sizeof(bom), &written, nullptr);
            WriteFile(f, defaults, sizeof(defaults)-sizeof(wchar_t), &written, nullptr); CloseHandle(f);
        }
    }
    if ((INT_PTR)ShellExecuteW((HWND)parent, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
        MessageBoxW((HWND)parent, file.c_str(), L"CCSwitchUsageBalance.ini", MB_OK | MB_ICONINFORMATION);
    return ITMPlugin::OR_OPTION_UNCHANGED;
}
} // namespace balance
