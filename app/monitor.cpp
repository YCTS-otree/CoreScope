#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d2d1.h>
#include <dwrite.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>
#include "pawnio_client.h"
#include "monitor.h"
#include "../shared/version.h"

namespace {
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT SampleMessage = WM_APP + 2;
struct Row { WORD group; BYTE cpu; double total = -1, kernel = -1, ghz = -1, time = -1, utility = -1; };
struct Option { const wchar_t* key; const wchar_t* label; int value, minimum, maximum; };
Option options[] = {
    {L"Opacity", L"Opacity (%)", 92, 15, 100},
    {L"Width", L"Window width (DIP)", 340, 220, 1200},
    {L"Gap", L"Row spacing (DIP)", 5, 0, 40},
    {L"Title", L"Title font size (DIP)", 19, 10, 48},
    {L"Text", L"Item font size (DIP)", 13, 8, 36},
    {L"Bar", L"Bar height (DIP)", 12, 2, 48},
    {L"Padding", L"Window padding (DIP)", 14, 4, 48},
    {L"Radius", L"Corner radius (DIP)", 12, 0, 40},
    {L"Refresh", L"Sample interval (ms)", 1000, 250, 5000},
    {L"Topmost", L"Always on top (0 / 1)", 1, 0, 1}
};
constexpr int OptionCount = static_cast<int>(sizeof(options) / sizeof(options[0]));
HWND windowHandle = nullptr, settingsHandle = nullptr;
HWND edits[OptionCount]{};
ID2D1Factory* factory = nullptr;
IDWriteFactory* textFactory = nullptr;
ID2D1HwndRenderTarget* target = nullptr;
IDWriteTextFormat *normalFont = nullptr, *titleFont = nullptr;
HFONT settingsFont = nullptr;
HICON trayIcon = nullptr;
UINT taskbarCreated = 0;
UINT activateMessage = 0;
std::wstring iniPath;
std::vector<Row> rows;
std::size_t processorCount = 0;
std::mutex dataMutex;
double temperature = -1;
std::wstring sampleStatus = L"Initializing sensors...";
HANDLE stopEvent = nullptr;
std::atomic<int> interval{1000};
float scale = 1, contentWidth = 340, contentHeight = 100, rowHeight = 20, labelWidth = 50, frequencyWidth = 70, titleHeight = 24, textHeight = 18;
int scrollOffset = 0;
int savedX = CW_USEDEFAULT, savedY = CW_USEDEFAULT;
bool preview = false;
bool selfTesting = false;
int language = 0;
std::atomic<int> usageMode{1};
const wchar_t* Tr(const wchar_t* en, const wchar_t* zh) { return language == 1 ? zh : en; }
const wchar_t* chineseLabels[] = {L"不透明度 (%)", L"窗口宽度 (DIP)", L"行间距 (DIP)", L"标题字号 (DIP)", L"条目字号 (DIP)", L"进度条高度 (DIP)", L"窗口内边距 (DIP)", L"圆角半径 (DIP)", L"采样间隔 (ms)", L"始终置顶 (0 / 1)"};


template<class T> void Release(T*& object) { if (object) { object->Release(); object = nullptr; } }
int Px(float dip) { return static_cast<int>(std::ceil(dip * scale)); }
std::wstring Number(int value) { return std::to_wstring(value); }
void SaveSettings() {
    for (const auto& o : options) WritePrivateProfileStringW(L"UI", o.key, Number(o.value).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"Language", language ? L"zh-CN" : L"en", iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"UsageMode", Number(usageMode.load()).c_str(), iniPath.c_str());
    RECT rect{}; GetWindowRect(windowHandle, &rect);
    WritePrivateProfileStringW(L"UI", L"X", Number(rect.left).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"Y", Number(rect.top).c_str(), iniPath.c_str());
}
void ReadPreferences() {
    for (auto& o : options) o.value = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"UI", o.key, o.value, iniPath.c_str())), o.minimum, o.maximum);
    wchar_t selectedLanguage[16]{};
    GetPrivateProfileStringW(L"UI", L"Language", PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? L"zh-CN" : L"en", selectedLanguage, 16, iniPath.c_str());
    language = wcscmp(selectedLanguage,L"zh-CN") == 0 ? 1 : 0;
    usageMode.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"UI",L"UsageMode",1,iniPath.c_str())),0,1));
    savedX = static_cast<int>(GetPrivateProfileIntW(L"UI", L"X", CW_USEDEFAULT, iniPath.c_str()));
    savedY = static_cast<int>(GetPrivateProfileIntW(L"UI", L"Y", CW_USEDEFAULT, iniPath.c_str()));
}
void LoadSettings() {
    if (selfTesting) return;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) {
        iniPath = std::wstring(local) + L"\\CpuFreqMonitor"; CoTaskMemFree(local);
        CreateDirectoryW(iniPath.c_str(), nullptr); iniPath += L"\\settings.ini";
    }
    ReadPreferences();
}
DWRITE_TEXT_METRICS Measure(const std::wstring& text, IDWriteTextFormat* font) {
    IDWriteTextLayout* layout = nullptr; DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(textFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), font, 10000, 1000, &layout))) { layout->GetMetrics(&metrics); layout->Release(); }
    return metrics;
}
void Layout() {
    Release(normalFont); Release(titleFont);
    textFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(options[4].value), L"en-us", &normalFont);
    textFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(options[3].value), L"en-us", &titleFont);
    if (!normalFont || !titleFont) return;
    normalFont->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); titleFont->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    titleHeight = Measure(L"CPU Freq Moniter", titleFont).height;
    textHeight = Measure(Tr(L"Temperature: 100.0 \u00b0C",L"温度: 100.0 \u00b0C"), normalFont).height;
    labelWidth = Measure(L"Core" + Number(static_cast<int>(processorCount) - 1), normalFont).width + 10;
    frequencyWidth = Measure(L"00.0 GHz", normalFont).width + 10;
    const float padding = static_cast<float>(options[6].value);
    contentWidth = (std::max)({static_cast<float>(options[1].value), labelWidth + frequencyWidth + 50 + 2 * padding, Measure(L"CPU Freq Moniter", titleFont).width + 2 * padding, Measure(Tr(L"Temperature: 100.0 \u00b0C",L"温度: 100.0 \u00b0C"), normalFont).width + 2 * padding});
    rowHeight = (std::max)(textHeight, static_cast<float>(options[5].value)) + static_cast<float>(options[2].value);
    contentHeight = padding * 2 + titleHeight + 12 + rowHeight * static_cast<float>(processorCount) + 10 + textHeight * 2;
    RECT current{}; GetWindowRect(windowHandle, &current);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST), &monitor);
    const int width = (std::min)(Px(contentWidth), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int height = (std::min)(Px(contentHeight), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    const int x = std::clamp(current.left, monitor.rcWork.left, monitor.rcWork.right - width);
    const int y = std::clamp(current.top, monitor.rcWork.top, monitor.rcWork.bottom - height);
    SetWindowPos(windowHandle, options[9].value ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, width, height, SWP_NOACTIVATE);
    const int radius = Px(static_cast<float>(options[7].value) * 2);
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
    if (!SetWindowRgn(windowHandle, region, TRUE)) DeleteObject(region);
    SetLayeredWindowAttributes(windowHandle, 0, static_cast<BYTE>(options[0].value * 255 / 100), LWA_ALPHA);
    interval.store(options[8].value);
    scrollOffset = std::clamp(scrollOffset, 0, (std::max)(0, Px(contentHeight) - height));
    InvalidateRect(windowHandle, nullptr, FALSE);
}
void DrawTextLine(const std::wstring& text, IDWriteTextFormat* font, float x, float y, float width, float height, ID2D1SolidColorBrush* brush) {
    target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), font, D2D1::RectF(x,y,x+width,y+height), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Paint() {
    PAINTSTRUCT ps{}; BeginPaint(windowHandle, &ps);
    RECT rect{}; GetClientRect(windowHandle, &rect);
    if (!target) factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(windowHandle, D2D1::SizeU(static_cast<UINT32>(rect.right), static_cast<UINT32>(rect.bottom))), &target);
    if (target && normalFont && titleFont) {
        target->SetDpi(96 * scale, 96 * scale);
        target->BeginDraw(); target->Clear(D2D1::ColorF(0x171E2B));
        target->SetTransform(D2D1::Matrix3x2F::Translation(0, -static_cast<float>(scrollOffset) / scale));
        ID2D1SolidColorBrush *text = nullptr, *muted = nullptr, *background = nullptr, *blue = nullptr, *kernel = nullptr;
        target->CreateSolidColorBrush(D2D1::ColorF(0xEDF3FF), &text); target->CreateSolidColorBrush(D2D1::ColorF(0xA5B4CD), &muted);
        target->CreateSolidColorBrush(D2D1::ColorF(0x29354A), &background); target->CreateSolidColorBrush(D2D1::ColorF(0x429BFF), &blue); target->CreateSolidColorBrush(D2D1::ColorF(0x2055AD), &kernel);
        if (text && muted && background && blue && kernel) {
            const float p = static_cast<float>(options[6].value), width = contentWidth - 2*p;
            DrawTextLine(L"CPU Freq Moniter", titleFont, p, p, width, titleHeight + 2, text);
            float y = p + titleHeight + 12;
            std::vector<Row> snapshot; double temp; std::wstring status;
            { std::lock_guard<std::mutex> lock(dataMutex); snapshot = rows; temp = temperature; status = sampleStatus; }
            for (std::size_t i = 0; i < snapshot.size(); ++i) {
                const Row& row = snapshot[i]; const float h = rowHeight - static_cast<float>(options[2].value);
                DrawTextLine(L"Core" + Number(static_cast<int>(i)), normalFont, p, y+(h-textHeight)/2, labelWidth, textHeight+2, muted);
                const float left = p + labelWidth, right = contentWidth-p-frequencyWidth, top = y+(h-static_cast<float>(options[5].value))/2;
                const auto bar = D2D1::RectF(left, top, right, top+static_cast<float>(options[5].value));
                target->FillRectangle(bar, background);
                if (row.total >= 0) target->FillRectangle(D2D1::RectF(left,top,left+(right-left)*static_cast<float>(row.total/100),bar.bottom),blue);
                if (row.kernel >= 0) target->FillRectangle(D2D1::RectF(left,top,left+(right-left)*static_cast<float>(row.kernel/100),bar.bottom),kernel);
                wchar_t value[32]{}; if (row.ghz >= 0) swprintf_s(value,L"%.1f GHz",row.ghz); else wcscpy_s(value,L"N/A");
                normalFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                DrawTextLine(value,normalFont,right+6,y+(h-textHeight)/2,frequencyWidth-6,textHeight+2,text);
                normalFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); y += rowHeight;
            }
            y += 10; wchar_t value[64]{};
            if (temp >= 0) swprintf_s(value,Tr(L"Temperature: %.1f \u00b0C",L"温度: %.1f \u00b0C"),temp); else wcscpy_s(value,Tr(L"Temperature: N/A",L"温度: N/A"));
            DrawTextLine(value, normalFont,p,y,width,textHeight+2,text);
            if(status == L"Initializing sensors...") status=Tr(L"Initializing sensors...",L"正在初始化传感器…");
            else if(status == L"DEMO - simulated sensors") status=Tr(L"DEMO - simulated sensors",L"演示模式 - 模拟数据");
            else if(status == L"Frequency unavailable (PawnIO / Intel)") status=Tr(L"Frequency unavailable (PawnIO / Intel)",L"频率不可用 (PawnIO / Intel)");
            else if(status == L"Some CPU usage counters unavailable") status=Tr(L"CPU usage unavailable",L"部分占用率计数器不可用");
            else status=status == L"Utility" ? Tr(L"Utility / kernel utility",L"性能使用率 / 内核性能使用率") : Tr(L"Busy time / kernel time",L"时间占用率 / 内核时间占用率");
            DrawTextLine(status,normalFont,p,y+textHeight,width,textHeight+2,muted);
        }
        Release(text); Release(muted); Release(background); Release(blue); Release(kernel);
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) Release(target);
    }
    EndPaint(windowHandle, &ps);
}
void Sample() {
    PawnIoClient pawn; DWORD error = 0; wchar_t path[32768]{};
    GetModuleFileNameW(nullptr,path,32768); std::wstring module(path); module = module.substr(0,module.find_last_of(L"\\")+1)+L"IntelMSR.bin";
    int cpuid[4]{}; __cpuid(cpuid,0); const int maxLeaf = cpuid[0];
    const bool intel = cpuid[1] == 0x756e6547 && cpuid[3] == 0x49656e69 && cpuid[2] == 0x6c65746e;
    unsigned bus = 100;
    if (intel && maxLeaf >= 0x16) { __cpuidex(cpuid,0x16,0); if(cpuid[2]>0 && cpuid[2]<=1000) bus=static_cast<unsigned>(cpuid[2]); }
    const bool hardware = !preview && intel && pawn.OpenAndLoad(module.c_str(),error);
    PDH_HQUERY query = nullptr; const bool queryOpen = PdhOpenQueryW(nullptr,0,&query) == ERROR_SUCCESS;
    std::vector<PDH_HCOUNTER> total(rows.size()), kernel(rows.size()), utility(rows.size()), privilegedUtility(rows.size());
    for (std::size_t i=0; queryOpen && i<rows.size(); ++i) {
        const auto instance = Number(rows[i].group)+L","+Number(rows[i].cpu);
        PdhAddEnglishCounterW(query,(L"\\Processor Information("+instance+L")\\% Processor Time").c_str(),0,&total[i]);
        PdhAddEnglishCounterW(query,(L"\\Processor Information("+instance+L")\\% Privileged Time").c_str(),0,&kernel[i]);
        PdhAddEnglishCounterW(query,(L"\\Processor Information("+instance+L")\\% Processor Utility").c_str(),0,&utility[i]);
        PdhAddEnglishCounterW(query,(L"\\Processor Information("+instance+L")\\% Privileged Utility").c_str(),0,&privilegedUtility[i]);
    }
    if(queryOpen) PdhCollectQueryData(query);
    unsigned tick = 0;
    while(WaitForSingleObject(stopEvent, static_cast<DWORD>(interval.load())) == WAIT_TIMEOUT) {
        std::vector<Row> next; { std::lock_guard<std::mutex> lock(dataMutex); next=rows; }
        const bool collected = queryOpen && PdhCollectQueryData(query) == ERROR_SUCCESS;
        double temp = -1; int available = 0; const int selectedMode=usageMode.load();
        for(std::size_t i=0;i<next.size();++i) {
            auto& row=next[i]; row.total=row.kernel=row.ghz=-1;
            auto readCounter = [&](PDH_HCOUNTER counter) { PDH_FMT_COUNTERVALUE value{}; if(collected && counter && PdhGetFormattedCounterValue(counter,PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,nullptr,&value)==ERROR_SUCCESS && (value.CStatus==PDH_CSTATUS_VALID_DATA || value.CStatus==PDH_CSTATUS_NEW_DATA)) return std::isfinite(value.doubleValue) ? (std::max)(value.doubleValue,0.0) : -1.0; return -1.0; };
            row.time=readCounter(total[i]); row.utility=readCounter(utility[i]);
            row.total=selectedMode ? row.utility : row.time;
            row.kernel=readCounter(selectedMode ? privilegedUtility[i] : kernel[i]);
            // Utility may exceed 100% under turbo. Clamp both segments on the same 0..100 scale.
            if(row.total>=0) row.total=(std::min)(row.total,100.0);
            if(row.total>=0 && row.kernel>=0) row.kernel=(std::min)(row.total,row.kernel); else row.kernel=-1;
            if(row.total>=0) ++available;
            if(hardware) {
                GROUP_AFFINITY requested{}, previous{}; requested.Group=row.group; requested.Mask=static_cast<KAFFINITY>(1)<<row.cpu;
                if(SetThreadGroupAffinity(GetCurrentThread(),&requested,&previous)) {
                    std::uint64_t value=0, thermal=0, tj=0; DWORD readError=0;
                    if(pawn.ReadMsr(0x198,value,readError) && ((value>>8)&255)) row.ghz=static_cast<double>((value>>8)&255)*bus/1000;
                    if(pawn.ReadMsr(0x19C,thermal,readError) && (thermal&(1ull<<31)) && pawn.ReadMsr(0x1A2,tj,readError)) {
                        const int junction=static_cast<int>((tj>>16)&255), delta=static_cast<int>((thermal>>16)&127);
                        if(junction>0 && junction<=125 && delta<=junction) temp=(std::max)(temp,static_cast<double>(junction-delta));
                    }
                    if(!SetThreadGroupAffinity(GetCurrentThread(),&previous,nullptr)) { row.ghz=-1; }
                }
            }
            if(preview) { row.total=static_cast<double>((i*17+tick*7)%101); row.kernel=row.total*0.3; row.ghz=i<8?5.2:4.0; temp=52; }
        }
        std::wstring status = preview ? L"DEMO - simulated sensors" : (!hardware ? L"Frequency unavailable (PawnIO / Intel)" : (available==static_cast<int>(next.size()) ? (selectedMode ? L"Utility" : L"Time") : L"Some CPU usage counters unavailable"));
        { std::lock_guard<std::mutex> lock(dataMutex); rows=std::move(next); temperature=temp; sampleStatus=std::move(status); }
        PostMessageW(windowHandle,SampleMessage,0,0); ++tick;
    }
    if(query) PdhCloseQuery(query);
}
HICON CreateTrayIcon() {
    const int size=32; HDC dc=GetDC(nullptr), memory=CreateCompatibleDC(dc);
    HBITMAP color=CreateCompatibleBitmap(dc,size,size), mask=CreateBitmap(size,size,1,1,nullptr);
    HGDIOBJ old=SelectObject(memory,color); RECT r{0,0,size,size}; HBRUSH bg=CreateSolidBrush(RGB(23,30,43)); FillRect(memory,&r,bg); DeleteObject(bg);
    HBRUSH blue=CreateSolidBrush(RGB(66,155,255)); RECT chip{7,7,25,25}; FillRect(memory,&chip,blue);
    for(int i=9;i<=21;i+=6) { RECT a{2,i,7,i+2},b{25,i,30,i+2},c{i,2,i+2,7},d{i,25,i+2,30}; FillRect(memory,&a,blue);FillRect(memory,&b,blue);FillRect(memory,&c,blue);FillRect(memory,&d,blue); }
    DeleteObject(blue); SelectObject(memory,mask); PatBlt(memory,0,0,size,size,BLACKNESS); SelectObject(memory,old);
    ICONINFO info{TRUE,0,0,mask,color}; HICON icon=CreateIconIndirect(&info);
    DeleteObject(mask); DeleteObject(color); DeleteDC(memory); ReleaseDC(nullptr,dc); return icon;
}
void Tray(DWORD operation) {
    if (selfTesting) return;
    NOTIFYICONDATAW data{}; data.cbSize=sizeof(data);data.hWnd=windowHandle;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=TrayMessage;data.hIcon=trayIcon;wcscpy_s(data.szTip,L"CPU Freq Moniter 1.3.0");
    Shell_NotifyIconW(operation,&data);
}
void ShowSettings();
void UpdateSettingsLanguage(HWND hwnd) {
    SetWindowTextW(hwnd,Tr(L"CPU Freq Moniter - Settings",L"CPU Freq Moniter - 设置"));
    for(int i=0;i<OptionCount;++i) SetWindowTextW(GetDlgItem(hwnd,200+i), language ? chineseLabels[i] : options[i].label);
    SetWindowTextW(GetDlgItem(hwnd,300),Tr(L"Changes preview and save automatically.\nWidth expands when needed to fit text.",L"更改即时预览并自动保存。\n宽度不足时会自动扩展以容纳文字。"));
    SetWindowTextW(GetDlgItem(hwnd,301),Tr(L"Close",L"关闭"));
    SetWindowTextW(GetDlgItem(hwnd,302),Tr(L"Language",L"语言"));
    SetWindowTextW(GetDlgItem(hwnd,304),Tr(L"CPU usage measurement",L"CPU 占用率口径"));
    SendDlgItemMessageW(hwnd,305,CB_RESETCONTENT,0,0);
    SendDlgItemMessageW(hwnd,305,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Tr(L"Busy time",L"时间占用率")));
    SendDlgItemMessageW(hwnd,305,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Tr(L"Processor Utility",L"性能使用率")));
    SendDlgItemMessageW(hwnd,305,CB_SETCURSEL,usageMode.load(),0);
}
void SettingsLayout(HWND hwnd) {
    const float s=static_cast<float>(GetDpiForWindow(hwnd))/96;
    auto px=[s](int value){return static_cast<int>(std::ceil(static_cast<float>(value)*s));};
    if(settingsFont) DeleteObject(settingsFont);
    settingsFont=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    for(int i=0;i<OptionCount;++i) {
        SetWindowPos(GetDlgItem(hwnd,200+i),nullptr,px(20),px(20+i*34),px(225),px(25),SWP_NOZORDER);
        SetWindowPos(edits[i],nullptr,px(250),px(20+i*34),px(100),px(25),SWP_NOZORDER);
        SendMessageW(GetDlgItem(hwnd,200+i),WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);SendMessageW(edits[i],WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);
    }
    SetWindowPos(GetDlgItem(hwnd,300),nullptr,px(20),px(20+OptionCount*34+80),px(340),px(46),SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd,301),nullptr,px(240),px(20+OptionCount*34+137),px(110),px(30),SWP_NOZORDER);
    SendMessageW(GetDlgItem(hwnd,300),WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);SendMessageW(GetDlgItem(hwnd,301),WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);
    for(int row=0;row<2;++row) {
        const int y=20+(OptionCount+row)*34;
        SetWindowPos(GetDlgItem(hwnd,302+row*2),nullptr,px(20),px(y),px(190),px(25),SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd,303+row*2),nullptr,px(210),px(y),px(145),px(150),SWP_NOZORDER);
        SendDlgItemMessageW(hwnd,302+row*2,WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);
        SendDlgItemMessageW(hwnd,303+row*2,WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);
        SendDlgItemMessageW(hwnd,303+row*2,CB_SETDROPPEDWIDTH,px(230),0);
    }
    RECT r{0,0,px(375),px(20+OptionCount*34+185)};AdjustWindowRectExForDpi(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW,GetDpiForWindow(hwnd));
    SetWindowPos(hwnd,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
}
LRESULT CALLBACK SettingsProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_CREATE:
        for(int i=0;i<OptionCount;++i) {
            CreateWindowW(L"STATIC",options[i].label,WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(200+i)),nullptr,nullptr);
            edits[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",Number(options[i].value).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),nullptr,nullptr);
        }
        CreateWindowW(L"STATIC",L"Changes preview and save automatically.\nWidth expands when needed to fit text.",WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,reinterpret_cast<HMENU>(300),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,0,0,0,0,hwnd,reinterpret_cast<HMENU>(301),nullptr,nullptr);
        for(int row=0;row<2;++row) {
            CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(302+row*2)),nullptr,nullptr);
            CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(303+row*2)),nullptr,nullptr);
        }
        SendDlgItemMessageW(hwnd,303,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"English"));
        SendDlgItemMessageW(hwnd,303,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"简体中文"));
        SendDlgItemMessageW(hwnd,303,CB_SETCURSEL,language,0);
        UpdateSettingsLanguage(hwnd);SettingsLayout(hwnd); return 0;
    case WM_COMMAND: {
        const int id=LOWORD(w);
        if((id==303 || id==305) && HIWORD(w)==CBN_SELCHANGE) {
            const int selected=static_cast<int>(SendDlgItemMessageW(hwnd,id,CB_GETCURSEL,0,0));
            if(selected>=0 && selected<=1) {
                if(id==303) language=selected; else usageMode.store(selected);
                UpdateSettingsLanguage(hwnd);Layout();if(!selfTesting)SaveSettings();
            }
            return 0;
        }
        if(id==301 || id==IDCANCEL) { DestroyWindow(hwnd);return 0; }
        if(id>=100 && id<100+OptionCount && (HIWORD(w)==EN_CHANGE || HIWORD(w)==EN_KILLFOCUS) && settingsHandle) {
            const int i=id-100; BOOL valid=FALSE; const UINT value=GetDlgItemInt(hwnd,id,&valid,FALSE);
            if(valid && value>=static_cast<UINT>(options[i].minimum) && value<=static_cast<UINT>(options[i].maximum)) {options[i].value=static_cast<int>(value);Layout();if(!selfTesting) SaveSettings();}
            else if(HIWORD(w)==EN_KILLFOCUS) SetWindowTextW(edits[i],Number(options[i].value).c_str());
        }
        return 0;
    }
    case WM_DPICHANGED: {const RECT* r=reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,r->left,r->top,0,0,SWP_NOSIZE|SWP_NOZORDER);SettingsLayout(hwnd);return 0;}
    case WM_CLOSE: DestroyWindow(hwnd);return 0;
    case WM_DESTROY: settingsHandle=nullptr;return 0;
    default: return DefWindowProcW(hwnd,message,w,l);
    }
}
void ShowSettings() {
    if(!settingsHandle) settingsHandle=CreateWindowExW(WS_EX_TOOLWINDOW,L"CpuFreqSettings",L"CPU Freq Moniter - Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,400,520,windowHandle,nullptr,GetModuleHandleW(nullptr),nullptr);
    ShowWindow(settingsHandle,SW_SHOW);SetForegroundWindow(settingsHandle);
}
HMENU CreateTrayMenu(HWND hwnd) {
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,IsWindowVisible(hwnd)?Tr(L"Hide overlay",L"隐藏浮窗"):Tr(L"Show overlay",L"显示浮窗"));AppendMenuW(menu,MF_STRING,2,Tr(L"Settings",L"打开设置"));AppendMenuW(menu,MF_STRING,3,Tr(L"About",L"关于"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,4,Tr(L"Exit",L"退出"));
    return menu;
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(activateMessage && message==activateMessage) { ShowWindow(hwnd,SW_SHOWNOACTIVATE); Layout(); SetForegroundWindow(hwnd); return 0; }
    if(taskbarCreated && message==taskbarCreated) { Tray(NIM_ADD);return 0; }
    switch(message) {
    case WM_PAINT: Paint();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE: if(target) target->Resize(D2D1::SizeU(LOWORD(l),HIWORD(l))); return 0;
    case WM_LBUTTONDOWN: ReleaseCapture();SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,l);return 0;
    case WM_NCLBUTTONDBLCLK:
    case WM_LBUTTONDBLCLK:ShowWindow(hwnd,SW_HIDE);return 0;
    case WM_EXITSIZEMOVE: Layout();if(!selfTesting) SaveSettings();return 0;
    case WM_MOUSEWHEEL: { RECT r{};GetClientRect(hwnd,&r);scrollOffset=std::clamp(scrollOffset-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*Px(rowHeight*3),0,(std::max)(0,Px(contentHeight)-static_cast<int>(r.bottom)));InvalidateRect(hwnd,nullptr,FALSE);return 0; }
    case WM_DPICHANGED: {scale=static_cast<float>(HIWORD(w))/96;const RECT* r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,0,0,SWP_NOSIZE|SWP_NOZORDER);Layout();return 0;}
    case WM_DISPLAYCHANGE:Layout();return 0;
    case SampleMessage:InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case TrayMessage:
        if(l==WM_LBUTTONUP) ShowSettings();
        if(l==WM_RBUTTONUP) {
            HMENU menu=CreateTrayMenu(hwnd);
            POINT p{};GetCursorPos(&p);SetForegroundWindow(hwnd);const UINT command=static_cast<UINT>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr));DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);
            if(command==1) ShowWindow(hwnd,IsWindowVisible(hwnd)?SW_HIDE:SW_SHOWNOACTIVATE);
            if(command==2) ShowSettings();
            if(command==3) MessageBoxW(hwnd,Tr(L"CPU Freq Moniter 1.3.0\nIntel frequency via PawnIO\nTemperature: hottest valid core\nBlue = total; dark blue = kernel portion\nDrag anywhere; double-click to hide.\nMouse wheel scrolls tall overlays.",L"CPU Freq Moniter 1.3.0\n通过 PawnIO 读取 Intel 频率\n温度：最高有效核心温度\n蓝色表示总占用，深蓝色表示其中的内核占用\n任意位置拖动；双击隐藏。\n超高浮窗可用滚轮查看。"),Tr(L"About",L"关于"),MB_OK);
            if(command==4) DestroyWindow(hwnd);
        }return 0;
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);return 0;
    case WM_DESTROY:if(!selfTesting)SaveSettings();Tray(NIM_DELETE);SetEvent(stopEvent);PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,message,w,l);
    }
}
}
int RunMonitor(bool demo, bool selfTest) {
    preview=demo;selfTesting=selfTest;
    struct InstanceGuard { HANDLE handle=nullptr; ~InstanceGuard() { if(handle) CloseHandle(handle); } } instance;
    activateMessage=RegisterWindowMessageW(L"CpuFreqMonitor.Activate.v1");
    if(!selfTest) {
        instance.handle=CreateMutexW(nullptr,FALSE,L"Local\\CpuFreqMonitor.SingleInstance.v1");
        const DWORD mutexError=GetLastError();
        if(!instance.handle) { MessageBoxW(nullptr,L"Cannot establish the single-instance lock.",L"CPU Freq Moniter",MB_ICONERROR); return 1; }
        if(mutexError==ERROR_ALREADY_EXISTS) {
            for(int attempt=0;attempt<50;++attempt) {
                HWND existing=FindWindowW(L"CpuFreqOverlay",nullptr);
                if(existing) { DWORD process=0;GetWindowThreadProcessId(existing,&process);AllowSetForegroundWindow(process);PostMessageW(existing,activateMessage,0,0);return 0; }
                Sleep(100);
            }
            MessageBoxW(nullptr,L"The monitor is already starting. Please try again shortly.",L"CPU Freq Moniter",MB_OK);
            return 0;
        }
    } else {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        std::wstring log(executable);log=log.substr(0,log.find_last_of(L"\\")+1)+(demo?L"ui-test.txt":L"sensor-test.txt");
        FILE* stream=nullptr;_wfreopen_s(&stream,log.c_str(),L"w",stdout);
        iniPath=log+L".ini";
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory)) || FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&textFactory)))) return 1;
    for(WORD group=0;group<GetActiveProcessorGroupCount();++group) for(DWORD cpu=0;cpu<GetActiveProcessorCount(group);++cpu) rows.push_back({group,static_cast<BYTE>(cpu)});
    if(rows.empty()) return 1;
    processorCount = rows.size();
    LoadSettings();stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent)return 1;
    WNDCLASSEXW cls{sizeof(cls)};cls.style=CS_DBLCLKS;cls.lpfnWndProc=WindowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=selfTest?L"CpuFreqOverlayTest":L"CpuFreqOverlay";RegisterClassExW(&cls);
    cls.style=0;cls.lpfnWndProc=SettingsProc;cls.lpszClassName=L"CpuFreqSettings";cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassExW(&cls);
    windowHandle=CreateWindowExW((preview && !selfTest ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW)|WS_EX_LAYERED,selfTest?L"CpuFreqOverlayTest":L"CpuFreqOverlay",L"CPU Freq Moniter",WS_POPUP,savedX==CW_USEDEFAULT?100:savedX,savedY==CW_USEDEFAULT?100:savedY,340,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!windowHandle)return 1;
    scale=static_cast<float>(GetDpiForWindow(windowHandle))/96;Layout();trayIcon=CreateTrayIcon();taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");ChangeWindowMessageFilterEx(windowHandle,taskbarCreated,MSGFLT_ALLOW,nullptr);ChangeWindowMessageFilterEx(windowHandle,activateMessage,MSGFLT_ALLOW,nullptr);Tray(NIM_ADD);
    ShowWindow(windowHandle,SW_SHOWNOACTIVATE);
    ShowWindow(windowHandle,SW_SHOWNOACTIVATE);
    std::thread worker(Sample);
    if(selfTest) SetTimer(windowHandle,99,2500,nullptr);
    MSG message{};int result=0;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(selfTest && message.message==WM_TIMER && message.wParam==99) {
            KillTimer(windowHandle,99);ShowSettings();
            SendDlgItemMessageW(settingsHandle,303,CB_SETCURSEL,1,0);
            SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(303,CBN_SELCHANGE),0);
            wchar_t translated[64]{};GetWindowTextW(GetDlgItem(settingsHandle,301),translated,64);
            if(wcscmp(translated,L"关闭")!=0 || language!=1)result=6;
            HMENU translatedMenu=CreateTrayMenu(windowHandle);GetMenuStringW(translatedMenu,2,translated,64,MF_BYCOMMAND);DestroyMenu(translatedMenu);
            if(wcscmp(translated,L"打开设置")!=0)result=8;
            SaveSettings();language=0;usageMode.store(0);ReadPreferences();
            if(language!=1 || usageMode.load()!=1)result=9;
            SendDlgItemMessageW(settingsHandle,303,CB_SETCURSEL,0,0);
            SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(303,CBN_SELCHANGE),0);
            GetWindowTextW(GetDlgItem(settingsHandle,301),translated,64);if(wcscmp(translated,L"Close")!=0)result=7;
            translatedMenu=CreateTrayMenu(windowHandle);GetMenuStringW(translatedMenu,2,translated,64,MF_BYCOMMAND);DestroyMenu(translatedMenu);
            if(wcscmp(translated,L"Settings")!=0)result=10;
            SetWindowTextW(edits[4],L"36");SetWindowTextW(edits[1],L"220");
            if(contentWidth < labelWidth+frequencyWidth+50+2*options[6].value) result=2;
            SetWindowTextW(edits[0],L"1");SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(100,EN_KILLFOCUS),reinterpret_cast<LPARAM>(edits[0]));
            if(options[0].value!=92) result=3;
            SendMessageW(windowHandle,WM_LBUTTONDBLCLK,0,0);if(IsWindowVisible(windowHandle))result=4;
            ShowWindow(windowHandle,SW_SHOWNOACTIVATE);
    ShowWindow(windowHandle,SW_SHOWNOACTIVATE);UpdateWindow(windowHandle);
            {std::lock_guard<std::mutex> lock(dataMutex);if(preview && (rows[0].total<0 || temperature<0))result=5; std::printf("Sensor mode: %s; temperature=%.1f C\n",preview?"demo":"hardware",temperature); for(std::size_t i=0;i<rows.size();++i) std::printf("Core%zu total=%.2f kernel=%.2f GHz=%.3f time=%.2f utility=%.2f\n",i,rows[i].total,rows[i].kernel,rows[i].ghz,rows[i].time,rows[i].utility);}
            std::printf("UI checks exit=%d\n",result); std::fflush(stdout); DestroyWindow(settingsHandle);DestroyWindow(windowHandle);continue;
        }
        if(!settingsHandle || !IsDialogMessageW(settingsHandle,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
    }
    SetEvent(stopEvent);worker.join();CloseHandle(stopEvent);if(trayIcon)DestroyIcon(trayIcon);if(settingsFont)DeleteObject(settingsFont);
    Release(target);Release(normalFont);Release(titleFont);Release(textFactory);Release(factory);CoUninitialize();return result;
}
