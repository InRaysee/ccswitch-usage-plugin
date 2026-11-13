#include "ProviderBalance.h"
#include <windows.h>

namespace balance {
namespace {
// Provider names, plans and errors must not create extra tooltip rows.
std::wstring SingleLine(const std::wstring& value) {
    std::wstring text;
    bool space = false;
    for (auto ch : value) {
        if (ch <= L' ' || ch == 127) { space = !text.empty(); continue; }
        if (space) text += L' ';
        text += ch;
        space = false;
    }
    return text;
}
}
std::wstring FormatTooltip(const Snapshot& snapshot, const Config& config) {
    // Always identify amounts in tooltips, independently of the taskbar style.
    Config full = config;
    full.showLabels = false;
    full.showUnit = true;
    Snapshot clean = snapshot;
    clean.error.clear();
    auto field = [&](Field selected) { return SingleLine(FormatField(selected,clean,full)); };
    auto name = SingleLine(Utf8(snapshot.name));
    std::wstring text = L"[\u4f9b\u5e94\u5546\u4f59\u989d]\n" + AppName(snapshot.app.empty() ? config.app : snapshot.app) +
        L" / " + (name.empty() ? L"--" : name);
    text += L"\n\u4f59\u989d: " + field(Field::Remaining);
    if (snapshot.hasValue && !snapshot.error.empty()) text += L" (\u4e0a\u6b21\u6210\u529f\u503c)";
    if (snapshot.hasValue && config.tooltipDetails) {
        const auto& v = snapshot.value;
        if (v.hasUsed && v.hasTotal)
            text += L"\n\u5df2\u7528 / \u603b\u989d: " + field(Field::UsedTotal);
        else if (v.hasUsed) text += L"\n\u5df2\u7528: " + field(Field::Used);
        else if (v.hasTotal) text += L"\n\u603b\u989d: " + field(Field::Total);
        auto percent = field(Field::Percent);
        if (percent != L"--") text += L"\n\u5269\u4f59\u6bd4\u4f8b: " + percent;
        auto plan = SingleLine(Utf8(v.plan));
        if (!plan.empty()) text += L"\n\u5957\u9910: " + plan;
    }
    if (config.tooltipTime && snapshot.updatedAt) {
        ULARGE_INTEGER t{}; t.QuadPart = (snapshot.updatedAt+11644473600ULL)*10000000ULL;
        FILETIME utc{t.LowPart,t.HighPart}, local{}; SYSTEMTIME st{};
        if (FileTimeToLocalFileTime(&utc,&local) && FileTimeToSystemTime(&local,&st)) {
            wchar_t time[80];
            swprintf(time,80,L"\n\u4e0a\u6b21\u6210\u529f: %04u-%02u-%02u %02u:%02u:%02u",
                st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
            text += time;
        }
    }
    if (config.tooltipErrors && !snapshot.error.empty())
        text += L"\n\u67e5\u8be2\u5931\u8d25: " + SingleLine(snapshot.error);
    if (IsWarning(snapshot,config)) text += L"\n\u63d0\u9192: \u4f59\u989d\u5df2\u8fbe\u5230\u4f4e\u4f59\u989d\u9608\u503c";
    return text;
}
} // namespace balance
