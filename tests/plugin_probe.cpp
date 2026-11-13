#include "PluginInterface.h"
#include <windows.h>
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3) { std::cerr << "Usage: plugin_probe <dll> [--options|--tooltip]\n"; return 1; }
    HMODULE dll=LoadLibraryA(argv[1]);
    if(!dll) { std::cerr << "LoadLibrary failed: " << GetLastError() << '\n'; return 1; }
    auto get=(ITMPlugin*(*)())GetProcAddress(dll,"TMPluginGetInstance");
    if(!get) return 2;
    auto* plugin=get();
    if (argc == 3 && std::string(argv[2]) == "--options") return plugin->ShowOptionsDialog(nullptr) == ITMPlugin::OR_OPTION_NOT_PROVIDED ? 5 : 0;
    auto* item=plugin->GetItem(1);
    if(!item || plugin->GetItem(2)) return 3;
    std::wstring text;
    for(int i=0;i<45;++i) {
        plugin->DataRequired();
        text=item->GetItemValueText();
        auto tooltip=std::wstring(plugin->GetTooltipInfo());
        if(text.find(L'$')!=std::wstring::npos || tooltip.find(L"failed")!=std::wstring::npos || tooltip.find(L"HTTP ")!=std::wstring::npos) break;
        Sleep(1000);
    }
    HDC dc=CreateCompatibleDC(nullptr);
    HDC screen=GetDC(nullptr);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,400,80);
    ReleaseDC(nullptr,screen);
    HGDIOBJ old=SelectObject(dc,bitmap);
    item->DrawItem(dc,0,0,300,64,false);
    int width=item->GetItemWidthEx(dc);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    int n=WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,nullptr,0,nullptr,nullptr);
    std::string utf8(n,0);WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,utf8.data(),n,nullptr,nullptr);
    std::cout << "DLL loaded; balance item: " << utf8.c_str() << "; measured width=" << width << '\n';
    if (argc == 3 && std::string(argv[2]) == "--tooltip") {
        const auto* tooltip = plugin->GetTooltipInfo();
        int size = WideCharToMultiByte(CP_UTF8,0,tooltip,-1,nullptr,0,nullptr,nullptr);
        std::string value(size,0);
        WideCharToMultiByte(CP_UTF8,0,tooltip,-1,value.data(),size,nullptr,nullptr);
        std::cout << value.c_str() << '\n';
    }
    return text.find(L'$')!=std::wstring::npos ? 0 : 4;
}
