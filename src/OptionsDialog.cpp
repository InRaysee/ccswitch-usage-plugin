#include "ProviderBalance.h"
#include "OptionsResource.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <tuple>

namespace balance {
namespace {
const wchar_t* fieldNames[] = {L"\u4e0d\u663e\u793a",L"\u4f9b\u5e94\u5546\u540d\u79f0",L"\u5e94\u7528 + \u4f9b\u5e94\u5546",
    L"\u5269\u4f59\u4f59\u989d",L"\u5df2\u7528\u989d\u5ea6",L"\u603b\u989d\u5ea6",L"\u5269\u4f59\u767e\u5206\u6bd4",L"\u5957\u9910\u540d\u79f0",
    L"\u5df2\u7528 / \u603b\u989d",L"\u4f59\u989d + \u767e\u5206\u6bd4"};
struct Dialog {
    HWND window = nullptr, pages[3]{};
    Config saved, previewConfig;
    Snapshot preview;
    std::string dbPath;
    std::vector<ProviderChoice> providers;
    std::vector<std::string> apps, providerIds;
    std::wstring listError;
    bool filling = false, changed = false;
};
Dialog* Context(HWND wnd) { return reinterpret_cast<Dialog*>(GetWindowLongPtrW(wnd,DWLP_USER)); }
HWND Control(Dialog& d,int id) {
    for (auto page : d.pages) if (auto control = GetDlgItem(page,id)) return control;
    return GetDlgItem(d.window,id);
}
std::wstring Text(Dialog& d,int id) {
    HWND control = Control(d,id);
    int count = GetWindowTextLengthW(control);
    std::wstring text(count+1,0);
    GetWindowTextW(control,text.data(),count+1); text.resize(count);
    return text;
}
void Text(Dialog& d,int id,const std::wstring& value) { SetWindowTextW(Control(d,id),value.c_str()); }
int Selection(Dialog& d,int id) { return int(SendMessageW(Control(d,id),CB_GETCURSEL,0,0)); }
void Select(Dialog& d,int id,int value) { SendMessageW(Control(d,id),CB_SETCURSEL,value,0); }
void Add(Dialog& d,int id,const std::wstring& text) { SendMessageW(Control(d,id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str())); }
void Check(Dialog& d,int id,bool checked) { SendMessageW(Control(d,id),BM_SETCHECK,checked ? BST_CHECKED : BST_UNCHECKED,0); }
bool Checked(Dialog& d,int id) { return SendMessageW(Control(d,id),BM_GETCHECK,0,0) == BST_CHECKED; }
bool Same(const Config& a,const Config& b) {
    return std::tie(a.app,a.providerId,a.proxyMode,a.proxyServer,a.refreshSeconds,a.width,a.planIndex,a.decimals,a.alignment,
        a.line1,a.line2,a.displayName,a.showUnit,a.showLabels,a.warnEnabled,a.warnBelow,a.tooltipUsage,a.tooltipDetails,a.tooltipTime,a.tooltipErrors) ==
        std::tie(b.app,b.providerId,b.proxyMode,b.proxyServer,b.refreshSeconds,b.width,b.planIndex,b.decimals,b.alignment,
        b.line1,b.line2,b.displayName,b.showUnit,b.showLabels,b.warnEnabled,b.warnBelow,b.tooltipUsage,b.tooltipDetails,b.tooltipTime,b.tooltipErrors);
}
void ReadProviders(Dialog& d) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(d.dbPath.c_str(),&db,SQLITE_OPEN_READONLY,nullptr) == SQLITE_OK) {
        sqlite3_busy_timeout(db,100);
        ListProviders(db,d.providers,d.listError);
    } else { d.providers.clear(); d.listError = L"\u65e0\u6cd5\u8bfb\u53d6 cc-switch \u6570\u636e\u5e93\uff1b\u4ecd\u53ef\u7f16\u8f91\u663e\u793a\u8bbe\u7f6e\u3002"; }
    if (db) sqlite3_close(db);
}
std::string SelectedApp(Dialog& d) {
    int i = Selection(d,IDC_APP);
    return i >= 0 && size_t(i) < d.apps.size() ? d.apps[i] : d.saved.app;
}
std::string SelectedProvider(Dialog& d) {
    int i = Selection(d,IDC_PROVIDER);
    return i >= 0 && size_t(i) < d.providerIds.size() ? d.providerIds[i] : d.saved.providerId;
}
void ProviderHint(Dialog& d) {
    std::wstring hint = L"\u4ec5\u9009\u62e9\u672c\u63d2\u4ef6\u7684\u76d1\u63a7\u5bf9\u8c61\uff0c\u4e0d\u6539\u53d8 cc-switch \u5f53\u524d\u4f9b\u5e94\u5546\u3002";
    const auto app = SelectedApp(d), id = SelectedProvider(d);
    auto found = std::find_if(d.providers.begin(),d.providers.end(),[&](const ProviderChoice& p){return p.app == app && p.id == id;});
    if (!d.listError.empty()) hint += L"\n" + d.listError;
    else if (found != d.providers.end() && (!found->enabled || !found->hasScript))
        hint += L"\n\u8bf7\u5728 cc-switch \u4e2d\u542f\u7528 request/extractor \u67e5\u8be2\u811a\u672c\u3002";
    else if (id.empty()) hint += L"\n\u8ddf\u968f\u5f53\u524d\u9009\u62e9\uff1b\u4f9b\u5e94\u5546\u9700\u5df2\u542f\u7528\u517c\u5bb9\u7684\u7528\u91cf\u811a\u672c\u3002";
    Text(d,IDC_PROVIDER_HINT,hint);
}
void FillProviders(Dialog& d,const std::string& id) {
    SendMessageW(Control(d,IDC_PROVIDER),CB_RESETCONTENT,0,0);
    d.providerIds = {""};
    Add(d,IDC_PROVIDER,L"\u8ddf\u968f cc-switch \u5f53\u524d\u4f9b\u5e94\u5546");
    int selected = id.empty() ? 0 : -1;
    const auto app = SelectedApp(d);
    for (const auto& p : d.providers) if (p.app == app) {
        auto label = Utf8(p.name.empty() ? p.id : p.name);
        if (!p.enabled) label += L" [\u672a\u542f\u7528\u67e5\u8be2]";
        else if (!p.hasScript) label += L" [\u65e0\u517c\u5bb9\u811a\u672c]";
        // IDs disambiguate providers with identical names; credentials are never loaded here.
        label += L"  (" + Utf8(p.id) + L")";
        if (p.id == id) selected = int(d.providerIds.size());
        d.providerIds.push_back(p.id); Add(d,IDC_PROVIDER,label);
    }
    if (selected < 0) {
        selected = int(d.providerIds.size());
        d.providerIds.push_back(id);
        Add(d,IDC_PROVIDER,L"[\u672a\u627e\u5230] " + Utf8(id));
    }
    Select(d,IDC_PROVIDER,selected);
    ProviderHint(d);
}
void FillApps(Dialog& d,const Config& c) {
    d.apps = {"codex","claude","gemini","opencode","openclaw","grokbuild"};
    for (const auto& p : d.providers) if (std::find(d.apps.begin(),d.apps.end(),p.app) == d.apps.end()) d.apps.push_back(p.app);
    if (std::find(d.apps.begin(),d.apps.end(),c.app) == d.apps.end()) d.apps.push_back(c.app);
    SendMessageW(Control(d,IDC_APP),CB_RESETCONTENT,0,0);
    for (const auto& app : d.apps) Add(d,IDC_APP,AppName(app));
    Select(d,IDC_APP,int(std::find(d.apps.begin(),d.apps.end(),c.app)-d.apps.begin()));
    FillProviders(d,c.providerId);
}
bool Invalid(Dialog& d,int id,const std::wstring& message,bool notify) {
    if (notify) {
        for (int i = 0; i < 3; ++i) if (GetDlgItem(d.pages[i],id)) {
            TabCtrl_SetCurSel(GetDlgItem(d.window,IDC_TABS),i);
            for (int j = 0; j < 3; ++j) ShowWindow(d.pages[j],j == i ? SW_SHOW : SW_HIDE);
        }
        MessageBoxW(d.window,message.c_str(),L"\u8bf7\u68c0\u67e5\u8bbe\u7f6e",MB_OK|MB_ICONWARNING);
        SetFocus(Control(d,id));
    }
    return false;
}
bool Unsigned(Dialog& d,int id,unsigned minimum,unsigned maximum,unsigned& result,bool notify,const wchar_t* name) {
    auto text = Text(d,id);
    try {
        size_t end = 0;
        if (text.empty() || text.front() == L'-') throw 0;
        auto number = std::stoul(text,&end);
        if (end != text.size() || number < minimum || number > maximum) throw 0;
        result = unsigned(number); return true;
    } catch (...) {
        return Invalid(d,id,std::wstring(name)+L"\u9700\u4e3a "+std::to_wstring(minimum)+L" - "+std::to_wstring(maximum)+L" \u7684\u6574\u6570\u3002",notify);
    }
}
bool Collect(Dialog& d,Config& c,bool notify) {
    c = d.saved;
    c.app = SelectedApp(d); c.providerId = SelectedProvider(d);
    int first = Selection(d,IDC_LINE1), second = Selection(d,IDC_LINE2);
    if (first < 0 || second < 0) return false;
    c.line1 = Field(first); c.line2 = Field(second);
    if (c.line1 == Field::None && c.line2 == Field::None)
        return Invalid(d,IDC_LINE1,L"\u8bf7\u81f3\u5c11\u9009\u62e9\u4e00\u4e2a\u663e\u793a\u5b57\u6bb5\u3002",notify);
    if (!Unsigned(d,IDC_INTERVAL,15,3600,c.refreshSeconds,notify,L"\u5237\u65b0\u95f4\u9694") ||
        !Unsigned(d,IDC_PLAN,0,1000,c.planIndex,notify,L"\u5957\u9910\u7d22\u5f15") ||
        !Unsigned(d,IDC_WIDTH,80,600,c.width,notify,L"\u663e\u793a\u5bbd\u5ea6") ||
        !Unsigned(d,IDC_DECIMALS,0,6,c.decimals,notify,L"\u5c0f\u6570\u4f4d\u6570")) return false;
    c.displayName = Utf8(Text(d,IDC_ALIAS));
    c.alignment = unsigned(std::max(0,Selection(d,IDC_ALIGN)));
    c.showUnit = Checked(d,IDC_UNIT); c.showLabels = Checked(d,IDC_LABELS);
    c.tooltipUsage = Checked(d,IDC_TOOLTIP_USAGE); c.tooltipDetails = Checked(d,IDC_TOOLTIP_DETAILS);
    c.tooltipTime = Checked(d,IDC_TOOLTIP_TIME); c.tooltipErrors = Checked(d,IDC_TOOLTIP_ERRORS);
    c.warnEnabled = Checked(d,IDC_WARN);
    std::wistringstream number(Text(d,IDC_THRESHOLD)); number.imbue(std::locale::classic());
    double threshold = 0;
    if (number >> threshold && number.eof() && std::isfinite(threshold) && threshold >= 0 && threshold <= 1e15) c.warnBelow = threshold;
    else if (c.warnEnabled) return Invalid(d,IDC_THRESHOLD,L"\u4f59\u989d\u9608\u503c\u9700\u4e3a 0 - 1000000000000000 \u7684\u6570\u5b57\uff0c\u5c0f\u6570\u70b9\u4f7f\u7528 .\u3002",notify);
    int proxy = Selection(d,IDC_PROXY);
    c.proxyMode = proxy == 1 ? "direct" : proxy == 2 ? "manual" : "system";
    c.proxyServer = Utf8(Text(d,IDC_PROXY_SERVER));
    if (c.proxyMode == "manual" && c.proxyServer.empty())
        return Invalid(d,IDC_PROXY_SERVER,L"\u624b\u52a8\u4ee3\u7406\u8bf7\u586b\u5199\u5730\u5740\uff0c\u4f8b\u5982 127.0.0.1:7890\u3002",notify);
    return true;
}
void UpdatePreview(Dialog& d) {
    EnableWindow(Control(d,IDC_PROXY_SERVER),Selection(d,IDC_PROXY) == 2);
    EnableWindow(Control(d,IDC_THRESHOLD),Checked(d,IDC_WARN));
    Config c;
    if (!Collect(d,c,false)) {
        Text(d,IDC_PREVIEW_NOTE,L"\u8bf7\u5b8c\u6210\u6709\u6548\u7684\u8bbe\u7f6e\uff1b\u81f3\u5c11\u663e\u793a\u4e00\u4e2a\u5b57\u6bb5\u3002"); return;
    }
    auto snapshot = GetSnapshot();
    bool real = snapshot.hasValue && c.app == d.saved.app && c.providerId == d.saved.providerId && c.planIndex == d.saved.planIndex &&
        snapshot.app == c.app && (c.providerId.empty() || c.providerId == snapshot.providerId);
    if (!real) {
        snapshot = {}; snapshot.app = c.app; snapshot.hasValue = true;
        auto found = std::find_if(d.providers.begin(),d.providers.end(),[&](const ProviderChoice& p){return p.app == c.app && p.id == c.providerId;});
        snapshot.name = found == d.providers.end() ? "Example Provider" : found->name;
        snapshot.value = {123.45,"USD","Example Plan",76.55,200,true,true};
    }
    d.preview = snapshot; d.previewConfig = c;
    Text(d,IDC_PREVIEW_NOTE,real ? L"\u5f53\u524d\u67e5\u8be2\u6570\u636e\n\u9884\u89c8\u6309\u8bbe\u5b9a\u5bbd\u5ea6\u622a\u53d6" :
        L"\u793a\u4f8b\u6570\u636e\n\u5e94\u7528\u540e\u67e5\u8be2\u771f\u5b9e\u4f59\u989d");
    InvalidateRect(Control(d,IDC_PREVIEW),nullptr,TRUE);
}
void Fill(Dialog& d,const Config& c) {
    d.filling = true;
    FillApps(d,c);
    Text(d,IDC_INTERVAL,std::to_wstring(c.refreshSeconds)); Text(d,IDC_PLAN,std::to_wstring(c.planIndex));
    Select(d,IDC_PROXY,c.proxyMode == "direct" ? 1 : c.proxyMode == "manual" ? 2 : 0);
    Text(d,IDC_PROXY_SERVER,Utf8(c.proxyServer));
    Select(d,IDC_LINE1,int(c.line1)); Select(d,IDC_LINE2,int(c.line2)); Select(d,IDC_PRESET,0);
    Text(d,IDC_ALIAS,Utf8(c.displayName)); Text(d,IDC_WIDTH,std::to_wstring(c.width)); Text(d,IDC_DECIMALS,std::to_wstring(c.decimals));
    Select(d,IDC_ALIGN,int(c.alignment)); Check(d,IDC_UNIT,c.showUnit); Check(d,IDC_LABELS,c.showLabels);
    Check(d,IDC_TOOLTIP_USAGE,c.tooltipUsage); Check(d,IDC_TOOLTIP_DETAILS,c.tooltipDetails);
    Check(d,IDC_TOOLTIP_TIME,c.tooltipTime); Check(d,IDC_TOOLTIP_ERRORS,c.tooltipErrors);
    Check(d,IDC_WARN,c.warnEnabled);
    std::wostringstream threshold; threshold.imbue(std::locale::classic()); threshold.precision(15); threshold << c.warnBelow;
    Text(d,IDC_THRESHOLD,threshold.str());
    d.filling = false; UpdatePreview(d);
}
void Status(Dialog& d) {
    auto s = GetSnapshot();
    std::wstring text = AppName(s.app) + L" / " + Utf8(s.name);
    text += L"\n" + (!s.error.empty() ? s.error : s.loading ? L"\u6b63\u5728\u67e5\u8be2..." :
        s.hasValue ? L"\u67e5\u8be2\u6210\u529f\u3002\u9884\u89c8\u4e0e\u4efb\u52a1\u680f\u5171\u7528\u663e\u793a\u903b\u8f91\u3002" : L"\u7b49\u5f85\u67e5\u8be2");
    Text(d,IDC_QUERY_STATUS,text);
}
bool Apply(Dialog& d) {
    Config c; if (!Collect(d,c,true)) return false;
    std::wstring error;
    if (!SaveConfigFile(ConfigPath(),c,error)) {
        MessageBoxW(d.window,error.c_str(),L"\u65e0\u6cd5\u4fdd\u5b58\u8bbe\u7f6e",MB_OK|MB_ICONERROR); return false;
    }
    d.changed = d.changed || !Same(c,d.saved); d.saved = c;
    ForceRefresh(d.dbPath); Status(d); UpdatePreview(d);
    return true;
}
void Command(Dialog& d,int id,int notification) {
    if (d.filling) return;
    if (id == IDC_APP && notification == CBN_SELCHANGE) {
        d.filling = true; FillProviders(d,""); d.filling = false;
    } else if (id == IDC_PROVIDER && notification == CBN_SELCHANGE) ProviderHint(d);
    else if (id == IDC_RELOAD && notification == BN_CLICKED) {
        Config c; if (!Collect(d,c,true)) return;
        d.filling = true; ReadProviders(d); FillApps(d,c); d.filling = false;
    } else if (id == IDC_PRESET && notification == CBN_SELCHANGE) {
        int preset = Selection(d,IDC_PRESET);
        if (preset > 0) {
            d.filling = true;
            Select(d,IDC_LINE1,int(preset == 2 ? Field::None : preset == 3 ? Field::UsedTotal : preset == 4 ? Field::Plan : Field::Provider));
            Select(d,IDC_LINE2,int(preset == 4 ? Field::BalancePercent : Field::Remaining));
            d.filling = false;
        }
    } else if ((id == IDC_LINE1 || id == IDC_LINE2) && notification == CBN_SELCHANGE) Select(d,IDC_PRESET,0);
    UpdatePreview(d);
}
INT_PTR CALLBACK PageProc(HWND wnd,UINT message,WPARAM wParam,LPARAM lParam) {
    if (message == WM_INITDIALOG) { SetWindowLongPtrW(wnd,DWLP_USER,lParam); return TRUE; }
    auto* d = Context(wnd);
    if (d && message == WM_COMMAND) { Command(*d,LOWORD(wParam),HIWORD(wParam)); return TRUE; }
    return FALSE;
}
INT_PTR CALLBACK OptionsProc(HWND wnd,UINT message,WPARAM wParam,LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto& d = *reinterpret_cast<Dialog*>(lParam);
        d.window = wnd; SetWindowLongPtrW(wnd,DWLP_USER,lParam);
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OptionsProc),&module);
        auto tab = GetDlgItem(wnd,IDC_TABS);
        const wchar_t* titles[] = {L"\u6570\u636e\u6e90\u4e0e\u5237\u65b0",L"\u4efb\u52a1\u680f\u663e\u793a",L"\u60ac\u505c\u4fe1\u606f\u4e0e\u63d0\u9192"};
        RECT pageRect{}; GetClientRect(tab,&pageRect); TabCtrl_AdjustRect(tab,FALSE,&pageRect);
        MapWindowPoints(tab,wnd,reinterpret_cast<POINT*>(&pageRect),2);
        for (int i = 0; i < 3; ++i) {
            TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(titles[i]);
            TabCtrl_InsertItem(tab,i,&item);
            d.pages[i] = CreateDialogParamW(module,MAKEINTRESOURCEW(IDD_SOURCE+i),wnd,PageProc,lParam);
            if (!d.pages[i]) { EndDialog(wnd,-1); return TRUE; }
            SetWindowPos(d.pages[i],HWND_TOP,pageRect.left,pageRect.top,pageRect.right-pageRect.left,pageRect.bottom-pageRect.top,SWP_NOACTIVATE);
            ShowWindow(d.pages[i],i == 0 ? SW_SHOW : SW_HIDE);
        }
        for (const auto* name : fieldNames) { Add(d,IDC_LINE1,name); Add(d,IDC_LINE2,name); }
        for (const auto* name : {L"\u81ea\u5b9a\u4e49",L"\u6807\u51c6\uff1a\u4f9b\u5e94\u5546 / \u4f59\u989d",L"\u7b80\u6d01\uff1a\u4ec5\u4f59\u989d",
            L"\u989d\u5ea6\uff1a\u5df2\u7528\u603b\u989d / \u4f59\u989d",L"\u5957\u9910\uff1a\u5957\u9910 / \u4f59\u989d\u767e\u5206\u6bd4"}) Add(d,IDC_PRESET,name);
        for (const auto* name : {L"\u8ddf\u968f\u7cfb\u7edf\u4ee3\u7406",L"\u76f4\u8fde",L"\u624b\u52a8 HTTP \u4ee3\u7406"}) Add(d,IDC_PROXY,name);
        for (const auto* name : {L"\u5de6\u5bf9\u9f50",L"\u5c45\u4e2d",L"\u53f3\u5bf9\u9f50"}) Add(d,IDC_ALIGN,name);
        SendMessageW(Control(d,IDC_ALIAS),EM_SETLIMITTEXT,100,0);
        SendMessageW(Control(d,IDC_PROXY_SERVER),EM_SETLIMITTEXT,1024,0);
        ReadProviders(d); Fill(d,d.saved); Status(d);
        SetTimer(wnd,1,1000,nullptr);
        return TRUE;
    }
    auto* d = Context(wnd);
    if (!d) return FALSE;
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_TABS &&
        reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE) {
        int selected = TabCtrl_GetCurSel(GetDlgItem(wnd,IDC_TABS));
        for (int i = 0; i < 3; ++i) ShowWindow(d->pages[i],i == selected ? SW_SHOW : SW_HIDE);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDOK: if (Apply(*d)) EndDialog(wnd,IDOK); return TRUE;
        case IDCANCEL: EndDialog(wnd,IDCANCEL); return TRUE;
        case IDC_APPLY: Apply(*d); return TRUE;
        case IDC_DEFAULTS: Fill(*d,Config{}); return TRUE;
        case IDC_INI:
            if (GetFileAttributesW(ConfigPath().c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::wstring error;
                if (!SaveConfigFile(ConfigPath(),d->saved,error)) { MessageBoxW(wnd,error.c_str(),L"INI",MB_OK|MB_ICONERROR); return TRUE; }
            }
            if ((INT_PTR)ShellExecuteW(wnd,L"open",ConfigPath().c_str(),nullptr,nullptr,SW_SHOWNORMAL) <= 32)
                MessageBoxW(wnd,ConfigPath().c_str(),L"INI",MB_OK|MB_ICONINFORMATION);
            return TRUE;
        }
    }
    if (message == WM_TIMER) { Refresh(d->dbPath); Status(*d); UpdatePreview(*d); return TRUE; }
    if (message == WM_DRAWITEM && wParam == IDC_PREVIEW) {
        auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        FillRect(item.hDC,&item.rcItem,GetSysColorBrush(COLOR_WINDOW));
        int width = std::min<int>(item.rcItem.right-item.rcItem.left,MulDiv(int(d->previewConfig.width),GetDeviceCaps(item.hDC,LOGPIXELSX),96));
        HGDIOBJ previous = SelectObject(item.hDC,reinterpret_cast<HGDIOBJ>(SendMessageW(wnd,WM_GETFONT,0,0)));
        DrawSnapshot(item.hDC,item.rcItem.left,item.rcItem.top,width,item.rcItem.bottom-item.rcItem.top,false,d->preview,d->previewConfig);
        SelectObject(item.hDC,previous);
        return TRUE;
    }
    if (message == WM_CLOSE) { EndDialog(wnd,IDCANCEL); return TRUE; }
    if (message == WM_DESTROY) { KillTimer(wnd,1); return TRUE; }
    return FALSE;
}
}
ITMPlugin::OptionReturn ShowOptions(void* parent,const std::string& dbPath) {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ShowOptions),&module);
    Dialog dialog; dialog.dbPath = dbPath; dialog.saved = ReadConfigFile(ConfigPath());
    auto result = DialogBoxParamW(module,MAKEINTRESOURCEW(IDD_OPTIONS),static_cast<HWND>(parent),OptionsProc,reinterpret_cast<LPARAM>(&dialog));
    if (result == -1) MessageBoxW(static_cast<HWND>(parent),L"\u65e0\u6cd5\u6253\u5f00\u8bbe\u7f6e\u7a97\u53e3\u3002",L"cc-switch",MB_OK|MB_ICONERROR);
    return dialog.changed ? ITMPlugin::OR_OPTION_CHANGED : ITMPlugin::OR_OPTION_UNCHANGED;
}
} // namespace balance
