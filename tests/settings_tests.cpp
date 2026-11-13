#include "ProviderBalance.h"
#include <windows.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace balance;
void Require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    wchar_t directory[MAX_PATH]{}, filename[MAX_PATH]{};
    GetTempPathW(MAX_PATH,directory); GetTempFileNameW(directory,L"ccs",0,filename);
    DeleteFileW(filename);
    try {
        Config c;
        c.app = "claude"; c.providerId = "provider-with-specific-id";
        c.line1 = Field::UsedTotal; c.line2 = Field::BalancePercent;
        c.width = 333; c.decimals = 4; c.alignment = 2;
        c.displayName = Utf8(std::wstring(L"\u6d4b\u8bd5\u540d\u79f0"));
        c.proxyMode = "manual"; c.proxyServer = "127.0.0.1:8899";
        c.refreshSeconds = 25; c.planIndex = 2; c.showUnit = false; c.showLabels = true;
        c.warnEnabled = true; c.warnBelow = 0.125;
        c.tooltipUsage = false; c.tooltipDetails = false; c.tooltipTime = false; c.tooltipErrors = false;
        std::wstring error;
        if (!SaveConfigFile(filename,c,error)) { std::cerr << "Save error: " << Utf8(error) << "; Win32=" << GetLastError() << "\n"; throw std::runtime_error("saving settings failed"); }
        WritePrivateProfileStringW(L"Other",L"keep",L"unchanged",filename);
        Config loaded = ReadConfigFile(filename);
        Require(loaded.app == c.app && loaded.providerId == c.providerId && loaded.displayName == c.displayName,"application/provider/Unicode alias round-trip");
        Require(loaded.line1 == c.line1 && loaded.line2 == c.line2 && loaded.width == 333 && loaded.decimals == 4 && loaded.alignment == 2,"display settings round-trip");
        Require(loaded.proxyMode == "manual" && loaded.proxyServer == c.proxyServer && loaded.refreshSeconds == 25 && loaded.planIndex == 2,"network settings round-trip");
        Require(!loaded.showUnit && loaded.showLabels && loaded.warnEnabled && loaded.warnBelow == 0.125,"format/warning settings round-trip");
        Require(!loaded.tooltipUsage && !loaded.tooltipDetails && !loaded.tooltipTime && !loaded.tooltipErrors,"tooltip settings round-trip");
        Require(SaveConfigFile(filename,c,error),"second save failed");
        wchar_t preserved[32]{};
        GetPrivateProfileStringW(L"Other",L"keep",L"",preserved,32,filename);
        Require(std::wstring(preserved) == L"unchanged","saving must preserve unrelated INI sections");
        WritePrivateProfileStringW(L"Balance",L"width",L"9999",filename);
        WritePrivateProfileStringW(L"Balance",L"decimals",L"-1",filename);
        WritePrivateProfileStringW(L"Balance",L"line1",L"none",filename);
        WritePrivateProfileStringW(L"Balance",L"line2",L"none",filename);
        loaded = ReadConfigFile(filename);
        Require(loaded.width == 600 && loaded.decimals == 2 && loaded.line2 == Field::Remaining,"invalid INI values must normalize safely");
        HANDLE locked = CreateFileW(filename,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(locked != INVALID_HANDLE_VALUE,"lock fixture");
        c.width = 222;
        bool saved = SaveConfigFile(filename,c,error);
        CloseHandle(locked);
        Require(!saved && ReadConfigFile(filename).width == 600,"failed save must retain original settings");
        Snapshot s; s.app = "codex"; s.name = "Provider"; s.hasValue = true;
        s.value = {75,"USD","Plan A",25,100,true,true};
        Config display;
        Require(FormatField(Field::Used,s,display) == L"$25.00","used display");
        Require(FormatField(Field::Total,s,display) == L"$100.00","total display");
        Require(FormatField(Field::Percent,s,display) == L"75.00%","percentage display");
        Require(FormatField(Field::BalancePercent,s,display) == L"$75.00 (75.00%)","combined display");
        display.line1 = Field::None;
        Require(DisplayLines(s,display).size() == 1,"single line layout");
        display.showUnit = false; display.decimals = 0;
        Require(FormatField(Field::Remaining,s,display) == L"75","precision and hidden unit");
        display.warnEnabled = true; display.warnBelow = 75;
        Require(IsWarning(s,display),"warning includes equality");
        s.value.hasUsed = false; s.value.hasTotal = false;
        Require(FormatField(Field::Used,s,display) == L"--" && FormatField(Field::Percent,s,display) == L"--","missing fields must not become zero");
        s.value.hasTotal = true; s.value.total = 0;
        Require(FormatField(Field::Percent,s,display) == L"--","zero total cannot produce percentage");
        s.error = L"HTTP 403";
        Require(FormatField(Field::Remaining,s,display) == L"75 *","stale balance remains marked");
        DeleteFileW(filename);
        std::cout << "PASS: settings persistence, failed saves, Unicode, display fields, precision, missing data and warnings\n";
        return 0;
    } catch (const std::exception& e) { DeleteFileW(filename); std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
