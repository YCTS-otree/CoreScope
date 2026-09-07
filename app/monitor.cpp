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
#include "hardware.h"
#include <psapi.h>
#include "../shared/version.h"

namespace {
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT SampleMessage = WM_APP + 2;
struct Row { WORD group; BYTE cpu; double total = -1, kernel = -1, ghz = -1, time = -1, utility = -1; int busyAlert=0; };
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
    {L"RefreshV2", L"Sample interval (ms)", 250, 100, 5000},
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
int benchmarkSeconds=0;
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
float scale = 1, contentWidth = 340, contentHeight = 100, rowHeight = 20, labelWidth = 50, frequencyWidth = 70, titleHeight = 24, textHeight = 18;
int scrollOffset = 0;
int savedX = CW_USEDEFAULT, savedY = CW_USEDEFAULT;
bool preview = false;
bool selfTesting = false;
int language = 0;
std::atomic<int> usageMode{0};
const wchar_t* Tr(const wchar_t* en, const wchar_t* zh) { return language == 1 ? zh : en; }
const wchar_t* chineseLabels[] = {L"不透明度 (%)", L"窗口宽度 (DIP)", L"行间距 (DIP)", L"标题字号 (DIP)", L"条目字号 (DIP)", L"进度条高度 (DIP)", L"窗口内边距 (DIP)", L"圆角半径 (DIP)", L"采样间隔 (ms)", L"始终置顶 (0 / 1)"};


template<class T> void Release(T*& object) { if (object) { object->Release(); object = nullptr; } }
void DropTarget() { Release(textBrush);Release(mutedBrush);Release(backgroundBrush);Release(blueBrush);Release(kernelBrush);Release(orangeBrush);Release(redBrush);Release(target); }
int Px(float dip) { return static_cast<int>(std::ceil(dip * scale)); }
std::wstring Number(int value) { return std::to_wstring(value); }
void SaveSettings() {
    for (const auto& o : options) WritePrivateProfileStringW(L"UI", o.key, Number(o.value).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"Language", language ? L"zh-CN" : L"en", iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"BusyModeV2", Number(usageMode.load()).c_str(), iniPath.c_str());
    RECT rect{}; GetWindowRect(windowHandle, &rect);
    WritePrivateProfileStringW(L"UI", L"X", Number(rect.left).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"UI", L"Y", Number(rect.top).c_str(), iniPath.c_str());
}
void ReadPreferences() {
    for (auto& o : options) o.value = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"UI", o.key, o.value, iniPath.c_str())), o.minimum, o.maximum);
    wchar_t selectedLanguage[16]{};
    GetPrivateProfileStringW(L"UI", L"Language", PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? L"zh-CN" : L"en", selectedLanguage, 16, iniPath.c_str());
    language = wcscmp(selectedLanguage,L"zh-CN") == 0 ? 1 : 0;
    usageMode.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"UI",L"BusyModeV2",0,iniPath.c_str())),0,1));
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
    titleHeight = Measure(topology.name, titleFont).height;
    textHeight = Measure(Tr(L"Temperature: 100.0 \u00b0C",L"温度: 100.0 \u00b0C"), normalFont).height;
    labelWidth = Measure(L"Core" + Number(static_cast<int>(processorCount) - 1), normalFont).width + 10;
    frequencyWidth = Measure(L"00.00G", normalFont).width + 12;
    temperatureColumnWidth=Measure(L"100°",normalFont).width+8;ipcWidth=Measure(L"N/A",normalFont).width+12;busyWidth=Measure(L"100%",normalFont).width+12;
    const float padding = static_cast<float>(options[6].value);
    contentWidth = (std::max)({static_cast<float>(options[1].value), labelWidth + frequencyWidth + ipcWidth + busyWidth + 70 + 2 * padding, Measure(topology.name, titleFont).width + 2 * padding, Measure(Tr(L"Temperature: 100.0 \u00b0C",L"温度: 100.0 \u00b0C"), normalFont).width + 2 * padding});
    rowHeight = (std::max)(textHeight, static_cast<float>(options[5].value)) + static_cast<float>(options[2].value);

    RECT current{}; GetWindowRect(windowHandle, &current);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST), &monitor);
    contentWidth=(std::min)(contentWidth,static_cast<float>(monitor.rcWork.right-monitor.rcWork.left)/scale);
    titleHeight=Measure(topology.name,titleFont,contentWidth-2*padding).height;
    contentHeight=padding*2+titleHeight+12+textHeight*2+rowHeight*static_cast<float>(processorCount)+(textHeight+10)*static_cast<float>(displayGroups.size())+textHeight*6+20;
    const int width=Px(contentWidth);
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
    PAINTSTRUCT ps{};BeginPaint(windowHandle,&ps);RECT rect{};GetClientRect(windowHandle,&rect);
    if(!target) {
        factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(windowHandle,D2D1::SizeU(static_cast<UINT32>(rect.right),static_cast<UINT32>(rect.bottom))),&target);
        if(target){target->CreateSolidColorBrush(D2D1::ColorF(0xEDF3FF),&textBrush);target->CreateSolidColorBrush(D2D1::ColorF(0xA5B4CD),&mutedBrush);target->CreateSolidColorBrush(D2D1::ColorF(0x29354A),&backgroundBrush);target->CreateSolidColorBrush(D2D1::ColorF(0x429BFF),&blueBrush);target->CreateSolidColorBrush(D2D1::ColorF(0x2055AD),&kernelBrush);target->CreateSolidColorBrush(D2D1::ColorF(0xFFA640),&orangeBrush);target->CreateSolidColorBrush(D2D1::ColorF(0xFF5E65),&redBrush);}
    }
    if(target && normalFont && titleFont && textBrush && mutedBrush && backgroundBrush && blueBrush && kernelBrush && orangeBrush && redBrush){
        static std::vector<Row> snapshot;static HardwareReading sensors;static std::vector<int> alerts;std::wstring status;int packageAlert=0;
        {std::lock_guard<std::mutex> lock(dataMutex);snapshot=rows;sensors=hardwareReading;alerts=temperatureAlerts;packageAlert=packageTemperatureAlert;status=sampleStatus;if(selfTesting && publishedAt>0 && paintLatencies.size()<4000){paintLatencies.push_back((MonotonicSeconds()-publishedAt)*1000);publishedAt=0;}}
        target->SetDpi(96*scale,96*scale);target->BeginDraw();target->Clear(D2D1::ColorF(0x171E2B));target->SetTransform(D2D1::Matrix3x2F::Translation(0,-static_cast<float>(scrollOffset)/scale));
        auto color=[&](int alert){return alert==2?redBrush:alert==1?orangeBrush:textBrush;};
        auto format=[](double value,const wchar_t* pattern){wchar_t text[96]{};if(value<0)wcscpy_s(text,L"N/A");else swprintf_s(text,pattern,value);return std::wstring(text);};
        const float p=static_cast<float>(options[6].value),width=contentWidth-2*p;
        const float ipcX=contentWidth-p-ipcWidth,freqX=ipcX-frequencyWidth,busyX=freqX-busyWidth,barX=p+labelWidth,barRight=busyX-8;
        DrawTextLine(topology.name,titleFont,p,p,width,titleHeight+2,textBrush);
        float y=p+titleHeight+8;
        const std::wstring powerText=format(sensors.packageWatts,L"%.1f W");
        const float powerColumn=Measure(L"999.9 W",normalFont).width+20,packageLabel=Measure(L"Package: ",normalFont).width;
        DrawTextLine(powerText,normalFont,p,y,powerColumn,textHeight+2,textBrush);
        DrawTextLine(L"Package: ",normalFont,p+powerColumn,y,packageLabel,textHeight+2,mutedBrush);
        DrawTextLine(format(sensors.packageTemperature,L"%.1f \u00b0C"),normalFont,p+powerColumn+packageLabel,y,width-powerColumn-packageLabel,textHeight+2,color(packageAlert));y+=textHeight+6;
        DrawTextLine(usageMode.load()?L"Utility":L"Busy Time",normalFont,p,y,busyX-p,textHeight+2,mutedBrush);
        DrawTextLine(L"Freq",normalFont,freqX,y,frequencyWidth,textHeight+2,mutedBrush);DrawTextLine(L"IPC",normalFont,ipcX,y,ipcWidth,textHeight+2,mutedBrush);y+=textHeight+4;
        for(const auto& group:displayGroups){
            std::wstring label=CoreKindName(group.kind);if(group.physical.size()==1)label+=L" "+Number(static_cast<int>(group.physical.front()));
            DrawTextLine(label,normalFont,p,y,labelWidth+75,textHeight+2,mutedBrush);
            float temperatureX=contentWidth-p;
            for(auto it=group.physical.rbegin();it!=group.physical.rend();++it){const auto core=*it;const auto value=core<sensors.cores.size()?sensors.cores[core].temperature:-1;const auto tempText=format(value,L"%.0f\u00b0");const float w=temperatureColumnWidth;temperatureX-=w;DrawTextLine(tempText,normalFont,temperatureX,y,w,textHeight+2,color(core<alerts.size()?alerts[core]:0));}
            y+=textHeight+6;
            for(const auto physical:group.physical)for(const auto i:topology.cores[physical].siblings){
                const auto& row=snapshot[i];const float h=rowHeight-static_cast<float>(options[2].value),textY=y+(h-textHeight)/2;
                DrawTextLine(Number(static_cast<int>(i)),normalFont,p,textY,labelWidth,textHeight+2,mutedBrush);
                const float top=y+(h-static_cast<float>(options[5].value))/2,bottom=top+static_cast<float>(options[5].value);
                target->FillRectangle(D2D1::RectF(barX,top,barRight,bottom),backgroundBrush);
                if(row.total>=0)target->FillRectangle(D2D1::RectF(barX,top,barX+(barRight-barX)*static_cast<float>(row.total/100),bottom),row.busyAlert?color(row.busyAlert):blueBrush);
                if(row.kernel>=0)target->FillRectangle(D2D1::RectF(barX,top,barX+(barRight-barX)*static_cast<float>(row.kernel/100),bottom),kernelBrush);
                DrawTextLine(format(row.total,L"%.0f%%"),normalFont,busyX,textY,busyWidth,textHeight+2,color(row.busyAlert));
                DrawTextLine(format(row.ghz,L"%.2fG"),normalFont,freqX,textY,frequencyWidth,textHeight+2,textBrush);
                DrawTextLine(L"N/A",normalFont,ipcX,textY,ipcWidth,textHeight+2,mutedBrush);y+=rowHeight;
            }y+=4;
        }
        y+=8;
        // A fixed, labelled display scale; it is not a measured bandwidth limit.
        for(int direction=0;direction<2;++direction){const double value=direction?sensors.writeGBs:sensors.readGBs;DrawTextLine(direction?L"RAM W":L"RAM R",normalFont,p,y,labelWidth+10,textHeight+2,mutedBrush);
            const float left=p+labelWidth+10,right=freqX-8;const float top=y+(textHeight-8)/2;
            target->FillRectangle(D2D1::RectF(left,top,right,top+8),backgroundBrush);if(value>=0)target->FillRectangle(D2D1::RectF(left,top,left+(right-left)*static_cast<float>(std::clamp(value/100,0.0,1.0)),top+8),blueBrush);
            DrawTextLine(format(value,L"%.1f GB/s"),normalFont,freqX,y,frequencyWidth+ipcWidth,textHeight+2,textBrush);y+=textHeight+4;}
        DrawTextLine(Tr(L"RAM scale: 100 GB/s",L"带宽条刻度：100 GB/s"),normalFont,p,y,width,textHeight+2,mutedBrush);y+=textHeight+4;
        DrawTextLine(L"LLC MPKI: N/A",normalFont,p,y,width,textHeight+2,mutedBrush);y+=textHeight+4;
        DrawTextLine(preview?Tr(L"DEMO - simulated CPU data",L"演示模式 - 模拟 CPU 数据"):Tr(L"IPC / LLC: PMU unavailable",L"IPC / LLC：PMU 不可用"),normalFont,p,y,width,textHeight+2,mutedBrush);
        if(target->EndDraw()==D2DERR_RECREATE_TARGET)DropTarget();
    }
    EndPaint(windowHandle,&ps);
}
void Sample() {
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring directory(path);directory=directory.substr(0,directory.find_last_of(L"\\")+1);
    HardwareSampler hardware(topology);if(!preview)hardware.Initialize(directory,selfTesting);
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
        hardware.Poll(sensors);
        for(std::size_t i=0;i<next.size();++i){auto& row=next[i];row.total=row.kernel=row.ghz=-1;
            auto readCounter=[&](PDH_HCOUNTER counter){PDH_FMT_COUNTERVALUE value{};if(collected && counter && PdhGetFormattedCounterValue(counter,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,nullptr,&value)==ERROR_SUCCESS && (value.CStatus==PDH_CSTATUS_VALID_DATA || value.CStatus==PDH_CSTATUS_NEW_DATA) && std::isfinite(value.doubleValue))return (std::max)(value.doubleValue,0.0);return -1.0;};
            row.time=readCounter(total[i]);row.utility=selectedMode?readCounter(utility[i]):-1;
            row.total=selectedMode?row.utility:row.time;row.kernel=readCounter(selectedMode?privilegedUtility[i]:kernel[i]);if(row.total>=0){row.total=(std::min)(row.total,100.0);++available;}if(row.total>=0 && row.kernel>=0)row.kernel=(std::min)(row.total,row.kernel);else row.kernel=-1;
            if(i<sensors.frequency.size())row.ghz=sensors.frequency[i];
            if(preview){row.time=row.total=static_cast<double>((i*17+tick*7)%101);row.kernel=row.total*0.3;row.ghz=topology.logical[i].kind==CoreKind::Efficiency?4.0:5.2;}
            row.busyAlert=busyFilters[i].Update(row.time,dt,90,98);
        }
        for(std::size_t i=0;i<sensors.cores.size();++i){auto& core=sensors.cores[i];if(preview){core.temperature=52;core.tjMax=100;}thermalAlerts[i]=thermalFilters[i].Update(core.temperature,dt,static_cast<double>(core.tjMax-15),static_cast<double>(core.tjMax-5));if(core.throttled){thermalAlerts[i]=2;thermalFilters[i].level=2;thermalFilters[i].heldSeconds=0;}}
        int packageTj=0;for(const auto& core:sensors.cores)if(core.tjMax>0)packageTj=packageTj==0?core.tjMax:(std::min)(packageTj,core.tjMax);
        int packageAlert=packageFilter.Update(sensors.packageTemperature,dt,packageTj-15.0,packageTj-5.0);if(sensors.packageThrottled){packageAlert=2;packageFilter.level=2;packageFilter.heldSeconds=0;}
        const double duration=(MonotonicSeconds()-start)*1000;
        waitMs=static_cast<DWORD>((std::max)(1.0,static_cast<double>(interval.load())-duration));
        {std::lock_guard<std::mutex> lock(dataMutex);rows.swap(next);hardwareReading=sensors;temperature=sensors.packageTemperature;temperatureAlerts=thermalAlerts;packageTemperatureAlert=packageAlert;pmuError=hardware.PmuError();imcError=hardware.ImcError();sampleIoctls+=sensors.ioctlCount;if(selfTesting && sensors.readGBs>=0 && sensors.writeGBs>=0){bandwidthReadSum+=sensors.readGBs;bandwidthWriteSum+=sensors.writeGBs;bandwidthReadMax=(std::max)(bandwidthReadMax,sensors.readGBs);bandwidthWriteMax=(std::max)(bandwidthWriteMax,sensors.writeGBs);++bandwidthSamples;}if(selfTesting && sampleDurations.size()<4000)sampleDurations.push_back(duration);publishedAt=MonotonicSeconds();sampleStatus=preview?L"DEMO":available==static_cast<int>(next.size())?L"Ready":L"Unavailable";}
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
    NOTIFYICONDATAW data{}; data.cbSize=sizeof(data);data.hWnd=windowHandle;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=TrayMessage;data.hIcon=trayIcon;wcscpy_s(data.szTip,L"CPU Freq Moniter 1.4.0");
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
    case SampleMessage:samplePending.store(false);InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case TrayMessage:
        if(l==WM_LBUTTONUP) ShowSettings();
        if(l==WM_RBUTTONUP) {
            HMENU menu=CreateTrayMenu(hwnd);
            POINT p{};GetCursorPos(&p);SetForegroundWindow(hwnd);const UINT command=static_cast<UINT>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr));DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);
            if(command==1) ShowWindow(hwnd,IsWindowVisible(hwnd)?SW_HIDE:SW_SHOWNOACTIVATE);
            if(command==2) ShowSettings();
            if(command==3) MessageBoxW(hwnd,Tr(L"CPU Freq Moniter 1.4.0\nIntel frequency via PawnIO\nTemperature: core and package sensors\nBlue = total; dark blue = kernel portion\nDrag anywhere; double-click to hide.\nMouse wheel scrolls tall overlays.",L"CPU Freq Moniter 1.4.0\n通过 PawnIO 读取 Intel 频率\n温度：核心与封装传感器\n蓝色表示总占用，深蓝色表示其中的内核占用\n任意位置拖动；双击隐藏。\n超高浮窗可用滚轮查看。"),Tr(L"About",L"关于"),MB_OK);
            if(command==4) DestroyWindow(hwnd);
        }return 0;
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);return 0;
    case WM_DESTROY:if(!selfTesting)SaveSettings();Tray(NIM_DELETE);SetEvent(stopEvent);PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,message,w,l);
    }
}
}
int RunMonitor(bool demo, bool selfTest, int benchmark) {
    preview=demo;selfTesting=selfTest;benchmarkSeconds=benchmark;
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
        std::wstring log(executable);log=log.substr(0,log.find_last_of(L"\\")+1)+(benchmark?L"benchmark.txt":demo?L"ui-test.txt":L"sensor-test.txt");
        FILE* stream=nullptr;_wfreopen_s(&stream,log.c_str(),L"w",stdout);
        iniPath=log+L".ini";
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory)) || FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&textFactory)))) return 1;
    topology=DetectTopology();BuildDisplayGroups();for(const auto& cpu:topology.logical)rows.push_back({cpu.group,cpu.number});
    if(rows.empty()){MessageBoxW(nullptr,L"Cannot obtain reliable CPU topology.",L"CPU Freq Moniter",MB_ICONERROR);return 1;}
    processorCount = rows.size();
    LoadSettings();stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent)return 1;
    WNDCLASSEXW cls{sizeof(cls)};cls.style=CS_DBLCLKS;cls.lpfnWndProc=WindowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=selfTest?L"CpuFreqOverlayTest":L"CpuFreqOverlay";RegisterClassExW(&cls);
    cls.style=0;cls.lpfnWndProc=SettingsProc;cls.lpszClassName=L"CpuFreqSettings";cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassExW(&cls);
    windowHandle=CreateWindowExW(((preview && !selfTest) || benchmark==60 ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW)|WS_EX_LAYERED,selfTest?L"CpuFreqOverlayTest":L"CpuFreqOverlay",L"CPU Freq Moniter",WS_POPUP,savedX==CW_USEDEFAULT?100:savedX,savedY==CW_USEDEFAULT?100:savedY,340,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!windowHandle)return 1;
    scale=static_cast<float>(GetDpiForWindow(windowHandle))/96;Layout();trayIcon=CreateTrayIcon();taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");ChangeWindowMessageFilterEx(windowHandle,taskbarCreated,MSGFLT_ALLOW,nullptr);ChangeWindowMessageFilterEx(windowHandle,activateMessage,MSGFLT_ALLOW,nullptr);Tray(NIM_ADD);
    ShowWindow(windowHandle,SW_SHOWNOACTIVATE);
    ShowWindow(windowHandle,SW_SHOWNOACTIVATE);
    std::thread worker(Sample);
    if(selfTest) SetTimer(windowHandle,99,benchmark?static_cast<UINT>((benchmark+3)*1000):2500,nullptr);
    FILETIME created{},exited{},kernelStart{},userStart{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelStart,&userStart);double benchmarkStart=MonotonicSeconds();if(benchmark)SetTimer(windowHandle,98,3000,nullptr);
    MSG message{};int result=0;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(benchmark && message.message==WM_TIMER && message.wParam==98){KillTimer(windowHandle,98);GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelStart,&userStart);benchmarkStart=MonotonicSeconds();std::lock_guard<std::mutex> lock(dataMutex);sampleDurations.clear();paintLatencies.clear();sampleIoctls=0;bandwidthSamples=0;bandwidthReadSum=bandwidthWriteSum=bandwidthReadMax=bandwidthWriteMax=0;continue;}
        if(selfTest && message.message==WM_TIMER && message.wParam==99) {
            KillTimer(windowHandle,99);
            if(benchmark) {DestroyWindow(windowHandle);continue;}
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
            SetWindowTextW(edits[4],L"36");SetWindowTextW(edits[1],L"220");
            if(contentWidth-labelWidth-frequencyWidth-ipcWidth-busyWidth-2*options[6].value<32)result=2;
            std::size_t displayed=0;for(const auto& group:displayGroups){if(group.kind==CoreKind::Efficiency && group.physical.size()>6)result=11;for(auto core:group.physical)displayed+=topology.cores[core].siblings.size();}if(displayed!=processorCount)result=12;
            SetWindowTextW(edits[0],L"1");SendMessageW(settingsHandle,WM_COMMAND,MAKEWPARAM(100,EN_KILLFOCUS),reinterpret_cast<LPARAM>(edits[0]));
            if(options[0].value!=92) result=3;
            SendMessageW(windowHandle,WM_LBUTTONDBLCLK,0,0);if(IsWindowVisible(windowHandle))result=4;
            ShowWindow(windowHandle,SW_SHOWNOACTIVATE);
    ShowWindow(windowHandle,SW_SHOWNOACTIVATE);UpdateWindow(windowHandle);
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
        for(std::size_t i=0;i<topology.logical.size();++i){const auto& cpu=topology.logical[i];std::printf("LP%zu group=%u number=%u physical=%u package=%u kind=%u APIC=%u hwcore=%u PMUv=%u fixed=%u width=%u\n",i,cpu.group,cpu.number,cpu.physical,cpu.package,static_cast<unsigned>(cpu.kind),cpu.apicId,cpu.hardwareCore,cpu.pmuVersion,cpu.fixedCount,cpu.fixedWidth);}
        std::fflush(stdout);
    }
    if(trayIcon)DestroyIcon(trayIcon);if(settingsFont)DeleteObject(settingsFont);
    DropTarget();Release(normalFont);Release(titleFont);Release(textFactory);Release(factory);CoUninitialize();return result;
}
