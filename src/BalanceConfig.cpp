#include "ProviderBalance.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace balance {
std::wstring ConfigPath() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ConfigPath),&module);
    wchar_t path[32768]{};
    DWORD size = GetModuleFileNameW(module,path,32768);
    if (!size || size >= 32768) return {};
    std::wstring file(path,size);
    return file.substr(0,file.find_last_of(L"\\/")+1) + L"CCSwitchUsageBalance.ini";
}
namespace {
std::wstring Read(const std::wstring& file, const wchar_t* key, const wchar_t* fallback) {
    wchar_t text[2048]{};
    GetPrivateProfileStringW(L"Balance",key,fallback,text,2048,file.c_str());
    return text;
}
unsigned Integer(const std::wstring& file, const wchar_t* key, unsigned fallback, unsigned min, unsigned max) {
    auto text = Read(file,key,L"");
    try {
        size_t end = 0;
        if (text.empty() || text.front() == L'-') return fallback;
        auto number = std::stoul(text,&end);
        if (end != text.size()) return fallback;
        return unsigned(std::clamp(number,static_cast<unsigned long>(min),static_cast<unsigned long>(max)));
    } catch (...) { return fallback; }
}
bool Flag(const std::wstring& file, const wchar_t* key, bool fallback) {
    return Integer(file,key,fallback ? 1 : 0,0,1) != 0;
}
}
Config ReadConfigFile(const std::wstring& file) {
    Config c;
    c.app = Utf8(Read(file,L"app_type",L"codex"));
    c.providerId = Utf8(Read(file,L"provider_id",L""));
    c.proxyMode = Utf8(Read(file,L"proxy_mode",L"system"));
    c.proxyServer = Utf8(Read(file,L"proxy_server",L""));
    if (c.proxyMode != "manual" && c.proxyMode != "direct") c.proxyMode = "system";
    c.refreshSeconds = Integer(file,L"refresh_seconds",60,15,3600);
    c.width = Integer(file,L"width",150,80,600);
    c.planIndex = Integer(file,L"plan_index",0,0,1000);
    c.decimals = Integer(file,L"decimals",2,0,6);
    c.alignment = Integer(file,L"alignment",0,0,2);
    c.line1 = ParseField(Utf8(Read(file,L"line1",L"provider")),Field::Provider);
    c.line2 = ParseField(Utf8(Read(file,L"line2",L"remaining")),Field::Remaining);
    if (c.line1 == Field::None && c.line2 == Field::None) c.line2 = Field::Remaining;
    c.displayName = Utf8(Read(file,L"display_name",L""));
    c.showUnit = Flag(file,L"show_unit",true);
    c.showLabels = Flag(file,L"show_labels",false);
    c.warnEnabled = Flag(file,L"warn_enabled",false);
    std::wistringstream number(Read(file,L"warn_below",L"10"));
    number.imbue(std::locale::classic());
    double threshold = 10;
    if (number >> threshold && number.eof() && std::isfinite(threshold) && threshold >= 0) c.warnBelow = threshold;
    c.tooltipUsage = Flag(file,L"tooltip_usage",true);
    c.tooltipDetails = Flag(file,L"tooltip_details",true);
    c.tooltipTime = Flag(file,L"tooltip_time",true);
    c.tooltipErrors = Flag(file,L"tooltip_errors",true);
    return c;
}
bool SaveConfigFile(const std::wstring& file, const Config& c, std::wstring& error) {
    const auto temp = file + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    DWORD attributes = GetFileAttributesW(file.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
        error = L"Configuration file is read-only"; return false;
    }
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (!CopyFileW(file.c_str(),temp.c_str(),TRUE)) {
            error = L"\u65e0\u6cd5\u521b\u5efa\u914d\u7f6e\u526f\u672c\uff0c\u8bf7\u68c0\u67e5\u63d2\u4ef6\u76ee\u5f55\u5199\u5165\u6743\u9650\u3002"; return false;
        }
    } else {
        HANDLE handle = CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (handle == INVALID_HANDLE_VALUE) { error = L"Cannot create configuration file"; return false; }
        wchar_t bom = 0xfeff; DWORD written = 0;
        bool ok = WriteFile(handle,&bom,sizeof(bom),&written,nullptr) && written == sizeof(bom);
        CloseHandle(handle);
        if (!ok) { DeleteFileW(temp.c_str()); error = L"Cannot initialize configuration file"; return false; }
    }
    std::vector<std::pair<std::wstring,std::wstring>> entries = {
        {L"app_type",Utf8(c.app)},{L"provider_id",Utf8(c.providerId)},
        {L"proxy_mode",Utf8(c.proxyMode)},{L"proxy_server",Utf8(c.proxyServer)},
        {L"line1",Utf8(std::string(FieldKey(c.line1)))},{L"line2",Utf8(std::string(FieldKey(c.line2)))},
        {L"display_name",Utf8(c.displayName)}
    };
    auto add = [&](const wchar_t* key, unsigned n) { entries.emplace_back(key,std::to_wstring(n)); };
    add(L"refresh_seconds",c.refreshSeconds); add(L"width",c.width); add(L"plan_index",c.planIndex);
    add(L"decimals",c.decimals); add(L"alignment",c.alignment);
    add(L"show_unit",c.showUnit); add(L"show_labels",c.showLabels); add(L"warn_enabled",c.warnEnabled);
    add(L"tooltip_usage",c.tooltipUsage); add(L"tooltip_details",c.tooltipDetails);
    add(L"tooltip_time",c.tooltipTime); add(L"tooltip_errors",c.tooltipErrors);
    std::wostringstream threshold; threshold.imbue(std::locale::classic());
    threshold << std::setprecision(17) << c.warnBelow;
    entries.emplace_back(L"warn_below",threshold.str());
    bool ok = true;
    for (const auto& entry : entries)
        if (!WritePrivateProfileStringW(L"Balance",entry.first.c_str(),entry.second.c_str(),temp.c_str())) { ok = false; break; }
    // A cache flush returns zero even when it succeeds (Win32 API contract).
    if (ok) WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
    if (ok) ok = MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) { DeleteFileW(temp.c_str()); error = L"\u4fdd\u5b58\u5931\u8d25\uff0c\u539f\u914d\u7f6e\u672a\u88ab\u66ff\u6362\u3002\u8bf7\u68c0\u67e5\u6587\u4ef6\u662f\u5426\u53ea\u8bfb\u6216\u76ee\u5f55\u662f\u5426\u53ef\u5199\u3002"; return false; }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,file.c_str());
    error.clear();
    return true;
}
} // namespace balance
