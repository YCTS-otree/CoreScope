// SPDX-License-Identifier: GPL-3.0-only
#include "hardware.h"
#include "monitor.h"
#include "../shared/version.h"
#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace {
int SampleOnce() {
    const auto topology=DetectTopology();
    if(topology.logical.empty()){std::puts("Cannot enumerate CPU topology");return 1;}
    wchar_t executable[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,executable,32768);
    if(!length || length>=32768)return 1;
    std::wstring directory(executable);directory=directory.substr(0,directory.find_last_of(L"\\")+1);
    HardwareSampler sampler(topology);sampler.Initialize(directory,false,true);
    HardwareReading reading;sampler.Poll(reading,false);Sleep(250);sampler.Poll(reading,false);
    std::size_t valid=0;
    for(std::size_t i=0;i<reading.frequency.size();++i){
        if(reading.frequency[i]>=0){std::printf("CPU%02zu=%.0f MHz ",i,reading.frequency[i]*1000);++valid;}
        else std::printf("CPU%02zu=N/A ",i);
    }
    std::printf("\nFrequency source: %s; sensor error=%lu\n",reading.osFrequency?"Windows reported":valid?"hardware P-state":"unavailable",reading.sensorError);
    return valid?0:1;
}
}

int wmain(int argc,wchar_t* argv[]) {
    if(argc==2 && wcscmp(argv[1],L"--version")==0){std::puts("CoreScope " CORESCOPE_VERSION_STRING);return 0;}
    if(argc==2 && wcscmp(argv[1],L"--once")==0)return SampleOnce();
    if(argc==2 && wcscmp(argv[1],L"--tma-test")==0)return RunMonitor(false,true,20,true,true);
    if(argc==2 && wcscmp(argv[1],L"--advanced-inspect")==0)return RunMonitor(false,true,900,true,true,2);
    if(argc==2 && wcscmp(argv[1],L"--compact-inspect")==0)return RunMonitor(true,true,900,true,false,1);
    if(argc==2 && wcscmp(argv[1],L"--compact-live-inspect")==0)return RunMonitor(false,true,60,false,false,3);
    if(argc==2 && wcscmp(argv[1],L"--pmu-test")==0)return RunMonitor(false,true,20,true);
    if(argc==2 && wcscmp(argv[1],L"--pmu-inspect")==0)return RunMonitor(false,true,300,true);
    if(argc==2 && wcscmp(argv[1],L"--inspect")==0)return RunMonitor(false,true,60);
    if(argc==2 && wcscmp(argv[1],L"--benchmark")==0)return RunMonitor(false,true,20);
    if(argc==2 && wcscmp(argv[1],L"--sensor-test")==0)return RunMonitor(false,true);
    if(argc==1 || (argc==2 && wcscmp(argv[1],L"--demo")==0) || (argc==2 && wcscmp(argv[1],L"--ui-test")==0))return RunMonitor(argc==2,argc==2 && wcscmp(argv[1],L"--ui-test")==0);
    std::printf("CoreScope " CORESCOPE_VERSION_STRING "\nUsage: CoreScope.exe [--version | --help | --once | --demo | --ui-test | --sensor-test | --benchmark | --inspect | --pmu-test | --pmu-inspect | --tma-test | --advanced-inspect | --compact-inspect | --compact-live-inspect]\n");
    return argc==2 && wcscmp(argv[1],L"--help")==0?0:2;
}
