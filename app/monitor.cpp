// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
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
#include <cwctype>
#include "pawnio_client.h"
#include "monitor.h"
#include "hardware.h"
#include "pmu.h"
#include <psapi.h>
#include "../shared/version.h"

namespace {
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT SampleMessage = WM_APP + 2;
struct Row { WORD group; BYTE cpu; double total = -1, kernel = -1, ghz = -1, time = -1, utility = -1, ipc = -1; int busyAlert=0; };
struct Option { const wchar_t* key; const wchar_t* label; int value, minimum, maximum; };
Option options[] = {
    {L"Opacity", L"Opacity (%)", 92, 15, 100},
    {L"Width", L"Window width (DIP)", 250, 180, 1200},
    {L"Gap", L"Row spacing (DIP)", 1, -40, 40},
    {L"Title", L"Title font size (DIP)", 14, 10, 48},
    {L"Text", L"Item font size (DIP)", 10, 8, 36},
    {L"Bar", L"Bar height (DIP)", 6, 2, 48},
    {L"Padding", L"Window padding (DIP)", 8, 4, 48},
    {L"Radius", L"Corner radius (DIP)", 8, 0, 40},
    {L"RefreshHz", L"Refresh rate (Hz)", 4, 1, 10},
    {L"Topmost", L"Always on top (0 / 1)", 1, 0, 1}
};
constexpr int OptionCount = static_cast<int>(sizeof(options) / sizeof(options[0]));
// Only spacing is fractional; the other options keep their integer semantics.
float rowSpacing=1;
HWND windowHandle = nullptr, settingsHandle = nullptr, tmaHandle=nullptr;
void SyncTmaWindow();
void ShowSettings();
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
CpuTopology topology;
struct DisplayGroup { CoreKind kind; std::vector<std::size_t> physical; };
std::vector<DisplayGroup> displayGroups;
HardwareReading hardwareReading;
std::vector<int> temperatureAlerts;
int packageTemperatureAlert=0;
DWORD pmuError=ERROR_NOT_SUPPORTED, imcError=ERROR_NOT_SUPPORTED;
std::vector<double> sampleDurations;
std::vector<double> paintLatencies;
double publishedAt=0;
std::uint64_t sampleIoctls=0;
double bandwidthReadSum=0,bandwidthWriteSum=0,bandwidthReadMax=0,bandwidthWriteMax=0;unsigned bandwidthSamples=0;
int benchmarkSeconds=0;unsigned pmuGoodSamples=0;
std::atomic<bool> memoryLifecyclePassed{true};
std::atomic<bool> samplePending{false};
float ipcWidth=0, busyWidth=0, temperatureColumnWidth=0;
ID2D1SolidColorBrush *textBrush=nullptr,*mutedBrush=nullptr,*backgroundBrush=nullptr,*blueBrush=nullptr,*kernelBrush=nullptr,*orangeBrush=nullptr,*redBrush=nullptr;
void BuildDisplayGroups() {
    for(std::size_t i=0;i<topology.cores.size();++i) if(topology.cores[i].kind!=CoreKind::Efficiency)displayGroups.push_back({topology.cores[i].kind,{i}});
    for(std::size_t i=0;i<topology.cores.size();++i) if(topology.cores[i].kind==CoreKind::Efficiency){if(displayGroups.empty() || displayGroups.back().kind!=CoreKind::Efficiency || displayGroups.back().physical.size()==6)displayGroups.push_back({CoreKind::Efficiency,{}});displayGroups.back().physical.push_back(i);}
}

std::mutex dataMutex;
double temperature = -1;
std::wstring sampleStatus = L"Initializing sensors...";
HANDLE stopEvent = nullptr;
std::atomic<int> interval{250};
float scale = 1, contentWidth = 250, contentHeight = 100, rowHeight = 20, labelWidth = 50, frequencyWidth = 70, titleHeight = 24, textHeight = 18;
int scrollOffset = 0;
int savedX = CW_USEDEFAULT, savedY = CW_USEDEFAULT;
bool preview = false;
bool selfTesting = false;
int language = 0;
bool detailed=true,advanced=false;
TmaReading tmaReading;DWORD tmaError=ERROR_NOT_SUPPORTED;double tmaPublishedAt=0;
int tmaX=CW_USEDEFAULT,tmaY=CW_USEDEFAULT,tmaScroll=0,tmaFullHeight=0;unsigned tmaGoodSamples=0,tmaMemoryGoodSamples=0;
std::wstring cpuTitle;
struct ContentOption { const wchar_t* key; const wchar_t* en; const wchar_t* zh; bool enabled; };
ContentOption contents[] = {
    {L"ShowIPC",L"IPC",L"IPC",false},
    {L"ShowRAM",L"RAM R/W",L"RAM R/W",true},
    {L"ShowLLC",L"LLC MPKI (P cores)",L"LLC MPKI（P 核）",false},
    {L"ShowFrequency",L"Frequency",L"频率",true},
    {L"ShowTemperature",L"Temperature",L"温度",true},
    {L"ShowPower",L"Package power (detailed)",L"封装功耗（详细模式）",true}
};
constexpr int ContentCount=static_cast<int>(sizeof(contents)/sizeof(contents[0]));
std::atomic<bool> memoryEnabled{true};
std::atomic<unsigned> pmuOptions{0};
int settingsTab=0;
float RowGap() {
    return (std::max)(rowSpacing,1.0f-(std::max)(textHeight,static_cast<float>(options[5].value)));
}
float CoreRowHeight(CoreKind) {
    return (std::max)(textHeight,static_cast<float>(options[5].value)) + RowGap();
}
float CoreHeaderHeight() { return (std::max)(1.0f,textHeight+RowGap()); }
float CoreGroupGap() { return (std::max)(0.0f,rowSpacing); }
float FooterRowHeight() { return (std::max)(1.0f,textHeight+RowGap()); }
void ApplyPreset(int preset) {
    const int tiny[]={92,180,-2,12,8,7,6,6,1,1};
    const int smallPreset[]={92,230,1,13,9,5,8,8};
    const int normal[]={92,250,1,14,10,6,8,8};
    if(preset==2){for(int i=0;i<OptionCount;++i)options[i].value=tiny[i];}
    else for(int i=1;i<8;++i)options[i].value=preset==0?smallPreset[i]:normal[i];
    rowSpacing=static_cast<float>(options[2].value);
}

std::atomic<int> usageMode{0};
const wchar_t* Tr(const wchar_t* en, const wchar_t* zh) { return language == 1 ? zh : en; }
const wchar_t* chineseLabels[] = {L"不透明度 (%)", L"窗口宽度 (DIP)", L"行间距 (DIP)", L"标题字号 (DIP)", L"条目字号 (DIP)", L"进度条高度 (DIP)", L"窗口内边距 (DIP)", L"圆角半径 (DIP)", L"刷新率 (Hz)", L"始终置顶 (0 / 1)"};


template<class T> void Release(T*& object) { if (object) { object->Release(); object = nullptr; } }
void DropTarget() { Release(textBrush);Release(mutedBrush);Release(backgroundBrush);Release(blueBrush);Release(kernelBrush);Release(orangeBrush);Release(redBrush);Release(target); }
int Px(float dip) { return static_cast<int>(std::ceil(dip * scale)); }
std::wstring Number(int value) { return std::to_wstring(value); }
std::wstring OptionText(int i) {
    if(i!=2)return Number(options[i].value);
    wchar_t text[64]{};swprintf_s(text,L"%.9g",static_cast<double>(rowSpacing));return text;
}
bool ParseSpacing(const wchar_t* text,float& value) {
    wchar_t* end=nullptr;const double parsed=wcstod(text,&end);
    if(end==text)return false;
    while(iswspace(*end))++end;
    if(*end || !std::isfinite(parsed) || parsed<options[2].minimum || parsed>options[2].maximum)return false;
    value=static_cast<float>(parsed);return true;
}
void SaveSettings() {
    for(int i=0;i<OptionCount;++i)WritePrivateProfileStringW(L"UI",options[i].key,OptionText(i).c_str(),iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"Language", language ? L"zh-CN" : L"en", iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"BusyModeV2", Number(usageMode.load()).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"UI",L"Detailed",detailed?L"1":L"0",iniPath.c_str());
    WritePrivateProfileStringW(L"UI",L"ViewMode",advanced?L"2":detailed?L"0":L"1",iniPath.c_str());
    WritePrivateProfileStringW(L"UI",L"TmaX",Number(tmaX).c_str(),iniPath.c_str());WritePrivateProfileStringW(L"UI",L"TmaY",Number(tmaY).c_str(),iniPath.c_str());
    for(const auto& c:contents)WritePrivateProfileStringW(L"UI",c.key,c.enabled?L"1":L"0",iniPath.c_str());
    RECT rect{}; GetWindowRect(windowHandle, &rect);
    WritePrivateProfileStringW(L"UI", L"X", Number(rect.left).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"Y", Number(rect.top).c_str(), iniPath.c_str());
}
void ReadPreferences() {
    for(int i=0;i<OptionCount;++i){auto& o=options[i];if(i==2){wchar_t text[128]{};GetPrivateProfileStringW(L"UI",o.key,OptionText(i).c_str(),text,128,iniPath.c_str());ParseSpacing(text,rowSpacing);}else o.value=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"UI",o.key,o.value,iniPath.c_str())),o.minimum,o.maximum);}
    detailed=GetPrivateProfileIntW(L"UI",L"Detailed",detailed?1:0,iniPath.c_str())!=0;
    const auto mode=GetPrivateProfileIntW(L"UI",L"ViewMode",detailed?0:1,iniPath.c_str());advanced=mode==2;detailed=mode!=1;
    tmaX=static_cast<int>(GetPrivateProfileIntW(L"UI",L"TmaX",CW_USEDEFAULT,iniPath.c_str()));tmaY=static_cast<int>(GetPrivateProfileIntW(L"UI",L"TmaY",CW_USEDEFAULT,iniPath.c_str()));
    for(auto& c:contents)c.enabled=GetPrivateProfileIntW(L"UI",c.key,c.enabled?1:0,iniPath.c_str())!=0;
    wchar_t selectedLanguage[16]{};
    GetPrivateProfileStringW(L"UI", L"Language", PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? L"zh-CN" : L"en", selectedLanguage, 16, iniPath.c_str());
    language = wcscmp(selectedLanguage,L"zh-CN") == 0 ? 1 : 0;
    usageMode.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"UI",L"BusyModeV2",0,iniPath.c_str())),0,1));
    savedX = static_cast<int>(GetPrivateProfileIntW(L"UI", L"X", CW_USEDEFAULT, iniPath.c_str()));
    savedY = static_cast<int>(GetPrivateProfileIntW(L"UI", L"Y", CW_USEDEFAULT, iniPath.c_str()));
    // Upgrade the exact old default; preserve deliberately customized layouts.
    if(!advanced && options[1].value==340 && rowSpacing==5 && options[3].value==19 && options[4].value==13 && options[5].value==12 && options[6].value==14 && options[7].value==12)ApplyPreset(1);
    if(options[1].value==180 && rowSpacing==-2 && options[3].value==12 && options[4].value==8 && options[5].value==8 && options[6].value==6 && options[7].value==6)options[5].value=7;
}
// Keep old settings intact and never overwrite an existing CoreScope configuration.
std::wstring ResolveSettingsPath(const std::wstring& local) {
    const auto directory = local + L"\\CoreScope";
    const auto current = directory + L"\\settings.ini";
    const auto legacy = local + L"\\CpuFreqMonitor\\settings.ini";
    CreateDirectoryW(directory.c_str(), nullptr);
    if (GetFileAttributesW(current.c_str()) != INVALID_FILE_ATTRIBUTES) return current;
    if (CopyFileW(legacy.c_str(), current.c_str(), TRUE)) return current;
    // Another instance may have completed migration. CopyFile's fail-if-exists protects it.
    if (GetFileAttributesW(current.c_str()) != INVALID_FILE_ATTRIBUTES) return current;
    if (GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) return legacy;
    return current;
}
void LoadSettings() {
    if (selfTesting) return;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) {
        iniPath = ResolveSettingsPath(local); CoTaskMemFree(local);
    }
    ReadPreferences();
}
DWRITE_TEXT_METRICS Measure(const std::wstring& text, IDWriteTextFormat* font, float width=10000) {
    IDWriteTextLayout* layout = nullptr; DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(textFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), font, width, 1000, &layout))) { layout->GetMetrics(&metrics); layout->Release(); }
    return metrics;
}
void Layout() {
    Release(normalFont); Release(titleFont);
    textFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(options[4].value), L"en-us", &normalFont);
    textFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(options[3].value), L"en-us", &titleFont);
    if (!normalFont || !titleFont) return;
    normalFont->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); titleFont->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    titleHeight = Measure(cpuTitle, titleFont).height;
    textHeight = Measure(L"Temperature: 100.0 \u00b0C", normalFont).height;
    labelWidth = Measure(detailed?Number(static_cast<int>(processorCount) - 1):L"P "+Number(static_cast<int>(topology.cores.size())-1), normalFont).width + 6;
    frequencyWidth = contents[3].enabled?Measure(detailed?L"00.00G":L"0.0 GHz", normalFont).width + 6:0;
    temperatureColumnWidth=Measure(L"100°C",normalFont).width+4;ipcWidth=contents[0].enabled?Measure(L"99.99",normalFont).width+6:0;busyWidth=detailed?Measure(L"100%",normalFont).width+6:0;
    const float padding = static_cast<float>(options[6].value);
    contentWidth = (std::max)({static_cast<float>(options[1].value), labelWidth + frequencyWidth + ipcWidth + busyWidth + 40 + 2 * padding, Measure(L"Temperature: 100.0 \u00b0C", normalFont).width + 2 * padding});
    if(contents[0].enabled || contents[2].enabled)contentWidth=(std::max)(contentWidth,Measure(L"PMU unavailable (error 4294967295)",normalFont).width+2*padding);
    if(topology.amd && !preview)contentWidth=(std::max)(contentWidth,Measure(L"AMD sensors unavailable (4294967295)",normalFont).width+2*padding);
    if(contents[2].enabled)contentWidth=(std::max)(contentWidth,Measure(L"LLC MPKI (P): 999999.9",normalFont).width+2*padding);
    if(detailed && contents[4].enabled)for(const auto& group:displayGroups)contentWidth=(std::max)(contentWidth,Measure(CoreKindName(group.kind),normalFont).width+12+temperatureColumnWidth*static_cast<float>(group.physical.size())+2*padding);
    rowHeight = (std::max)(textHeight, static_cast<float>(options[5].value)) + RowGap();

    RECT current{}; GetWindowRect(windowHandle, &current);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST), &monitor);
    contentWidth=(std::min)(contentWidth,static_cast<float>(monitor.rcWork.right-monitor.rcWork.left)/scale);
    titleHeight=Measure(cpuTitle,titleFont,contentWidth-2*padding).height;
    contentHeight=padding*2+titleHeight+8;
    if(detailed) {
        if(contents[4].enabled || contents[5].enabled)contentHeight+=textHeight+6;
        contentHeight+=textHeight+4;
        for(const auto& group:displayGroups){contentHeight+=CoreHeaderHeight()+CoreGroupGap();for(auto core:group.physical)contentHeight+=CoreRowHeight(group.kind)*static_cast<float>(topology.cores[core].siblings.size());}
    } else {
        contentHeight+=rowHeight*static_cast<float>(processorCount);
        if(contents[4].enabled)contentHeight+=FooterRowHeight();
    }
    if(contents[1].enabled)contentHeight+=(FooterRowHeight())*2;
    if(contents[2].enabled)contentHeight+=FooterRowHeight();
    if(contents[0].enabled || contents[2].enabled)contentHeight+=FooterRowHeight();
    if(preview)contentHeight+=FooterRowHeight();
    if(topology.amd && !preview){if(contents[3].enabled)contentHeight+=FooterRowHeight();if(contents[4].enabled || contents[5].enabled)contentHeight+=FooterRowHeight();}
    const int width=Px(contentWidth);
    const int height = (std::min)(Px(contentHeight), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    const int x = std::clamp(current.left, monitor.rcWork.left, monitor.rcWork.right - width);
    const int y = std::clamp(current.top, monitor.rcWork.top, monitor.rcWork.bottom - height);
    SetWindowPos(windowHandle, options[9].value ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, width, height, SWP_NOACTIVATE);
    const int radius = Px(static_cast<float>(options[7].value) * 2);
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
    if (!SetWindowRgn(windowHandle, region, TRUE)) DeleteObject(region);
    SetLayeredWindowAttributes(windowHandle, 0, static_cast<BYTE>(options[0].value * 255 / 100), LWA_ALPHA);
    interval.store(1000/options[8].value);
    memoryEnabled.store(contents[1].enabled);pmuOptions.store((contents[0].enabled?1u:0u) | (contents[2].enabled?2u:0u) | (advanced?4u:0u));
    scrollOffset = std::clamp(scrollOffset, 0, (std::max)(0, Px(contentHeight) - height));
    InvalidateRect(windowHandle, nullptr, FALSE);SyncTmaWindow();
}
void DrawTextLine(const std::wstring& text, IDWriteTextFormat* font, float x, float y, float width, float height, ID2D1SolidColorBrush* brush) {
    target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), font, D2D1::RectF(x,y,x+width,y+height), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Paint() {
    PAINTSTRUCT ps{};BeginPaint(windowHandle,&ps);RECT rect{};GetClientRect(windowHandle,&rect);
    if(!target) {
        factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(windowHandle,D2D1::SizeU(static_cast<UINT32>(rect.right),static_cast<UINT32>(rect.bottom))),&target);
        if(target){target->CreateSolidColorBrush(D2D1::ColorF(0xEDF3FF),&textBrush);target->CreateSolidColorBrush(D2D1::ColorF(0xA5B4CD),&mutedBrush);target->CreateSolidColorBrush(D2D1::ColorF(0x29354A),&backgroundBrush);target->CreateSolidColorBrush(D2D1::ColorF(0x429BFF),&blueBrush);target->CreateSolidColorBrush(D2D1::ColorF(0x2055AD),&kernelBrush);target->CreateSolidColorBrush(D2D1::ColorF(0xFFA640),&orangeBrush);target->CreateSolidColorBrush(D2D1::ColorF(0xFF5E65),&redBrush);}
    }
    if(target && normalFont && titleFont && textBrush && mutedBrush && backgroundBrush && blueBrush && kernelBrush && orangeBrush && redBrush){
        static std::vector<Row> snapshot;static HardwareReading sensors;static std::vector<int> alerts;std::wstring status;int packageAlert=0;DWORD snapshotPmuError=ERROR_NOT_SUPPORTED;
        {std::lock_guard<std::mutex> lock(dataMutex);snapshot=rows;sensors=hardwareReading;alerts=temperatureAlerts;packageAlert=packageTemperatureAlert;status=sampleStatus;snapshotPmuError=pmuError;if(selfTesting && publishedAt>0 && paintLatencies.size()<4000){paintLatencies.push_back((MonotonicSeconds()-publishedAt)*1000);publishedAt=0;}}
        target->SetDpi(96*scale,96*scale);target->BeginDraw();target->Clear(D2D1::ColorF(0x171E2B));target->SetTransform(D2D1::Matrix3x2F::Translation(0,-static_cast<float>(scrollOffset)/scale));
        auto color=[&](int alert){return alert==2?redBrush:alert==1?orangeBrush:textBrush;};
        auto format=[](double value,const wchar_t* pattern){wchar_t text[96]{};if(value<0)wcscpy_s(text,L"N/A");else swprintf_s(text,pattern,value);return std::wstring(text);};
        const float p=static_cast<float>(options[6].value),width=contentWidth-2*p;
        const float ipcX=contentWidth-p-ipcWidth,freqX=ipcX-frequencyWidth,busyX=freqX-busyWidth,barX=p+labelWidth,barRight=busyX-8;
        DrawTextLine(cpuTitle,titleFont,p,p,width,titleHeight+2,textBrush);
        float y=p+titleHeight+8;
        auto drawRow=[&](std::size_t i,CoreKind kind,bool merged=false,bool lastSibling=false){
            const auto& row=snapshot[i];
            const float h=(std::max)(textHeight,static_cast<float>(options[5].value)),textY=y+(h-textHeight)/2;
            if(!merged)DrawTextLine(Number(static_cast<int>(i)),normalFont,p,textY,labelWidth,textHeight+2,mutedBrush);
            const bool joined=kind==CoreKind::Performance && (detailed || merged);
            const float barHeight=joined?(detailed?CoreRowHeight(kind):h+(!lastSibling?RowGap():0)):static_cast<float>(options[5].value);
            const float top=joined?y:y+(h-barHeight)/2,bottom=top+barHeight;
            if(!(merged && kind==CoreKind::Performance)){
            target->FillRectangle(D2D1::RectF(barX,top,barRight,bottom),backgroundBrush);
            if(row.total>=0)target->FillRectangle(D2D1::RectF(barX,top,barX+(barRight-barX)*static_cast<float>(row.total/100),bottom),row.busyAlert?color(row.busyAlert):blueBrush);
            if(row.kernel>=0)target->FillRectangle(D2D1::RectF(barX,top,barX+(barRight-barX)*static_cast<float>(row.kernel/100),bottom),kernelBrush);
            }
            if(detailed)DrawTextLine(format(row.total,L"%.0f%%"),normalFont,busyX,textY,busyWidth,textHeight+2,color(row.busyAlert));
            if(contents[3].enabled && !merged)DrawTextLine(format(row.ghz,detailed?L"%.2fG":L"%.1f GHz"),normalFont,freqX,textY,frequencyWidth,textHeight+2,textBrush);
            if(contents[0].enabled)DrawTextLine(format(row.ipc,L"%.2f"),normalFont,ipcX,textY,ipcWidth,textHeight+2,row.ipc>=0?textBrush:mutedBrush);
            y+=CoreRowHeight(kind);
        };
        if(detailed){
            if(contents[4].enabled || contents[5].enabled){
                float x=p;
                if(contents[5].enabled){const float w=Measure(L"999.9 W",normalFont).width+20;DrawTextLine(format(sensors.packageWatts,L"%.1f W"),normalFont,x,y,w,textHeight+2,textBrush);x+=w;}
                if(contents[4].enabled){const float w=Measure(L"Package: ",normalFont).width;DrawTextLine(L"Package: ",normalFont,x,y,w,textHeight+2,mutedBrush);DrawTextLine(format(sensors.packageTemperature,L"%.1f °C"),normalFont,x+w,y,contentWidth-p-x-w,textHeight+2,color(packageAlert));}
                y+=textHeight+6;
            }
            DrawTextLine(usageMode.load()?L"Utility":L"Busy Time",normalFont,p,y,busyX-p,textHeight+2,mutedBrush);
            if(contents[3].enabled)DrawTextLine(L"Freq",normalFont,freqX,y,frequencyWidth,textHeight+2,mutedBrush);
            if(contents[0].enabled)DrawTextLine(L"IPC",normalFont,ipcX,y,ipcWidth,textHeight+2,mutedBrush);
            y+=textHeight+4;
            for(const auto& group:displayGroups){
                std::wstring label=CoreKindName(group.kind);if(group.physical.size()==1)label+=L" "+Number(static_cast<int>(group.physical.front()));
                DrawTextLine(label,normalFont,p,y,labelWidth+75,textHeight+2,mutedBrush);
                float temperatureX=contentWidth-p;
                if(contents[4].enabled)for(auto it=group.physical.rbegin();it!=group.physical.rend();++it){const auto core=*it;temperatureX-=temperatureColumnWidth;DrawTextLine(format(core<sensors.cores.size()?sensors.cores[core].temperature:-1,L"%.0f°C"),normalFont,temperatureX,y,temperatureColumnWidth,textHeight+2,color(core<alerts.size()?alerts[core]:0));}
                y+=CoreHeaderHeight();
                for(auto physical:group.physical)for(auto i:topology.cores[physical].siblings)drawRow(i,group.kind);
                y+=CoreGroupGap();
            }
        }else{
            for(std::size_t core=0;core<topology.cores.size();++core){const auto& physical=topology.cores[core];const float startY=y;
                for(auto i:physical.siblings)if(i<snapshot.size())drawRow(i,physical.kind,true,i==physical.siblings.back());
                if(physical.kind==CoreKind::Performance){
                    const float barHeight=(std::min)(static_cast<float>(options[5].value)+5.0f/scale,(y-startY-RowGap())/static_cast<float>(physical.siblings.size())),groupHeight=barHeight*static_cast<float>(physical.siblings.size());
                    float barY=startY+(y-startY-RowGap()-groupHeight)/2;
                    for(auto i:physical.siblings)if(i<snapshot.size()){const auto& row=snapshot[i];const float bottom=barY+barHeight;
                        target->FillRectangle(D2D1::RectF(barX,barY,barRight,bottom),backgroundBrush);
                        if(row.total>=0)target->FillRectangle(D2D1::RectF(barX,barY,barX+(barRight-barX)*static_cast<float>(row.total/100),bottom),row.busyAlert?color(row.busyAlert):blueBrush);
                        if(row.kernel>=0)target->FillRectangle(D2D1::RectF(barX,barY,barX+(barRight-barX)*static_cast<float>(row.kernel/100),bottom),kernelBrush);
                        barY=bottom;
                    }
                }
                const float textY=startY+(y-startY-RowGap()-textHeight)/2;
                const std::wstring label=(physical.kind==CoreKind::Performance?L"P ":physical.kind==CoreKind::Efficiency?L"E ":L"C ")+Number(static_cast<int>(core));
                DrawTextLine(label,normalFont,p,textY,labelWidth,textHeight+2,mutedBrush);
                double ghz=-1;for(auto i:physical.siblings)if(i<snapshot.size() && snapshot[i].ghz>=0){ghz=snapshot[i].ghz;break;}
                if(contents[3].enabled)DrawTextLine(format(ghz,L"%.1f GHz"),normalFont,freqX,textY,frequencyWidth,textHeight+2,textBrush);
            }
            if(contents[4].enabled){double hottest=topology.amd?sensors.packageTemperature:-1;int alert=topology.amd?packageAlert:0;for(std::size_t i=0;i<sensors.cores.size();++i){hottest=(std::max)(hottest,sensors.cores[i].temperature);if(i<alerts.size())alert=(std::max)(alert,alerts[i]);}const float w=Measure(L"Temperature: ",normalFont).width;DrawTextLine(L"Temperature: ",normalFont,p,y,w,textHeight+2,mutedBrush);DrawTextLine(format(hottest,L"%.1f °C"),normalFont,p+w,y,width-w,textHeight+2,color(alert));y+=FooterRowHeight();}
        }
        // GB/s only: there is no guessed or fixed bandwidth ceiling.
        if(contents[1].enabled){DrawTextLine(L"RAM R: "+format(sensors.readGBs,L"%.1f GB/s"),normalFont,p,y,width,textHeight+2,mutedBrush);y+=FooterRowHeight();DrawTextLine(L"RAM W: "+format(sensors.writeGBs,L"%.1f GB/s"),normalFont,p,y,width,textHeight+2,mutedBrush);y+=FooterRowHeight();}
        if(contents[2].enabled){DrawTextLine(L"LLC MPKI (P): "+format(sensors.llcMpki,L"%.1f"),normalFont,p,y,width,textHeight+2,mutedBrush);y+=FooterRowHeight();}
        if(contents[0].enabled || contents[2].enabled){
            wchar_t pmuStatus[96]{};
            if(snapshotPmuError)swprintf_s(pmuStatus,L"PMU unavailable (error %lu)",snapshotPmuError);
            else wcscpy_s(pmuStatus,std::any_of(snapshot.begin(),snapshot.end(),[](const Row& row){return row.ipc>=0;})?L"PMU: active (1s updates)":L"PMU: waiting for fresh data");
            DrawTextLine(pmuStatus,normalFont,p,y,width,textHeight+2,mutedBrush);y+=FooterRowHeight();
        }
        if(topology.amd && !preview){
            if(contents[3].enabled){const bool frequencyValid=std::any_of(sensors.frequency.begin(),sensors.frequency.end(),[](double value){return value>=0;});DrawTextLine(sensors.osFrequency?L"Frequency: Windows reported":frequencyValid?L"Frequency: hardware P-state":L"Frequency: unavailable",normalFont,p,y,width,textHeight+2,mutedBrush);y+=FooterRowHeight();}
            if(contents[4].enabled || contents[5].enabled){const auto sensorStatus=sensors.sensorError?L"AMD sensors unavailable ("+std::to_wstring(sensors.sensorError)+L")":L"AMD sensors: PawnIO active";DrawTextLine(sensorStatus,normalFont,p,y,width,textHeight+2,mutedBrush);y+=FooterRowHeight();}
        }
        if(preview)DrawTextLine(L"DEMO - simulated CPU data",normalFont,p,y,width,textHeight+2,mutedBrush);
        if(target->EndDraw()==D2DERR_RECREATE_TARGET)DropTarget();
    }
    EndPaint(windowHandle,&ps);
}
void Sample() {
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring directory(path);directory=directory.substr(0,directory.find_last_of(L"\\")+1);
    HardwareSampler hardware(topology);PmuSampler pmu(topology);if(!preview)hardware.Initialize(directory,selfTesting);
    if(selfTesting && !preview && !benchmarkSeconds && hardware.Available()) {
        HardwareReading off,on,resumed;
        hardware.Poll(off,false);hardware.Poll(on,true);hardware.Poll(off,false);hardware.Poll(resumed,true);
        const bool passed=off.readGBs<0 && off.writeGBs<0 && resumed.readGBs<0 && resumed.writeGBs<0 && (hardware.ImcError()!=ERROR_SUCCESS || resumed.ioctlCount>off.ioctlCount);
        memoryLifecyclePassed.store(passed);
        std::printf("RAM enable/disable/rebaseline checks=%s off_IOCTL=%llu on_IOCTL=%llu\n",passed?"PASS":"FAIL",off.ioctlCount,resumed.ioctlCount);
    }
    PDH_HQUERY query=nullptr;const bool queryOpen=PdhOpenQueryW(nullptr,0,&query)==ERROR_SUCCESS;
    std::vector<PDH_HCOUNTER> total(processorCount),kernel(processorCount),utility(processorCount),privilegedUtility(processorCount);
    for(std::size_t i=0;queryOpen && i<processorCount;++i){const auto instance=Number(rows[i].group)+L","+Number(rows[i].cpu);
        PdhAddEnglishCounterW(query,(L"\\Processor Information("+instance+L")\\% Processor Time").c_str(),0,&total[i]);
        PdhAddEnglishCounterW(query,(L"\\Processor Information("+instance+L")\\% Privileged Time").c_str(),0,&kernel[i]);
        PdhAddEnglishCounterW(query,(L"\\Processor Information("+instance+L")\\% Processor Utility").c_str(),0,&utility[i]);
        PdhAddEnglishCounterW(query,(L"\\Processor Information("+instance+L")\\% Privileged Utility").c_str(),0,&privilegedUtility[i]);}
    if(queryOpen)PdhCollectQueryData(query);
    unsigned tick=0;double previous=MonotonicSeconds();std::vector<Row> next=rows;HardwareReading sensors;
    std::vector<AlertFilter> busyFilters(processorCount),thermalFilters(topology.cores.size());std::vector<int> thermalAlerts(topology.cores.size());AlertFilter packageFilter;DWORD waitMs=static_cast<DWORD>(interval.load());
    sampleDurations.reserve(4000);
    while(WaitForSingleObject(stopEvent,waitMs)==WAIT_TIMEOUT){
        const double start=MonotonicSeconds(),dt=start-previous;previous=start;
        const bool collected=queryOpen && PdhCollectQueryData(query)==ERROR_SUCCESS;const int selectedMode=usageMode.load();int available=0;
        hardware.Poll(sensors,memoryEnabled.load());
        const unsigned pmuMode=pmuOptions.load();const auto pmuReading=pmu.Poll(!preview && pmuMode!=0,(pmuMode&2u)!=0,(pmuMode&4u)!=0);sensors.llcMpki=pmuReading.pCoreMpki;
        for(std::size_t i=0;i<next.size();++i){auto& row=next[i];row.total=row.kernel=row.ghz=-1;
            auto readCounter=[&](PDH_HCOUNTER counter){PDH_FMT_COUNTERVALUE value{};if(collected && counter && PdhGetFormattedCounterValue(counter,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,nullptr,&value)==ERROR_SUCCESS && (value.CStatus==PDH_CSTATUS_VALID_DATA || value.CStatus==PDH_CSTATUS_NEW_DATA) && std::isfinite(value.doubleValue))return (std::max)(value.doubleValue,0.0);return -1.0;};
            row.time=readCounter(total[i]);row.utility=selectedMode?readCounter(utility[i]):-1;
            row.total=selectedMode?row.utility:row.time;row.kernel=readCounter(selectedMode?privilegedUtility[i]:kernel[i]);if(row.total>=0){row.total=(std::min)(row.total,100.0);++available;}if(row.total>=0 && row.kernel>=0)row.kernel=(std::min)(row.total,row.kernel);else row.kernel=-1;
            if(i<sensors.frequency.size())row.ghz=sensors.frequency[i];row.ipc=i<pmuReading.ipc.size()?pmuReading.ipc[i]:-1;
            if(preview){if(selfTesting && benchmarkSeconds)row.ipc=0.5+static_cast<double>((i*7+tick)%20)/10;row.time=row.total=static_cast<double>((i*17+tick*7)%101);row.kernel=row.total*0.3;row.ghz=topology.logical[i].kind==CoreKind::Efficiency?4.0:5.2;}
            row.busyAlert=busyFilters[i].Update(row.time,dt,90,98);
        }
        for(std::size_t i=0;i<sensors.cores.size();++i){auto& core=sensors.cores[i];if(preview){core.temperature=52;core.tjMax=100;}thermalAlerts[i]=thermalFilters[i].Update(core.temperature,dt,static_cast<double>(core.tjMax-15),static_cast<double>(core.tjMax-5));if(core.throttled){thermalAlerts[i]=2;thermalFilters[i].level=2;thermalFilters[i].heldSeconds=0;}}
        int packageTj=sensors.packageTjMax;for(const auto& core:sensors.cores)if(core.tjMax>0)packageTj=packageTj==0?core.tjMax:(std::min)(packageTj,core.tjMax);
        int packageAlert=packageFilter.Update(packageTj>0?sensors.packageTemperature:-1,dt,packageTj-15.0,packageTj-5.0);if(sensors.packageThrottled){packageAlert=2;packageFilter.level=2;packageFilter.heldSeconds=0;}
        const double duration=(MonotonicSeconds()-start)*1000;
        waitMs=static_cast<DWORD>((std::max)(1.0,static_cast<double>(interval.load())-duration));
        {std::lock_guard<std::mutex> lock(dataMutex);rows.swap(next);hardwareReading=sensors;temperature=sensors.packageTemperature;temperatureAlerts=thermalAlerts;packageTemperatureAlert=packageAlert;pmuError=pmuReading.error;tmaReading=pmuReading.tma;tmaError=pmuReading.tmaError;tmaPublishedAt=MonotonicSeconds();if(selfTesting && pmuReading.tma.slots)++tmaGoodSamples;if(selfTesting && pmuReading.tma.memoryCycles && pmuReading.tma.l1>=0 && pmuReading.tma.l2>=0 && pmuReading.tma.l3>=0 && pmuReading.tma.dram>=0)++tmaMemoryGoodSamples;imcError=hardware.ImcError();sampleIoctls+=sensors.ioctlCount;if(selfTesting && sensors.readGBs>=0 && sensors.writeGBs>=0){bandwidthReadSum+=sensors.readGBs;bandwidthWriteSum+=sensors.writeGBs;bandwidthReadMax=(std::max)(bandwidthReadMax,sensors.readGBs);bandwidthWriteMax=(std::max)(bandwidthWriteMax,sensors.writeGBs);++bandwidthSamples;}if(selfTesting && pmuReading.pCoreMpki>=0 && std::all_of(pmuReading.ipc.begin(),pmuReading.ipc.end(),[](double ipc){return ipc>=0;}))++pmuGoodSamples;if(selfTesting && sampleDurations.size()<4000)sampleDurations.push_back(duration);publishedAt=MonotonicSeconds();sampleStatus=preview?L"DEMO":available==static_cast<int>(next.size())?L"Ready":L"Unavailable";}
        if(!samplePending.exchange(true))PostMessageW(windowHandle,SampleMessage,0,0);++tick;
    }
    if(query)PdhCloseQuery(query);
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
    NOTIFYICONDATAW data{}; data.cbSize=sizeof(data);data.hWnd=windowHandle;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=TrayMessage;data.hIcon=trayIcon;wcscpy_s(data.szTip,L"CoreScope " CORESCOPE_VERSION_STRING);
    Shell_NotifyIconW(operation,&data);
}
void ShowSettings();
void UpdateSettingsLanguage(HWND hwnd) {
    SetWindowTextW(GetDlgItem(hwnd,306),Tr(L"View mode",L"显示模式"));
    SetWindowTextW(GetDlgItem(hwnd,308),Tr(L"Size preset",L"大小预设"));
    for(int id:{307,309})SendDlgItemMessageW(hwnd,id,CB_RESETCONTENT,0,0);
    SendDlgItemMessageW(hwnd,307,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Tr(L"Detailed",L"详细模式")));
    SendDlgItemMessageW(hwnd,307,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Tr(L"Compact",L"精简模式")));
    SendDlgItemMessageW(hwnd,307,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Tr(L"Advanced",L"进阶模式")));
    SendDlgItemMessageW(hwnd,307,CB_SETCURSEL,advanced?2:detailed?0:1,0);
    for(const auto* label:{Tr(L"Small",L"小号"),Tr(L"Default",L"默认"),Tr(L"Very small (Laptop)",L"很小（笔记本）"),Tr(L"Custom",L"自定义")})SendDlgItemMessageW(hwnd,309,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    int selectedPreset=3;
    if(options[1].value==230 && rowSpacing==1 && options[3].value==13 && options[4].value==9 && options[5].value==5 && options[6].value==8 && options[7].value==8)selectedPreset=0;
    if(options[1].value==250 && rowSpacing==1 && options[3].value==14 && options[4].value==10 && options[5].value==6 && options[6].value==8 && options[7].value==8)selectedPreset=1;
    if(options[0].value==92 && options[1].value==180 && rowSpacing==-2 && options[3].value==12 && options[4].value==8 && options[5].value==7 && options[6].value==6 && options[7].value==6 && options[8].value==1 && options[9].value==1)selectedPreset=2;
    SendDlgItemMessageW(hwnd,309,CB_SETCURSEL,selectedPreset,0);
    for(int i=0;i<ContentCount;++i){SetWindowTextW(GetDlgItem(hwnd,400+i),Tr(contents[i].en,contents[i].zh));SendDlgItemMessageW(hwnd,400+i,BM_SETCHECK,contents[i].enabled?BST_CHECKED:BST_UNCHECKED,0);}
    TCITEMW item{};item.mask=TCIF_TEXT;
    item.pszText=const_cast<wchar_t*>(Tr(L"General",L"常规"));TabCtrl_SetItem(GetDlgItem(hwnd,500),0,&item);
    item.pszText=const_cast<wchar_t*>(Tr(L"Display content",L"显示内容"));TabCtrl_SetItem(GetDlgItem(hwnd,500),1,&item);

    SetWindowTextW(hwnd,Tr(L"CoreScope - Settings",L"CoreScope - 设置"));
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
    auto px=[s](int v){return static_cast<int>(std::ceil(static_cast<float>(v)*s));};
    if(settingsFont)DeleteObject(settingsFont);
    settingsFont=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    const int top=52,step=30,bottom=top+(OptionCount+4)*step;
    auto place=[&](int id,int x,int y,int w,int h,bool show){HWND child=GetDlgItem(hwnd,id);SetWindowPos(child,nullptr,px(x),px(y),px(w),px(h),SWP_NOZORDER);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);ShowWindow(child,show?SW_SHOW:SW_HIDE);};
    place(500,10,8,365,bottom, true);
    for(int i=0;i<OptionCount;++i){place(200+i,24,top+i*step,225,25,settingsTab==0);place(100+i,254,top+i*step,100,25,settingsTab==0);}
    for(int row=0;row<4;++row){place(302+row*2,24,top+(OptionCount+row)*step,180,25,settingsTab==0);place(303+row*2,210,top+(OptionCount+row)*step,144,150,settingsTab==0);SendDlgItemMessageW(hwnd,303+row*2,CB_SETDROPPEDWIDTH,px(240),0);}
    for(int i=0;i<ContentCount;++i)place(400+i,24,top+i*step,320,25,settingsTab==1);
    place(300,20,bottom+18,345,46,true);place(301,244,bottom+70,110,30,true);
    RECT r{0,0,px(385),px(bottom+112)};AdjustWindowRectExForDpi(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW,GetDpiForWindow(hwnd));
    SetWindowPos(hwnd,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
}
LRESULT CALLBACK SettingsProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_CREATE: {
        HWND tab=CreateWindowW(WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPSIBLINGS,0,0,0,0,hwnd,reinterpret_cast<HMENU>(500),nullptr,nullptr);
        TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(L"General");TabCtrl_InsertItem(tab,0,&item);item.pszText=const_cast<wchar_t*>(L"Display content");TabCtrl_InsertItem(tab,1,&item);TabCtrl_SetCurSel(tab,settingsTab);
        for(int i=0;i<ContentCount;++i)CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(400+i)),nullptr,nullptr);
        for(int i=0;i<OptionCount;++i) {
            CreateWindowW(L"STATIC",options[i].label,WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(200+i)),nullptr,nullptr);
            edits[i]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",OptionText(i).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|(i==2?0:ES_NUMBER),0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),nullptr,nullptr);
        }
        CreateWindowW(L"STATIC",L"Changes preview and save automatically.\nWidth expands when needed to fit text.",WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,reinterpret_cast<HMENU>(300),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,0,0,0,0,hwnd,reinterpret_cast<HMENU>(301),nullptr,nullptr);
        for(int row=0;row<4;++row) {
            CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(302+row*2)),nullptr,nullptr);
            CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(303+row*2)),nullptr,nullptr);
        }
        SendDlgItemMessageW(hwnd,303,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"English"));
        SendDlgItemMessageW(hwnd,303,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"简体中文"));
        SendDlgItemMessageW(hwnd,303,CB_SETCURSEL,language,0);
        UpdateSettingsLanguage(hwnd);SettingsLayout(hwnd); return 0;
    }
    case WM_NOTIFY: if(reinterpret_cast<NMHDR*>(l)->idFrom==500 && reinterpret_cast<NMHDR*>(l)->code==TCN_SELCHANGE){settingsTab=TabCtrl_GetCurSel(GetDlgItem(hwnd,500));SettingsLayout(hwnd);}return 0;
    case WM_COMMAND: {
        const int id=LOWORD(w);
        if(id>=400 && id<400+ContentCount && HIWORD(w)==BN_CLICKED){contents[id-400].enabled=SendDlgItemMessageW(hwnd,id,BM_GETCHECK,0,0)==BST_CHECKED;Layout();if(!selfTesting)SaveSettings();return 0;}
        if((id==307 || id==309) && HIWORD(w)==CBN_SELCHANGE){const int selected=static_cast<int>(SendDlgItemMessageW(hwnd,id,CB_GETCURSEL,0,0));if(selected>=0 && selected<3){if(id==307){advanced=selected==2;detailed=selected!=1;}else {ApplyPreset(selected);for(int i=0;i<OptionCount;++i)SetWindowTextW(edits[i],OptionText(i).c_str());SendDlgItemMessageW(hwnd,309,CB_SETCURSEL,selected,0);}Layout();if(!selfTesting)SaveSettings();}return 0;}

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
            if(id==102){wchar_t text[128]{};GetWindowTextW(edits[2],text,128);
                if(ParseSpacing(text,rowSpacing)){SendDlgItemMessageW(hwnd,309,CB_SETCURSEL,3,0);Layout();if(!selfTesting)SaveSettings();}
                else if(HIWORD(w)==EN_KILLFOCUS)SetWindowTextW(edits[2],OptionText(2).c_str());
                return 0;
            }
            const int i=id-100; BOOL valid=FALSE; const UINT value=GetDlgItemInt(hwnd,id,&valid,FALSE);
            if(valid && value>=static_cast<UINT>(options[i].minimum) && value<=static_cast<UINT>(options[i].maximum)) {options[i].value=static_cast<int>(value);SendDlgItemMessageW(hwnd,309,CB_SETCURSEL,3,0);Layout();if(!selfTesting) SaveSettings();}
            else if(HIWORD(w)==EN_KILLFOCUS) SetWindowTextW(edits[i],OptionText(i).c_str());
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
    if(!settingsHandle) settingsHandle=CreateWindowExW(WS_EX_TOOLWINDOW,L"CpuFreqSettings",L"CoreScope - Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,400,520,windowHandle,nullptr,GetModuleHandleW(nullptr),nullptr);
    ShowWindow(settingsHandle,SW_SHOW);SetForegroundWindow(settingsHandle);
}

struct TmaLine {std::wstring label,value;bool heading=false;};
std::vector<TmaLine> TmaLines(){
 TmaReading reading;DWORD error;double published;
 {std::lock_guard<std::mutex> lock(dataMutex);reading=tmaReading;error=tmaError;published=tmaPublishedAt;}
 if(MonotonicSeconds()-published>2)reading={};
 auto value=[](double number){wchar_t text[64]{};if(number<0 || !std::isfinite(number))return std::wstring(L"N/A");swprintf_s(text,L"%.1f%%",number);return std::wstring(text);};
 auto age=[](double seconds){if(seconds<0)return std::wstring(L"N/A");wchar_t text[48]{};swprintf_s(text,L"%.1fs ago",seconds);return std::wstring(text);};
 std::vector<TmaLine> lines={{L"TMA · P cores",L"",true},{Tr(L"% of all P-core slots · ~1s",L"占 P 核总槽位 · 约 1 秒"),age(reading.topAge)},
  {L"Retiring",value(reading.retiring)},{L"Frontend Bound *",value(reading.frontend)},{L"Bad Speculation *",value(reading.badSpec)},{L"Backend Bound",value(reading.backend)},
  {L"Backend:",L"",true},{L"  Core Bound *",value(reading.core)},{L"  Memory Bound *",value(reading.memory)},
  {L"Memory: (% cycles)",age(reading.memoryAge),true},{L"  L1 Bound",value(reading.l1)},{L"  L2 Bound",value(reading.l2)},{L"  L3 Bound",value(reading.l3)},{L"  DRAM Bound",value(reading.dram)},
  {Tr(L"* Unvalidated / estimated",L"* 未验证 / 估算"),L""},{Tr(L"Counter groups rotate every 5s",L"计数器分组每 5 秒轮换"),L""}};
 if(error){lines.push_back({Tr(L"PMU unavailable: ",L"PMU 不可用：")+std::to_wstring(error),L""});}
 else lines.push_back({reading.slots?Tr(L"PMU active",L"PMU 采集中"):Tr(L"Waiting for valid counters",L"等待有效计数"),L""});
 return lines;
}
struct TmaMetrics {int pad=0,gap=0,line=0,title=0,width=0,height=0,value=0;float rowGap=0;};
TmaMetrics MeasureTma(HDC dc,HFONT font,HFONT bold,const std::vector<TmaLine>& lines,float dpi){
 TmaMetrics m;m.pad=static_cast<int>(std::ceil(options[6].value*dpi));m.gap=static_cast<int>(std::ceil((std::max)(1.0f,rowSpacing)*dpi));
 SelectObject(dc,font);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);m.line=tm.tmHeight+2;m.value=0;int w=0;
 for(const auto& row:lines){SelectObject(dc,row.heading?bold:font);SIZE label{},value{};GetTextExtentPoint32W(dc,row.label.c_str(),static_cast<int>(row.label.size()),&label);const std::wstring valueText=row.value.size()>6?row.value:L"999.9%";GetTextExtentPoint32W(dc,valueText.c_str(),static_cast<int>(valueText.size()),&value);m.value=(std::max)(m.value,static_cast<int>(value.cx)+m.gap*2);w=(std::max)(w,static_cast<int>(label.cx)+(row.value.empty()?0:static_cast<int>(value.cx)+m.gap*3));}
 SelectObject(dc,bold);GetTextMetricsW(dc,&tm);m.title=tm.tmHeight+2;m.width=(std::max)(static_cast<int>(std::ceil(options[1].value*dpi)),w+2*m.pad);m.height=2*m.pad;
 m.rowGap=(std::max)(rowSpacing*dpi,1.0f-static_cast<float>((std::min)(m.line,m.title)));float height=static_cast<float>(m.pad),bottom=height;
 for(const auto& row:lines){const float h=static_cast<float>(row.heading?m.title:m.line);bottom=(std::max)(bottom,height+h);height+=h+m.rowGap;}m.height=static_cast<int>(std::ceil((std::max)(bottom,height)+m.pad));
 return m;
}
HFONT TmaFont(float dpi,bool title){return CreateFontW(-static_cast<int>(std::ceil((title?options[3].value:options[4].value)*dpi)),0,0,0,title?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
void SyncTmaWindow(){
 if(!windowHandle)return;
 if(!advanced){if(tmaHandle)ShowWindow(tmaHandle,SW_HIDE);return;}
 if(!tmaHandle){RECT main{};GetWindowRect(windowHandle,&main);tmaHandle=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"CpuFreqTma",L"CoreScope v" CORESCOPE_VERSION_STRING L" - TMA (P cores)",WS_POPUP,tmaX==CW_USEDEFAULT?main.right+8:tmaX,tmaY==CW_USEDEFAULT?main.top:tmaY,1,1,windowHandle,nullptr,GetModuleHandleW(nullptr),nullptr);}
 if(!tmaHandle)return;
 const float dpi=static_cast<float>(GetDpiForWindow(tmaHandle))/96;HDC dc=GetDC(tmaHandle);HFONT font=TmaFont(dpi,false),bold=TmaFont(dpi,true);HGDIOBJ old=SelectObject(dc,font);const auto metrics=MeasureTma(dc,font,bold,TmaLines(),dpi);SelectObject(dc,old);DeleteObject(font);DeleteObject(bold);ReleaseDC(tmaHandle,dc);
 RECT current{};GetWindowRect(tmaHandle,&current);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(tmaHandle,MONITOR_DEFAULTTONEAREST),&monitor);
 const int width=(std::min)(metrics.width,static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)),height=(std::min)(metrics.height,static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
 tmaFullHeight=metrics.height;tmaScroll=std::clamp(tmaScroll,0,(std::max)(0,tmaFullHeight-height));
 SetWindowPos(tmaHandle,options[9].value?HWND_TOPMOST:HWND_NOTOPMOST,std::clamp(current.left,monitor.rcWork.left,monitor.rcWork.right-width),std::clamp(current.top,monitor.rcWork.top,monitor.rcWork.bottom-height),width,height,SWP_NOACTIVATE);
 SetLayeredWindowAttributes(tmaHandle,0,static_cast<BYTE>(options[0].value*255/100),LWA_ALPHA);const int radius=static_cast<int>(std::ceil(options[7].value*2*dpi));HRGN region=CreateRoundRectRgn(0,0,width+1,height+1,radius,radius);if(!SetWindowRgn(tmaHandle,region,TRUE))DeleteObject(region);
 ShowWindow(tmaHandle,IsWindowVisible(windowHandle)?SW_SHOWNOACTIVATE:SW_HIDE);InvalidateRect(tmaHandle,nullptr,FALSE);
}
LRESULT CALLBACK TmaProc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
 switch(message){
 case WM_PAINT:{PAINTSTRUCT ps{};HDC screen=BeginPaint(hwnd,&ps);RECT client{};GetClientRect(hwnd,&client);HDC dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,client.right,client.bottom);HGDIOBJ oldBitmap=SelectObject(dc,bitmap);
  HBRUSH background=CreateSolidBrush(RGB(23,30,43));FillRect(dc,&client,background);DeleteObject(background);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(237,243,255));
  const float dpi=static_cast<float>(GetDpiForWindow(hwnd))/96;HFONT font=TmaFont(dpi,false),bold=TmaFont(dpi,true);HGDIOBJ oldFont=SelectObject(dc,font);const auto lines=TmaLines();const auto metrics=MeasureTma(dc,font,bold,lines,dpi);float y=static_cast<float>(metrics.pad-tmaScroll);
  for(const auto& row:lines){SelectObject(dc,row.heading?bold:font);const int h=row.heading?metrics.title:metrics.line,top=static_cast<int>(std::round(y));RECT label{metrics.pad,top,client.right-metrics.pad-(row.value.empty()?0:metrics.value),top+h};DrawTextW(dc,row.label.c_str(),-1,&label,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);if(!row.value.empty()){RECT value{client.right-metrics.pad-metrics.value,top,client.right-metrics.pad,top+h};DrawTextW(dc,row.value.c_str(),-1,&value,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}y+=static_cast<float>(h)+metrics.rowGap;}
  BitBlt(screen,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldFont);DeleteObject(font);DeleteObject(bold);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(hwnd,&ps);return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_LBUTTONDOWN:ReleaseCapture();SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,l);return 0;
 case WM_EXITSIZEMOVE:{RECT rect{};GetWindowRect(hwnd,&rect);tmaX=rect.left;tmaY=rect.top;SyncTmaWindow();if(!selfTesting)SaveSettings();return 0;}
 case WM_DPICHANGED:{const RECT* rect=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,rect->left,rect->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);SyncTmaWindow();return 0;}
 case WM_DISPLAYCHANGE:SyncTmaWindow();return 0;
 case WM_MOUSEWHEEL:{RECT client{};GetClientRect(hwnd,&client);const int step=static_cast<int>(std::ceil(options[4].value*3.f*GetDpiForWindow(hwnd)/96));tmaScroll=std::clamp(tmaScroll-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*step,0,(std::max)(0,tmaFullHeight-static_cast<int>(client.bottom)));InvalidateRect(hwnd,nullptr,FALSE);return 0;}
 case WM_RBUTTONUP:ShowSettings();return 0;
 case WM_CLOSE:advanced=false;Layout();if(settingsHandle)UpdateSettingsLanguage(settingsHandle);if(!selfTesting)SaveSettings();return 0;
 case WM_DESTROY:tmaHandle=nullptr;return 0;
 default:return DefWindowProcW(hwnd,message,w,l);
 }
}
HMENU CreateTrayMenu(HWND hwnd) {
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(detailed && !advanced?MF_CHECKED:0),5,Tr(L"Detailed mode",L"详细模式"));AppendMenuW(menu,MF_STRING|(!detailed?MF_CHECKED:0),6,Tr(L"Compact mode",L"精简模式"));AppendMenuW(menu,MF_STRING|(advanced?MF_CHECKED:0),7,Tr(L"Advanced mode",L"进阶模式"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,1,IsWindowVisible(hwnd)?Tr(L"Hide overlay",L"隐藏浮窗"):Tr(L"Show overlay",L"显示浮窗"));AppendMenuW(menu,MF_STRING,2,Tr(L"Settings",L"打开设置"));AppendMenuW(menu,MF_STRING,3,Tr(L"About",L"关于"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,4,Tr(L"Exit",L"退出"));
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
    case WM_LBUTTONDBLCLK:ShowWindow(hwnd,SW_HIDE);SyncTmaWindow();return 0;
    case WM_EXITSIZEMOVE: Layout();if(!selfTesting) SaveSettings();return 0;
    case WM_MOUSEWHEEL: { RECT r{};GetClientRect(hwnd,&r);scrollOffset=std::clamp(scrollOffset-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*Px(rowHeight*3),0,(std::max)(0,Px(contentHeight)-static_cast<int>(r.bottom)));InvalidateRect(hwnd,nullptr,FALSE);return 0; }
    case WM_DPICHANGED: {scale=static_cast<float>(HIWORD(w))/96;const RECT* r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,0,0,SWP_NOSIZE|SWP_NOZORDER);Layout();return 0;}
    case WM_DISPLAYCHANGE:Layout();return 0;
    case SampleMessage:samplePending.store(false);InvalidateRect(hwnd,nullptr,FALSE);if(tmaHandle)SyncTmaWindow();return 0;
    case TrayMessage:
        if(l==WM_LBUTTONUP) ShowSettings();
        if(l==WM_RBUTTONUP) {
            HMENU menu=CreateTrayMenu(hwnd);
            POINT p{};GetCursorPos(&p);SetForegroundWindow(hwnd);const UINT command=static_cast<UINT>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr));DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);
            if(command==1){ShowWindow(hwnd,IsWindowVisible(hwnd)?SW_HIDE:SW_SHOWNOACTIVATE);SyncTmaWindow();}
            if(command==2) ShowSettings();
            if(command==3) MessageBoxW(hwnd,Tr(L"CoreScope " CORESCOPE_VERSION_STRING L"\nCPU and memory performance monitor\nFrequency, temperature, power, bandwidth, IPC and TMA\nGPL-3.0-only | github.com/YCTS-otree/CoreScope\nDrag anywhere; double-click to hide.\nMouse wheel scrolls tall overlays.",L"CoreScope " CORESCOPE_VERSION_STRING L"\nCPU 与内存性能监视器\n频率、温度、功耗、带宽、IPC 和 TMA\nGPL-3.0-only | github.com/YCTS-otree/CoreScope\n任意位置拖动；双击隐藏。\n超高浮窗可用滚轮查看。"),Tr(L"About",L"关于"),MB_OK);
            if(command==4) DestroyWindow(hwnd);
            if(command==5 || command==6 || command==7){advanced=command==7;detailed=command!=6;Layout();if(settingsHandle)UpdateSettingsLanguage(settingsHandle);if(!selfTesting)SaveSettings();}
        }return 0;
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);SyncTmaWindow();return 0;
    case WM_DESTROY:if(tmaHandle)DestroyWindow(tmaHandle);if(!selfTesting)SaveSettings();Tray(NIM_DELETE);SetEvent(stopEvent);PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,message,w,l);
    }
}
}
int RunMonitor(bool demo, bool selfTest, int benchmark, bool testPmu,bool testTma,int inspectMode) {
    preview=demo;selfTesting=selfTest;benchmarkSeconds=benchmark;
    struct InstanceGuard { HANDLE handle=nullptr; ~InstanceGuard() { if(handle) CloseHandle(handle); } } instance;
    // Legacy IPC identities intentionally prevent duplicate old/new instances.
    activateMessage=RegisterWindowMessageW(L"CpuFreqMonitor.Activate.v1");
    if(!selfTest) {
        instance.handle=CreateMutexW(nullptr,FALSE,L"Local\\CpuFreqMonitor.SingleInstance.v1");
        const DWORD mutexError=GetLastError();
        if(!instance.handle) { MessageBoxW(nullptr,L"Cannot establish the single-instance lock.",L"CoreScope",MB_ICONERROR); return 1; }
        if(mutexError==ERROR_ALREADY_EXISTS) {
            for(int attempt=0;attempt<50;++attempt) {
                HWND existing=FindWindowW(L"CpuFreqOverlay",nullptr);
                if(existing) { DWORD process=0;GetWindowThreadProcessId(existing,&process);AllowSetForegroundWindow(process);PostMessageW(existing,activateMessage,0,0);return 0; }
                Sleep(100);
            }
            MessageBoxW(nullptr,L"The monitor is already starting. Please try again shortly.",L"CoreScope",MB_OK);
            return 0;
        }
    } else {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        std::wstring log(executable);log=log.substr(0,log.find_last_of(L"\\")+1)+(benchmark?L"benchmark.txt":demo?L"ui-test.txt":L"sensor-test.txt");
        FILE* stream=nullptr;_wfreopen_s(&stream,log.c_str(),L"w",stdout);
        iniPath=log+L".ini";
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES};InitCommonControlsEx(&controls);
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory)) || FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&textFactory)))) return 1;
    topology=DetectTopology();cpuTitle=ShortCpuName(topology.name);BuildDisplayGroups();for(const auto& cpu:topology.logical)rows.push_back({cpu.group,cpu.number});
    if(rows.empty()){MessageBoxW(nullptr,L"Cannot obtain reliable CPU topology.",L"CoreScope",MB_ICONERROR);return 1;}
    processorCount = rows.size();
    LoadSettings();if(testTma)advanced=detailed=true;if(inspectMode==1 || inspectMode==3){detailed=advanced=false;}if(inspectMode>=0)ApplyPreset(2);if(inspectMode==1)contents[0].enabled=true;if(testPmu){contents[0].enabled=true;contents[2].enabled=true;}stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent)return 1;
    // Optional screenshot hold for demo self-tests; never reads or changes normal settings.
    wchar_t testGap[64]{};const bool inspectSpacing=selfTest && demo && GetEnvironmentVariableW(L"CORESCOPE_TEST_GAP",testGap,64)>0 && (wcscmp(testGap,L"compact")==0 || ParseSpacing(testGap,rowSpacing));
    if(inspectSpacing){options[0].value=100;if(wcscmp(testGap,L"compact")!=0){detailed=true;options[4].value=18;}}
    WNDCLASSEXW cls{sizeof(cls)};cls.style=CS_DBLCLKS;cls.lpfnWndProc=WindowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=selfTest?L"CpuFreqOverlayTest":L"CpuFreqOverlay";RegisterClassExW(&cls);
    cls.style=0;cls.lpfnWndProc=TmaProc;cls.lpszClassName=L"CpuFreqTma";RegisterClassExW(&cls);
    cls.style=0;cls.lpfnWndProc=SettingsProc;cls.lpszClassName=L"CpuFreqSettings";cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassExW(&cls);
    windowHandle=CreateWindowExW(((preview && !selfTest) || (benchmark>=60 && testPmu) || benchmark==60 ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW)|WS_EX_LAYERED,selfTest?L"CpuFreqOverlayTest":L"CpuFreqOverlay",selfTest?L"CoreScope v" CORESCOPE_VERSION_STRING L" (test)":L"CoreScope",WS_POPUP,inspectMode==1?900:savedX==CW_USEDEFAULT?100:savedX,savedY==CW_USEDEFAULT?100:savedY,options[1].value,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!windowHandle)return 1;
    if(inspectMode>=0)SetWindowTextW(windowHandle,inspectMode==3?L"v" CORESCOPE_VERSION_STRING L" Compact (LIVE)":inspectMode==1?L"v" CORESCOPE_VERSION_STRING L" Compact layout (DEMO)":L"v" CORESCOPE_VERSION_STRING L" Advanced (LIVE)");
    scale=static_cast<float>(GetDpiForWindow(windowHandle))/96;Layout();trayIcon=CreateTrayIcon();taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");ChangeWindowMessageFilterEx(windowHandle,taskbarCreated,MSGFLT_ALLOW,nullptr);ChangeWindowMessageFilterEx(windowHandle,activateMessage,MSGFLT_ALLOW,nullptr);Tray(NIM_ADD);
    ShowWindow(windowHandle,SW_SHOWNOACTIVATE);SyncTmaWindow();
    std::thread worker(Sample);
    if(selfTest) SetTimer(windowHandle,99,benchmark?static_cast<UINT>((benchmark+3)*1000):inspectSpacing?60000:2500,nullptr);
    FILETIME created{},exited{},kernelStart{},userStart{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelStart,&userStart);double benchmarkStart=MonotonicSeconds();if(benchmark)SetTimer(windowHandle,98,3000,nullptr);
    MSG message{};int result=0;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(benchmark && message.message==WM_TIMER && message.wParam==98){KillTimer(windowHandle,98);GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelStart,&userStart);benchmarkStart=MonotonicSeconds();std::lock_guard<std::mutex> lock(dataMutex);sampleDurations.clear();paintLatencies.clear();sampleIoctls=0;bandwidthSamples=0;bandwidthReadSum=bandwidthWriteSum=bandwidthReadMax=bandwidthWriteMax=0;continue;}
        if(selfTest && message.message==WM_TIMER && message.wParam==99) {
            KillTimer(windowHandle,99);
            if(!memoryLifecyclePassed.load())result=24;
            if(benchmark) {DestroyWindow(windowHandle);continue;}
            const auto migrationRoot = iniPath + L".migration-" + Number(static_cast<int>(GetCurrentProcessId()));
            const auto oldDirectory = migrationRoot + L"\\CpuFreqMonitor";
            const auto oldSettings = oldDirectory + L"\\settings.ini";
            const auto newDirectory = migrationRoot + L"\\CoreScope";
            const auto newSettings = newDirectory + L"\\settings.ini";
            CreateDirectoryW(migrationRoot.c_str(), nullptr);
            CreateDirectoryW(oldDirectory.c_str(), nullptr);
            const bool wroteLegacy = WritePrivateProfileStringW(L"UI", L"Width", L"417", oldSettings.c_str()) != FALSE;
            if (!wroteLegacy || ResolveSettingsPath(migrationRoot) != newSettings || GetPrivateProfileIntW(L"UI", L"Width", 0, newSettings.c_str()) != 417) result=32;
            WritePrivateProfileStringW(L"UI", L"Width", L"518", newSettings.c_str());
            if (ResolveSettingsPath(migrationRoot) != newSettings || GetPrivateProfileIntW(L"UI", L"Width", 0, newSettings.c_str()) != 518 || GetPrivateProfileIntW(L"UI", L"Width", 0, oldSettings.c_str()) != 417) result=33;
            DeleteFileW(newSettings.c_str()); DeleteFileW(oldSettings.c_str());
            RemoveDirectoryW(newDirectory.c_str()); RemoveDirectoryW(oldDirectory.c_str()); RemoveDirectoryW(migrationRoot.c_str());
            std::printf("CoreScope settings migration / existing configuration precedence exit=%d\n", result);
            ShowSettings();
            SendDlgItemMessageW(settingsHandle,303,CB_SETCURSEL,1,0);
            SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(303,CBN_SELCHANGE),0);
            wchar_t translated[64]{};GetWindowTextW(GetDlgItem(settingsHandle,301),translated,64);
            if(wcscmp(translated,L"关闭")!=0 || language!=1)result=6;
            HMENU translatedMenu=CreateTrayMenu(windowHandle);GetMenuStringW(translatedMenu,2,translated,64,MF_BYCOMMAND);DestroyMenu(translatedMenu);
            if(wcscmp(translated,L"打开设置")!=0)result=8;
            SaveSettings();language=0;usageMode.store(1);ReadPreferences();
            if(language!=1 || usageMode.load()!=0)result=9;
            SendDlgItemMessageW(settingsHandle,303,CB_SETCURSEL,0,0);
            SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(303,CBN_SELCHANGE),0);
            GetWindowTextW(GetDlgItem(settingsHandle,301),translated,64);if(wcscmp(translated,L"Close")!=0)result=7;
            translatedMenu=CreateTrayMenu(windowHandle);GetMenuStringW(translatedMenu,2,translated,64,MF_BYCOMMAND);DestroyMenu(translatedMenu);
            if(wcscmp(translated,L"Settings")!=0)result=10;
            detailed=true;Layout();const float detailedHeight=contentHeight;
            SendDlgItemMessageW(settingsHandle,307,CB_SETCURSEL,1,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(307,CBN_SELCHANGE),0);
            if(detailed || contentHeight>=detailedHeight)result=13;
            SendDlgItemMessageW(settingsHandle,309,CB_SETCURSEL,0,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(309,CBN_SELCHANGE),0);
            if(options[1].value!=230 || options[4].value!=9)result=14;
            SendDlgItemMessageW(settingsHandle,309,CB_SETCURSEL,1,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(309,CBN_SELCHANGE),0);
            if(options[1].value!=250 || options[4].value!=10)result=15;
            SetWindowTextW(edits[8],L"10");if(interval.load()!=100)result=16;
            SetWindowTextW(edits[8],L"4");
            TabCtrl_SetCurSel(GetDlgItem(settingsHandle,500),1);NMHDR changed{};changed.idFrom=500;changed.code=TCN_SELCHANGE;
            SendMessageW(settingsHandle,WM_NOTIFY,500,reinterpret_cast<LPARAM>(&changed));
            if(IsWindowVisible(edits[0]) || !IsWindowVisible(GetDlgItem(settingsHandle,400)))result=17;
            SendDlgItemMessageW(settingsHandle,400,BM_SETCHECK,BST_CHECKED,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(400,BN_CLICKED),0);
            if(!contents[0].enabled || ipcWidth<=0)result=18;
            const auto titleEN=cpuTitle;const float widthEN=contentWidth;
            language=1;UpdateSettingsLanguage(settingsHandle);Layout();if(cpuTitle!=titleEN || contentWidth!=widthEN)result=19;
            SaveSettings();detailed=true;contents[0].enabled=false;ReadPreferences();if(detailed || !contents[0].enabled || !contents[1].enabled)result=20;
            SendDlgItemMessageW(settingsHandle,401,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(401,BN_CLICKED),0);
            if(memoryEnabled.load())result=22;
            SendDlgItemMessageW(settingsHandle,401,BM_SETCHECK,BST_CHECKED,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(401,BN_CLICKED),0);
            if(!memoryEnabled.load())result=23;
            detailed=true;contents[0].enabled=false;Layout();
            if(CoreRowHeight(CoreKind::Performance)!=rowHeight)result=21;
            TabCtrl_SetCurSel(GetDlgItem(settingsHandle,500),0);SendMessageW(settingsHandle,WM_NOTIFY,500,reinterpret_cast<LPARAM>(&changed));
            SendDlgItemMessageW(settingsHandle,307,CB_SETCURSEL,2,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(307,CBN_SELCHANGE),0);
            if(!advanced || !detailed || !(pmuOptions.load()&4u) || !tmaHandle || !IsWindowVisible(tmaHandle))result=28;
            SaveSettings();advanced=false;ReadPreferences();if(!advanced || !detailed)result=29;
            SendDlgItemMessageW(settingsHandle,309,CB_SETCURSEL,2,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(309,CBN_SELCHANGE),0);
            if(options[1].value!=180 || options[4].value!=8 || options[8].value!=1)result=30;
            SendDlgItemMessageW(settingsHandle,307,CB_SETCURSEL,1,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(307,CBN_SELCHANGE),0);
            if(advanced || detailed || (pmuOptions.load()&4u) || IsWindowVisible(tmaHandle))result=31;
            SendDlgItemMessageW(settingsHandle,309,CB_SETCURSEL,1,0);SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(309,CBN_SELCHANGE),0);
            std::printf("New UI checks: mode/presets/refresh/tabs/content/persistence/EN/P-core spacing exit=%d\n",result);
            // Exercise actual edit notifications and INI round trips, including partial typing.
            float parsed=0;
            for(const auto* invalid:{L"",L"-",L".",L"NaN",L"inf",L"1.5junk",L"-40.1",L"40.1"})if(ParseSpacing(invalid,parsed))result=34;
            if(GetWindowLongPtrW(edits[2],GWL_STYLE)&ES_NUMBER)result=35;
            for(const auto* spacing:{L"-1.5",L"0.25",L"-40",L"40"}){
                SetWindowTextW(edits[2],spacing);ParseSpacing(spacing,parsed);if(rowSpacing!=parsed)result=36;
                SaveSettings();rowSpacing=5;ReadPreferences();if(rowSpacing!=parsed)result=37;
                for(bool mode:{false,true}){detailed=mode;Layout();if(rowHeight<1 || CoreRowHeight(CoreKind::Unknown)<1 || CoreRowHeight(CoreKind::Performance)<1)result=38;}
            }
            SetWindowTextW(edits[2],L"-1.5");SetWindowTextW(edits[2],L"-");
            SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(102,EN_KILLFOCUS),reinterpret_cast<LPARAM>(edits[2]));
            wchar_t spacingText[64]{};GetWindowTextW(edits[2],spacingText,64);if(wcscmp(spacingText,L"-1.5")!=0)result=39;
            HDC spacingDc=GetDC(windowHandle);HFONT spacingFont=TmaFont(1.25f,false),spacingBold=TmaFont(1.25f,true);HGDIOBJ spacingOld=SelectObject(spacingDc,spacingFont);
            const auto spacingMetrics=MeasureTma(spacingDc,spacingFont,spacingBold,TmaLines(),1.25f);
            if(spacingMetrics.rowGap!=-1.875f || spacingMetrics.height<=0 || spacingMetrics.width<=0)result=41;
            SelectObject(spacingDc,spacingOld);DeleteObject(spacingFont);DeleteObject(spacingBold);ReleaseDC(windowHandle,spacingDc);
            ApplyPreset(1);if(rowSpacing!=1)result=40;SetWindowTextW(edits[2],OptionText(2).c_str());Layout();
            SetWindowTextW(edits[2],L"0");const float zeroHeader=CoreHeaderHeight(),zeroHeight=contentHeight;
            SetWindowTextW(edits[2],L"-2.5");
            if(std::abs(CoreHeaderHeight()-(zeroHeader-2.5f))>.001f || CoreGroupGap()!=0 || contentHeight>=zeroHeight)result=42;
            SetWindowTextW(edits[2],L"5");if(CoreGroupGap()!=5 || CoreHeaderHeight()<=zeroHeader)result=43;
            std::printf("Core header/own bars spacing reacts to zero, negative fractional and positive values exit=%d\n",result);
            const int oldDefault[]={92,340,5,19,13,12,14,12};
            for(int i=1;i<8;++i)options[i].value=oldDefault[i];rowSpacing=5;detailed=true;SaveSettings();ReadPreferences();
            if(!detailed || options[1].value!=250 || options[4].value!=10)result=44;
            for(int i=1;i<8;++i)options[i].value=oldDefault[i];rowSpacing=5;detailed=false;SaveSettings();ReadPreferences();
            if(detailed || options[1].value!=250 || options[4].value!=10)result=46;
            for(int i=1;i<8;++i)options[i].value=oldDefault[i];rowSpacing=-2.5f;detailed=true;SaveSettings();ReadPreferences();
            if(!detailed || options[1].value!=340 || rowSpacing!=-2.5f)result=45;
            ApplyPreset(1);SetWindowTextW(edits[2],OptionText(2).c_str());Layout();
            std::printf("Smaller presets and legacy default/custom settings migration exit=%d\n",result);
            std::vector<bool> savedContent;for(auto& c:contents){savedContent.push_back(c.enabled);c.enabled=true;}
            ApplyPreset(2);detailed=true;Layout();const float laptopWidth=contentWidth;
            if(std::abs(laptopWidth-180)>.01f || rowSpacing!=-2 || options[5].value!=7 || std::abs(FooterRowHeight()-(textHeight-2))>.01f)result=47;
            options[5].value=8;SaveSettings();ReadPreferences();if(options[5].value!=7)result=50;
            SetWindowTextW(edits[1],L"230");if(contentWidth<=laptopWidth || std::abs(contentWidth-230)>.01f)result=48;
            UpdateSettingsLanguage(settingsHandle);wchar_t laptopLabel[64]{};SendDlgItemMessageW(settingsHandle,309,CB_GETLBTEXT,2,reinterpret_cast<LPARAM>(laptopLabel));
            if(wcscmp(laptopLabel,language?L"很小（笔记本）":L"Very small (Laptop)")!=0)result=49;
            for(int i=0;i<ContentCount;++i)contents[i].enabled=savedContent[i];ApplyPreset(1);Layout();
            std::printf("Laptop preset snapshot, footer spacing and 180/230 DIP width response exit=%d\n",result);
            std::printf("Signed/fractional spacing: input, invalid/partial values, persistence, layout bounds and presets exit=%d\n",result);
            SetWindowTextW(edits[4],L"36");SetWindowTextW(edits[1],L"220");
            if(contentWidth-labelWidth-frequencyWidth-ipcWidth-busyWidth-2*options[6].value<32)result=2;
            std::size_t displayed=0;for(const auto& group:displayGroups){if(group.kind==CoreKind::Efficiency && group.physical.size()>6)result=11;for(auto core:group.physical)displayed+=topology.cores[core].siblings.size();}if(displayed!=processorCount)result=12;
            SetWindowTextW(edits[0],L"1");SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(100,EN_KILLFOCUS),reinterpret_cast<LPARAM>(edits[0]));
            if(options[0].value!=92) result=3;
            SendMessageW(windowHandle,WM_LBUTTONDBLCLK,0,0);if(IsWindowVisible(windowHandle))result=4;
            ShowWindow(windowHandle,SW_SHOWNOACTIVATE);SyncTmaWindow();UpdateWindow(windowHandle);
            {std::lock_guard<std::mutex> lock(dataMutex);if(preview && rows[0].total<0)result=5; std::printf("Sensor mode: %s; temperature=%.1f C\n",preview?"demo":"hardware",temperature); for(std::size_t i=0;i<rows.size();++i) std::printf("Core%zu total=%.2f kernel=%.2f GHz=%.3f time=%.2f utility=%.2f\n",i,rows[i].total,rows[i].kernel,rows[i].ghz,rows[i].time,rows[i].utility);}
            std::printf("UI checks exit=%d\n",result); std::fflush(stdout); DestroyWindow(settingsHandle);DestroyWindow(windowHandle);continue;
        }
        if(!settingsHandle || !IsDialogMessageW(settingsHandle,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
    }
    SetEvent(stopEvent);worker.join();CloseHandle(stopEvent);
    if(selfTest){
        FILETIME kernelEnd{},userEnd{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelEnd,&userEnd);
        auto ticks=[](FILETIME t){return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
        const double elapsed=MonotonicSeconds()-benchmarkStart,cpuSeconds=static_cast<double>(ticks(kernelEnd)+ticks(userEnd)-ticks(kernelStart)-ticks(userStart))/1e7;
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
        std::sort(sampleDurations.begin(),sampleDurations.end());double mean=0;for(double duration:sampleDurations)mean+=duration;if(!sampleDurations.empty())mean/=static_cast<double>(sampleDurations.size());
        std::printf("Elapsed=%.2fs CPU=%.3fs one_core=%.3f%% machine=%.3f%% working_set=%.2fMiB private=%.2fMiB samples=%zu IOCTL/s=%.1f latency_mean=%.3fms p95=%.3fms\n",elapsed,cpuSeconds,cpuSeconds/elapsed*100,cpuSeconds/elapsed*100/static_cast<double>(processorCount),memory.WorkingSetSize/1048576.0,memory.PrivateUsage/1048576.0,sampleDurations.size(),static_cast<double>(sampleIoctls)/elapsed,mean,sampleDurations.empty()?0:sampleDurations[static_cast<std::size_t>(static_cast<double>(sampleDurations.size()-1)*.95)]);
        if(bandwidthSamples)std::printf("RAM mean R=%.3f W=%.3f max R=%.3f W=%.3f GB/s\n",bandwidthReadSum/bandwidthSamples,bandwidthWriteSum/bandwidthSamples,bandwidthReadMax,bandwidthWriteMax);
        std::sort(paintLatencies.begin(),paintLatencies.end());if(!paintLatencies.empty())std::printf("publish_to_paint_p95=%.3fms paints=%zu\n",paintLatencies[static_cast<std::size_t>(static_cast<double>(paintLatencies.size()-1)*.95)],paintLatencies.size());
        std::printf("CPU family=%X model=%X physical=%zu logical=%zu package_temp=%.1f power=%.2f RAM_R=%.3f RAM_W=%.3f PMU_error=%lu IMC_error=%lu\n",topology.family,topology.model,topology.cores.size(),topology.logical.size(),hardwareReading.packageTemperature,hardwareReading.packageWatts,hardwareReading.readGBs,hardwareReading.writeGBs,pmuError,imcError);
        std::printf("Sensors error=%lu frequency_source=%s\n",hardwareReading.sensorError,hardwareReading.osFrequency?"Windows reported":"hardware");
        std::printf("TMA valid_samples=%u error=%lu slots=%llu Retiring=%.3f Frontend=%.3f BadSpec=%.3f Backend=%.3f Core=%.3f Memory=%.3f\n",tmaGoodSamples,tmaError,tmaReading.slots,tmaReading.retiring,tmaReading.frontend,tmaReading.badSpec,tmaReading.backend,tmaReading.core,tmaReading.memory);
        std::printf("Memory Bound valid_samples=%u cycles=%llu L1=%.3f L2=%.3f L3=%.3f DRAM=%.3f top_age=%.2f memory_age=%.2f\n",tmaMemoryGoodSamples,tmaReading.memoryCycles,tmaReading.l1,tmaReading.l2,tmaReading.l3,tmaReading.dram,tmaReading.topAge,tmaReading.memoryAge);
        if(testTma && (tmaGoodSamples<3 || tmaMemoryGoodSamples<3))result=27;
        if(testPmu && pmuGoodSamples<3)result=26;std::printf("ETW valid_samples=%u\n",pmuGoodSamples);
        std::printf("ETW IPC valid=%zu/%zu LLC_MPKI_P=%.4f\n",static_cast<std::size_t>(std::count_if(rows.begin(),rows.end(),[](const Row& row){return row.ipc>=0;})),rows.size(),hardwareReading.llcMpki);
        for(std::size_t i=0;i<topology.logical.size();++i){const auto& cpu=topology.logical[i];std::printf("LP%zu group=%u number=%u physical=%u package=%u kind=%u APIC=%u hwcore=%u PMUv=%u fixed=%u width=%u\n",i,cpu.group,cpu.number,cpu.physical,cpu.package,static_cast<unsigned>(cpu.kind),cpu.apicId,cpu.hardwareCore,cpu.pmuVersion,cpu.fixedCount,cpu.fixedWidth);}
        std::fflush(stdout);
    }
    if(trayIcon)DestroyIcon(trayIcon);if(settingsFont)DeleteObject(settingsFont);
    DropTarget();Release(normalFont);Release(titleFont);Release(textFactory);Release(factory);CoUninitialize();return result;
}
