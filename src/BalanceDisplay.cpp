#include "ProviderBalance.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace balance {
namespace {
const char* keys[] = {"none","provider","app_provider","remaining","used","total","percent","plan","used_total","balance_percent"};
std::wstring NumberText(double value, unsigned decimals) {
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(std::min(decimals, 6u)) << value;
    return out.str();
}
std::wstring Money(double number, const Value& value, const Config& config) {
    auto text = NumberText(number, config.decimals);
    if (!config.showUnit || value.unit.empty()) return text;
    std::string unit = value.unit;
    for (auto& ch : unit) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (unit == "usd" || unit == "$") return L"$" + text;
    if (unit == "cny" || unit == "rmb" || unit == "\xc2\xa5" || unit == "\xe5\x85\x83") return L"\u00a5" + text;
    return text + L" " + Utf8(value.unit);
}
}
const char* FieldKey(Field field) {
    const auto index = static_cast<unsigned>(field);
    return index < sizeof(keys)/sizeof(*keys) ? keys[index] : "none";
}
Field ParseField(const std::string& key, Field fallback) {
    for (unsigned i = 0; i < sizeof(keys)/sizeof(*keys); ++i)
        if (key == keys[i]) return static_cast<Field>(i);
    return fallback;
}
std::wstring AppName(const std::string& app) {
    if (app == "codex") return L"Codex";
    if (app == "claude_desktop") return L"Claude Desktop";
    if (app == "hermes") return L"Hermes";
    if (app == "agy") return L"Antigravity CLI";
    if (app == "copilot") return L"Copilot CLI";
    if (app == "claude") return L"Claude Code";
    if (app == "gemini") return L"Gemini";
    if (app == "opencode") return L"OpenCode";
    if (app == "openclaw") return L"OpenClaw";
    if (app == "grokbuild") return L"GrokBuild";
    return Utf8(app);
}
std::wstring FormatField(Field field, const Snapshot& s, const Config& c) {
    const auto& v = s.value;
    const auto name = !c.displayName.empty() ? Utf8(c.displayName) :
        !s.name.empty() ? Utf8(s.name) : AppName(c.app);
    if (field == Field::None) return {};
    if (field == Field::Provider) return name;
    if (field == Field::AppProvider) return AppName(c.app) + L" / " + name;
    const wchar_t* label = field == Field::Used ? L"\u5df2\u7528 " : field == Field::Total ? L"\u603b\u989d " :
        field == Field::Percent ? L"\u5269\u4f59 " : field == Field::Plan ? L"\u5957\u9910 " :
        field == Field::UsedTotal ? L"\u5df2\u7528/\u603b\u989d " : L"\u4f59\u989d ";
    auto prefix = c.showLabels ? std::wstring(label) : std::wstring();
    if (!s.hasValue) return prefix + (s.loading ? L"\u67e5\u8be2\u4e2d..." : L"--");
    std::wstring value;
    switch (field) {
    case Field::Remaining: value = Money(v.remaining, v, c); break;
    case Field::Used: value = v.hasUsed ? Money(v.used, v, c) : L"--"; break;
    case Field::Total: value = v.hasTotal ? Money(v.total, v, c) : L"--"; break;
    case Field::Percent:
        value = v.hasTotal && v.total > 0 && std::isfinite(v.remaining/v.total*100)
            ? NumberText(v.remaining/v.total*100, c.decimals) + L"%" : L"--"; break;
    case Field::Plan: value = v.plan.empty() ? L"--" : Utf8(v.plan); break;
    case Field::UsedTotal:
        value = (v.hasUsed ? Money(v.used, v, c) : L"--") + L" / " +
            (v.hasTotal ? Money(v.total, v, c) : L"--"); break;
    case Field::BalancePercent:
        value = Money(v.remaining, v, c);
        if (v.hasTotal && v.total > 0 && std::isfinite(v.remaining/v.total*100))
            value += L" (" + NumberText(v.remaining/v.total*100, c.decimals) + L"%)";
        break;
    default: break;
    }
    if (!s.error.empty() && field != Field::Plan) value += L" *";
    return prefix + value;
}
std::vector<std::wstring> DisplayLines(const Snapshot& s, const Config& c) {
    std::vector<std::wstring> lines;
    for (auto field : {c.line1, c.line2}) if (field != Field::None) lines.push_back(FormatField(field,s,c));
    return lines;
}
bool IsWarning(const Snapshot& s, const Config& c) {
    return s.hasValue && c.warnEnabled && s.value.remaining <= c.warnBelow;
}
void DrawSnapshot(void* hdc, int x, int y, int w, int h, bool dark, const Snapshot& s, const Config& c) {
    auto lines = DisplayLines(s,c);
    if (w <= 6 || h < 12 || lines.empty()) return;
    HDC dc = static_cast<HDC>(hdc);
    const int lineHeight = h / int(lines.size());
    LOGFONTW lf{};
    HFONT font = nullptr;
    auto current = GetCurrentObject(dc, OBJ_FONT);
    if (current && GetObjectW(current,sizeof(lf),&lf)) {
        lf.lfHeight = -std::max(6,std::min(lineHeight-1,int(std::abs(lf.lfHeight))));
        font = CreateFontIndirectW(&lf);
    }
    auto oldFont = font ? SelectObject(dc,font) : nullptr;
    int oldBk = SetBkMode(dc,TRANSPARENT);
    auto oldColor = GetTextColor(dc);
    const UINT align = c.alignment == 1 ? DT_CENTER : c.alignment == 2 ? DT_RIGHT : DT_LEFT;
    for (unsigned i = 0; i < lines.size(); ++i) {
        Field field = c.line1 != Field::None && i == 0 ? c.line1 : c.line2;
        bool attention = field != Field::Provider && field != Field::AppProvider &&
            field != Field::Plan && s.hasValue && (!s.error.empty() || IsWarning(s,c));
        SetTextColor(dc, attention ? (dark ? RGB(255,195,90) : RGB(155,75,0)) :
            (dark ? RGB(255,255,255) : RGB(20,20,20)));
        RECT rect{x+3,y+int(i)*lineHeight,x+w-3,y+int(i+1)*lineHeight};
        DrawTextW(dc,lines[i].c_str(),-1,&rect,align|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    SetTextColor(dc,oldColor); SetBkMode(dc,oldBk);
    if (font) { SelectObject(dc,oldFont); DeleteObject(font); }
}
} // namespace balance
