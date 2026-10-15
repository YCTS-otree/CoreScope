// SPDX-License-Identifier: GPL-3.0-only
#include "../app/hardware.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
int main(int argc, char** argv) {
    unsigned family=0,model=0;
    DecodeCpuSignature(0x00B20F40,family,model);assert(family==0x1A && model==0x24);
    DecodeCpuSignature(0x00870F10,family,model);assert(family==0x17 && model==0x71);
    DecodeCpuSignature(0x00A60F12,family,model);assert(family==0x19 && model==0x61);
    DecodeCpuSignature(0x00090672,family,model);assert(family==6 && model==0x97);
    assert(std::abs(DecodeAmdFrequency(0x1A,1020)-5.1)<1e-9);
    assert(std::abs(DecodeAmdFrequency(0x17,(8ull<<8)|144)-3.6)<1e-9);
    assert(std::abs(DecodeAmdFrequency(0x19,(10ull<<8)|225)-4.5)<1e-9);
    assert(DecodeAmdFrequency(0x17,100)<0 && DecodeAmdFrequency(0x1A,0)<0);
    assert(DecodeAmdFrequency(0x1A,4095)<0 && DecodeAmdFrequency(0x16,100)<0);
    assert(DecodeAmdFrequency(0x17,~0ull)<0 && DecodeAmdFrequency(0x19,0xffffffffu)<0);
    assert(DecodeAmdTemperature(600u<<21,L"AMD Ryzen AI 9 HX 370")==75);
    assert(DecodeAmdTemperature((992u<<21)|(1u<<19),L"Ryzen")==75);
    assert(DecodeAmdTemperature((992u<<21)|0x30000u,L"Ryzen")==75);
    assert(DecodeAmdTemperature(600u<<21,L"AMD Ryzen 7 1800X")==55);
    assert(DecodeAmdTemperature(600u<<21,L"AMD Ryzen 7 2700X")==65);
    assert(DecodeAmdTemperature(600u<<21,L"AMD Ryzen Threadripper 1950X")==48);
    assert(DecodeAmdTemperature(0,L"")<0 && DecodeAmdTemperature(0xffffffffu,L"")<0);
    assert(ShortCpuName(L"12th Gen Intel(R) Core(TM) i7-12700KF") == L"Intel i7-12700KF");
    assert(ShortCpuName(L"Intel(R) Core(TM) i9-14900KS @ 3.20GHz") == L"Intel i9-14900KS");
    assert(ShortCpuName(L"AMD Ryzen 9 7950X 16-Core Processor") == L"AMD Ryzen 9 7950X");
    assert(ShortCpuName(L"AMD Ryzen AI 9 HX 370 w/ Radeon 890M") == L"AMD Ryzen AI 9 HX 370");
    assert(ShortCpuName(L"Intel(R) Core(TM) Ultra 9 285K") == L"Intel Core Ultra 9 285K");
    CounterDelta counter;std::uint64_t delta=0;
    assert(!counter.Update(0xfffffff0,32,.25,delta));
    assert(counter.Update(0x20,32,.25,delta) && delta==48);
    assert(!counter.Update(0x30,32,30,delta));
    assert(counter.Update(0x40,32,.25,delta) && delta==16);
    counter.Reset();assert(!counter.Update(0,48,.25,delta));
    AlertFilter alert;
    for(int i=0;i<16;++i)assert(alert.Update(20,.25,90,98)==0);
    assert(alert.Update(100,.25,90,98)==0); // An isolated spike must not flash.
    for(int i=0;i<50;++i)alert.Update(100,.25,90,98);
    assert(alert.level==2);
    assert(alert.Update(0,.25,90,98)==2); // Recovery is also debounced.
    for(int i=0;i<50;++i)alert.Update(0,.25,90,98);
    assert(alert.level==0);
    assert(alert.Update(100,30,90,98)==0);
    assert(!counter.Update(1,0,.25,delta));
    const auto topology=DetectTopology();assert(!topology.logical.empty());
    std::vector<int> seen(topology.logical.size());
    for(std::size_t physical=0;physical<topology.cores.size();++physical) {
        for(auto lp:topology.cores[physical].siblings){assert(lp<seen.size());++seen[lp];assert(topology.logical[lp].physical==physical);assert(topology.logical[lp].package==topology.cores[physical].package);}
    }
    for(int count:seen)assert(count==1);
    CpuTopology unsupported;HardwareSampler absent(unsupported);absent.Initialize(L"");
    HardwareReading unavailable;unavailable.ioctlCount=99;unavailable.osFrequency=true;absent.Poll(unavailable);
    assert(unavailable.frequency.empty() && unavailable.packageTemperature<0 && unavailable.packageWatts<0 && unavailable.ioctlCount==0 && !unavailable.osFrequency);
    if(argc==2){
        const bool missing=std::string(argv[1])=="--missing-module";
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);std::wstring directory(executable);directory=directory.substr(0,directory.find_last_of(L"\\")+1);
        HardwareSampler hardware(topology);hardware.Initialize(missing?directory+L"missing-modules\\":directory,true);
        HardwareReading reading;
        for(int sample=0;sample<4;++sample){Sleep(250);hardware.Poll(reading,false);
            std::size_t valid=0;for(double ghz:reading.frequency)if(ghz>0 && ghz<=10)++valid;
            std::printf("sample=%d family=%X model=%X frequency=%zu/%zu source=%s temperature=%.3f power=%.3f sensor_error=%lu IOCTL=%llu\n",sample,topology.family,topology.model,valid,reading.frequency.size(),reading.osFrequency?"Windows":"MSR",reading.packageTemperature,reading.packageWatts,reading.sensorError,reading.ioctlCount);
            if(topology.amd && valid!=topology.logical.size())return 2;
            if(missing && (reading.packageTemperature>=0 || reading.packageWatts>=0 || (topology.amd && !reading.osFrequency)))return 3;
        }
        if(std::string(argv[1])=="--require-sensors" && (reading.packageTemperature<=0 || reading.packageWatts<0 || reading.osFrequency))return 4;
        if(topology.amd){assert(reading.readGBs<0 && reading.writeGBs<0);for(const auto& core:reading.cores)assert(core.temperature<0);}
    }
    std::printf("PASS: counter wrap, suspend rebaseline, alert spike/recovery, topology coverage and SMT mapping (%zu cores / %zu LPs)\n",topology.cores.size(),topology.logical.size());
}
