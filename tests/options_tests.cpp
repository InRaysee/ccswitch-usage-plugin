#include "ProviderBalance.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
namespace balance { std::wstring TestConfigPath(); }
#define ConfigPath TestConfigPath
#include "../src/OptionsDialog.cpp"
#undef ConfigPath

namespace balance {
std::wstring testIni;
unsigned refreshCount = 0;
std::wstring TestConfigPath() { return testIni; }
Snapshot GetSnapshot() { return {}; }
void Refresh(const std::string&) {}
void ForceRefresh(const std::string&) { ++refreshCount; }
}
void Require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    using namespace balance;
    wchar_t directory[MAX_PATH]{}, dbPath[MAX_PATH]{}, iniPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH,directory);
    GetTempFileNameW(directory,L"ccd",0,dbPath);
    GetTempFileNameW(directory,L"cci",0,iniPath); DeleteFileW(iniPath);
    testIni = iniPath;
    HWND wnd = nullptr;
    try {
        sqlite3* db = nullptr;
        Require(sqlite3_open(Utf8(std::wstring(dbPath)).c_str(),&db) == SQLITE_OK,"open fixture DB");
        const char* fixture = R"SQL(CREATE TABLE providers(id TEXT,app_type TEXT,name TEXT,meta TEXT);
INSERT INTO providers VALUES('one','codex','Same Name','{"usage_script":{"enabled":true,"code":"({})"}}');
INSERT INTO providers VALUES('two','codex','Same Name','{}');
INSERT INTO providers VALUES('claude-one','claude','Claude Provider','{"usage_script":{"enabled":true,"code":"({})"}}');)SQL";
        Require(sqlite3_exec(db,fixture,nullptr,nullptr,nullptr) == SQLITE_OK,"fixture DB");
        sqlite3_close(db);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_TAB_CLASSES}; InitCommonControlsEx(&common);
        Dialog d; d.dbPath = Utf8(std::wstring(dbPath)); d.saved.providerId = "one";
        wnd = CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_OPTIONS),nullptr,OptionsProc,reinterpret_cast<LPARAM>(&d));
        Require(wnd != nullptr,"create real options dialog resource");
        Require(TabCtrl_GetItemCount(GetDlgItem(wnd,IDC_TABS)) == 3,"three settings pages");
        for (auto page : d.pages) Require(IsWindow(page),"create settings page resource");
        Require(Selection(d,IDC_PROVIDER) == 1 && d.providerIds.size() == 3,"fixed provider must be selected by ID");
        Require(Text(d,IDC_PROVIDER).find(L"one") != std::wstring::npos,"provider IDs disambiguate names");
        Select(d,IDC_APP,1); Command(d,IDC_APP,CBN_SELCHANGE);
        Require(d.providerIds.size() == 2 && d.providerIds[1] == "claude-one" && Selection(d,IDC_PROVIDER) == 0,"application changes filter provider list and reset to follow");
        Select(d,IDC_PROVIDER,1); Command(d,IDC_PROVIDER,CBN_SELCHANGE);
        Select(d,IDC_LINE1,int(Field::Used)); Select(d,IDC_LINE2,int(Field::Total));
        Text(d,IDC_DECIMALS,L"3"); Text(d,IDC_WIDTH,L"250");
        Config draft; Require(Collect(d,draft,false),"collect settings");
        Require(draft.app == "claude" && draft.providerId == "claude-one" && draft.line1 == Field::Used && draft.line2 == Field::Total,"selected values must map to configuration");
        Require(Apply(d) && d.changed && refreshCount == 1,"apply persists and refreshes");
        Require(ReadConfigFile(testIni).providerId == "claude-one" && ReadConfigFile(testIni).width == 250,"persist settings selected in dialog");
        Text(d,IDC_WIDTH,L"300");
        Require(ReadConfigFile(testIni).width == 250,"editing must not save without apply");
        Fill(d,Config{});
        Require(ReadConfigFile(testIni).app == "claude" && ReadConfigFile(testIni).width == 250,"restore defaults must remain a draft");
        Select(d,IDC_LINE1,int(Field::None)); Select(d,IDC_LINE2,int(Field::None));
        Require(!Collect(d,draft,false),"reject zero visible fields");
        Fill(d,d.saved); Text(d,IDC_INTERVAL,L"1");
        Require(!Collect(d,draft,false),"reject out of range refresh interval");
        Fill(d,d.saved); Select(d,IDC_PROXY,2); Text(d,IDC_PROXY_SERVER,L"");
        Require(!Collect(d,draft,false),"manual proxy requires address");
        Fill(d,d.saved); FillProviders(d,"deleted-provider");
        Require(SelectedProvider(d) == "deleted-provider","do not silently change missing fixed provider");
        // An offscreen paint exercises the shared taskbar preview renderer.
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 600; info.bmiHeader.biHeight = -80;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr; HBITMAP bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        auto old = SelectObject(dc,bitmap);
        DrawSnapshot(dc,0,0,250,60,false,d.preview,d.previewConfig);
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
        DestroyWindow(wnd); wnd = nullptr;
        DeleteFileW(dbPath); DeleteFileW(iniPath);
        std::cout << "PASS: native dialog creation, app/provider selection, draft/apply behavior, validation and preview rendering\n";
        return 0;
    } catch (const std::exception& e) {
        if (wnd) DestroyWindow(wnd);
        DeleteFileW(dbPath); DeleteFileW(iniPath);
        std::cerr << "FAIL: " << e.what() << "; Win32=" << GetLastError() << '\n'; return 1;
    }
}
