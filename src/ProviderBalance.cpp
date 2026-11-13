#include "ProviderBalance.h"
#include <windows.h>
#include <memory>
#include <mutex>

namespace balance {
namespace {
struct State {
    std::mutex mutex;
    Snapshot snapshot;
    Config config;
    std::string identity;
    unsigned long long generation = 0, lastPoll = 0, nextQuery = 0;
    bool running = false;
};
// The worker holds a DLL reference. State intentionally lives until process exit.
State& Data() { static auto* state = new State; return *state; }
std::string SettingsFile(const std::string& dbPath) {
    auto end = dbPath.find_last_of("\\/");
    if (end == std::string::npos) return {};
    const auto path = Utf8(dbPath.substr(0,end+1) + "settings.json");
    HANDLE file = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    DWORD size = GetFileSize(file,nullptr), read = 0;
    std::string text;
    if (size != INVALID_FILE_SIZE && size < 1024*1024) {
        text.resize(size);
        if (!ReadFile(file,text.data(),size,&read,nullptr)) text.clear();
        else text.resize(read);
    }
    CloseHandle(file);
    return text;
}
unsigned long long UnixNow() {
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{}; value.LowPart = ft.dwLowDateTime; value.HighPart = ft.dwHighDateTime;
    return value.QuadPart/10000000ULL - 11644473600ULL;
}
struct Job { Provider provider; Config config; unsigned long long generation; HMODULE module = nullptr; };
DWORD WINAPI Worker(void* input) {
    std::unique_ptr<Job> job(static_cast<Job*>(input));
    Value result;
    std::wstring error;
    bool ok = false;
    try { ok = Fetch(job->provider,job->config,result,error); }
    catch (...) { error = L"Balance query failed unexpectedly"; }
    auto& state = Data();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.running = false;
        if (job->generation == state.generation) {
            state.snapshot.loading = false; state.snapshot.error = error;
            if (ok) { state.snapshot.value = result; state.snapshot.hasValue = true; state.snapshot.updatedAt = UnixNow(); }
            state.nextQuery = GetTickCount64() + job->config.refreshSeconds*1000ULL;
        }
    }
    HMODULE module = job->module;
    job.reset();
    FreeLibraryAndExitThread(module,0);
    return 0;
}
class BalanceItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"CC\u4f59\u989d"; }
    const wchar_t* GetItemId() const override { return L"ccswitch_provider_balance"; }
    const wchar_t* GetItemLableText() const override { return L""; }
    const wchar_t* GetItemValueText() const override {
        static thread_local std::wstring value;
        auto lines = DisplayLines(GetSnapshot(),GetDisplayConfig());
        value.clear();
        for (const auto& line : lines) { if (!value.empty()) value += L" "; value += line; }
        return value.c_str();
    }
    const wchar_t* GetItemValueSampleText() const override { return L"Provider $9999.99"; }
    bool IsCustomDraw() const override { return true; }
    int GetItemWidth() const override { return int(GetDisplayConfig().width); }
    int GetItemWidthEx(void* hdc) const override { return MulDiv(GetItemWidth(),GetDeviceCaps((HDC)hdc,LOGPIXELSX),96); }
    void DrawItem(void* dc,int x,int y,int w,int h,bool dark) override {
        DrawSnapshot(dc,x,y,w,h,dark,GetSnapshot(),GetDisplayConfig());
    }
};
} // namespace
void Refresh(const std::string& dbPath) {
    auto& state = Data();
    auto now = GetTickCount64();
    { std::lock_guard<std::mutex> lock(state.mutex); if (state.lastPoll && now-state.lastPoll < 2000) return; state.lastPoll = now; }
    const Config config = ReadConfigFile(ConfigPath());
    Provider provider;
    std::wstring error;
    sqlite3* db = nullptr;
    bool ok = sqlite3_open_v2(dbPath.c_str(),&db,SQLITE_OPEN_READONLY,nullptr) == SQLITE_OK;
    if (ok) { sqlite3_busy_timeout(db,50); ok = LoadProvider(db,config,SettingsFile(dbPath),provider,error); }
    else error = L"Cannot open cc-switch database";
    if (db) sqlite3_close(db);
    std::unique_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.config = config;
        std::string identity = config.app + '\0' + provider.id + '\0' + provider.meta + '\0' + provider.settings + '\0' +
            config.proxyMode + '\0' + config.proxyServer + '\0' + std::to_string(config.planIndex) + ':' + std::to_string(config.refreshSeconds);
        if (identity != state.identity) {
            state.identity = std::move(identity); ++state.generation;
            state.snapshot = {}; state.snapshot.providerId = provider.id; state.snapshot.name = provider.name;
            state.nextQuery = 0;
        }
        state.snapshot.name = provider.name; state.snapshot.app = config.app;
        if (!ok) { state.snapshot.error = error; state.snapshot.loading = false; return; }
        if (state.running || now < state.nextQuery) return;
        state.running = true; state.snapshot.loading = !state.snapshot.hasValue;
        job = std::make_unique<Job>(Job{provider,config,state.generation});
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&job->module)) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.running = false; state.snapshot.loading = false; state.snapshot.error = L"Cannot reference plugin module"; return;
    }
    HMODULE module = job->module;
    HANDLE thread = CreateThread(nullptr,0,Worker,job.get(),0,nullptr);
    if (thread) { job.release(); CloseHandle(thread); }
    else {
        FreeLibrary(module);
        std::lock_guard<std::mutex> lock(state.mutex);
        state.running = false; state.snapshot.loading = false; state.snapshot.error = L"Cannot start balance query worker";
    }
}
void ForceRefresh(const std::string& dbPath) {
    { auto& state = Data(); std::lock_guard<std::mutex> lock(state.mutex); state.lastPoll = 0; state.nextQuery = 0; }
    Refresh(dbPath);
}
Snapshot GetSnapshot() { auto& state = Data(); std::lock_guard<std::mutex> lock(state.mutex); return state.snapshot; }
Config GetDisplayConfig() { auto& state = Data(); std::lock_guard<std::mutex> lock(state.mutex); return state.config; }
std::wstring Tooltip() { return FormatTooltip(GetSnapshot(),GetDisplayConfig()); }
IPluginItem* Item() { static BalanceItem item; return &item; }
} // namespace balance
