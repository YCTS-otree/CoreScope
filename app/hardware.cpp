// SPDX-License-Identifier: GPL-3.0-only
#include "hardware.h"
#include <intrin.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <pdhmsg.h>

namespace {
// Intel SDM Vol.4; platform selection is centralized here.
struct IntelRegisters {
    static constexpr unsigned Frequency=0x198, CoreThermal=0x19C, TjMax=0x1A2;
    static constexpr unsigned PackageThermal=0x1B1, RaplUnit=0x606, PackageEnergy=0x611, FixedCtr0=0x309;
    // Intel PCM TGLClientBW, also selected for Alder Lake. Four 64-bit readouts,
    // 32-bit free-running counter arithmetic, 64 bytes per transaction.
    static constexpr std::array<unsigned,8> Imc={0x5058,0x50A0,0xD858,0xD8A0,0x15058,0x150A0,0x1D858,0x1D8A0};
};
struct AmdRegisters {
    static constexpr unsigned Frequency=0xC0010293, PowerUnit=0xC0010299, PackageEnergy=0xC001029B;
    static constexpr unsigned Temperature=0x59800;
};
class Affinity {
    GROUP_AFFINITY previous_{}; bool valid_=false;
public:
    explicit Affinity(const LogicalCpu& cpu) { GROUP_AFFINITY a{};a.Group=cpu.group;a.Mask=static_cast<KAFFINITY>(1)<<cpu.number;valid_=SetThreadGroupAffinity(GetCurrentThread(),&a,&previous_)!=FALSE; }
    ~Affinity() { if(valid_) SetThreadGroupAffinity(GetCurrentThread(),&previous_,nullptr); }
    bool Valid() const { return valid_; }
};
bool InMask(const LogicalCpu& cpu,const PROCESSOR_RELATIONSHIP& relation) {
    for(WORD i=0;i<relation.GroupCount;++i) if(relation.GroupMask[i].Group==cpu.group && (relation.GroupMask[i].Mask&(static_cast<KAFFINITY>(1)<<cpu.number)))return true;
    return false;
}
double DecodeTemperature(std::uint64_t value,int tjMax) {
    const int delta=static_cast<int>((value>>16)&127);
    return (value&(1ull<<31)) && tjMax>0 && delta<=tjMax ? static_cast<double>(tjMax-delta) : -1;
}
}
double MonotonicSeconds() { LARGE_INTEGER counter{};QueryPerformanceCounter(&counter); static const double hz=[] {LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return static_cast<double>(f.QuadPart);}(); return static_cast<double>(counter.QuadPart)/hz; }
const wchar_t* CoreKindName(CoreKind kind) { return kind==CoreKind::Performance ? L"P Core" : kind==CoreKind::Efficiency ? L"E Core" : L"Core"; }
void DecodeCpuSignature(unsigned signature, unsigned& family, unsigned& model) {
    const unsigned baseFamily=(signature>>8)&15;
    family=baseFamily;if(baseFamily==15)family+=(signature>>20)&255;
    model=(signature>>4)&15;if(baseFamily==6 || baseFamily==15)model|=(signature>>12)&0xf0;
}
double DecodeAmdFrequency(unsigned family, std::uint64_t value) {
    if(static_cast<std::uint32_t>(value)==0xffffffffu)return -1;
    double mhz=0;
    if(family==0x1A)mhz=static_cast<double>(value&0xfff)*5;
    else if(family==0x17 || family==0x19){const unsigned divisor=static_cast<unsigned>((value>>8)&63);if(!divisor)return -1;mhz=200.0*static_cast<double>(value&255)/divisor;}
    return mhz>0 && mhz<=10000?mhz/1000:-1;
}
double DecodeAmdTemperature(std::uint32_t value, const std::wstring& name) {
    if(value==0 || value==0xffffffffu)return -1;
    double temperature=static_cast<double>(value>>21)*.125;
    if((value&(1u<<19)) || (value&0x30000u)==0x30000u)temperature-=49;
    // The early Zen Tctl offsets are SKU-specific (Linux k10temp).
    if(name.find(L"1600X")!=std::wstring::npos || name.find(L"1700X")!=std::wstring::npos || name.find(L"1800X")!=std::wstring::npos)temperature-=20;
    else if(name.find(L"Threadripper 19")!=std::wstring::npos || name.find(L"Threadripper 29")!=std::wstring::npos)temperature-=27;
    else if(name.find(L"2700X")!=std::wstring::npos)temperature-=10;
    return temperature>0 && temperature<=125?temperature:-1;
}
CpuTopology DetectTopology() {
    CpuTopology result;int r[4]{};__cpuid(r,0);const int maxLeaf=r[0];
    result.intel=r[1]==0x756e6547 && r[3]==0x49656e69 && r[2]==0x6c65746e;
    result.amd=r[1]==0x68747541 && r[3]==0x69746e65 && r[2]==0x444d4163;
    __cpuid(r,1);DecodeCpuSignature(static_cast<unsigned>(r[0]),result.family,result.model);
    __cpuid(r,static_cast<int>(0x80000000));const unsigned maxExtended=static_cast<unsigned>(r[0]);if(maxExtended>=0x80000004u) {char name[49]{};for(unsigned i=0;i<3;++i){__cpuid(r,static_cast<int>(0x80000002u+i));memcpy(name+i*16,r,16);}result.name.assign(name,name+strlen(name));while(!result.name.empty() && result.name.front()==L' ')result.name.erase(0,1);}
    for(WORD g=0;g<GetActiveProcessorGroupCount();++g) for(DWORD n=0;n<GetActiveProcessorCount(g);++n) {LogicalCpu cpu;cpu.group=g;cpu.number=static_cast<BYTE>(n);result.logical.push_back(cpu);}
    DWORD size=0;GetLogicalProcessorInformationEx(RelationAll,nullptr,&size);std::vector<BYTE> buffer(size);
    if(size && GetLogicalProcessorInformationEx(RelationAll,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),&size)) {
        for(DWORD offset=0;offset<size;) { const auto* relation=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()+offset);if(relation->Size==0 || relation->Size>size-offset)break;
            if(relation->Relationship==RelationProcessorPackage) {for(auto& cpu:result.logical)if(InMask(cpu,relation->Processor))cpu.package=result.packages;++result.packages;}offset+=relation->Size;
        }
        for(DWORD offset=0;offset<size;) {const auto* relation=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()+offset);if(relation->Size==0 || relation->Size>size-offset)break;
            if(relation->Relationship==RelationProcessorCore) {PhysicalCpu core;for(std::size_t i=0;i<result.logical.size();++i)if(InMask(result.logical[i],relation->Processor)){result.logical[i].physical=static_cast<unsigned>(result.cores.size());core.siblings.push_back(i);core.package=result.logical[i].package;}if(!core.siblings.empty())result.cores.push_back(core);}offset+=relation->Size;
        }
    }
    if(result.cores.empty()) {result.logical.clear();return result;}
    result.packages=(std::max)(1u,result.packages);
    for(auto& cpu:result.logical) {Affinity affinity(cpu);if(!affinity.Valid())continue;
        if(result.intel && maxLeaf>=0x1A){__cpuidex(r,0x1A,0);const auto type=static_cast<unsigned>(r[0])>>24;cpu.kind=type==0x40?CoreKind::Performance:type==0x20?CoreKind::Efficiency:CoreKind::Unknown;}
        if(result.intel && maxLeaf>=0xA){__cpuidex(r,0xA,0);cpu.pmuVersion=static_cast<unsigned>(r[0])&255;cpu.fixedCount=static_cast<unsigned>(r[3])&31;cpu.fixedWidth=(static_cast<unsigned>(r[3])>>5)&255;}
        if(result.amd && maxExtended>=0x8000001Eu){__cpuid(r,static_cast<int>(0x8000001E));cpu.apicId=static_cast<unsigned>(r[0]);cpu.hardwareCore=static_cast<unsigned>(r[1])&255;}
        else {const int leaf=maxLeaf>=0x1F?0x1F:0xB; if(maxLeaf>=leaf){__cpuidex(r,leaf,0);cpu.apicId=static_cast<unsigned>(r[3]);cpu.hardwareCore=cpu.apicId>>(r[0]&31);}}
        result.cores[cpu.physical].kind=cpu.kind;
    }
    return result;
}
bool CounterDelta::Update(std::uint64_t current,unsigned width,double seconds,std::uint64_t& delta) {
    if(width==0 || width>64){Reset();delta=0;return false;}
    const std::uint64_t mask=width==64?~0ull:((1ull<<width)-1);current&=mask;
    const bool ready=valid && seconds>0 && seconds<=5;
    delta=(current-previous)&mask;previous=current;valid=true;return ready;
}
int AlertFilter::Update(double value,double seconds,double orange,double red) {
    if(value<0 || !std::isfinite(value)){ema=-1;heldSeconds=0;return level=0;}
    if(seconds<=0 || seconds>5){ema=value;heldSeconds=0;return level=0;}
    ema=ema<0?value:ema+(value-ema)*(1-std::exp(-seconds/1.5));
    const int candidate=ema>=red?2:ema>=orange?1:0;
    if(candidate>level){heldSeconds+=seconds;if(heldSeconds>=1.5){level=candidate;heldSeconds=0;}}
    else if(candidate<level && ema<(level==2?red:orange)-3){heldSeconds+=seconds;if(heldSeconds>=2){level=candidate;heldSeconds=0;}}
    else heldSeconds=0;
    return level;
}
HardwareSampler::HardwareSampler(const CpuTopology& topology):topology_(topology) {}
HardwareSampler::~HardwareSampler() { if(frequencyQuery_)PdhCloseQuery(frequencyQuery_); }
void HardwareSampler::Initialize(const std::wstring& directory, bool diagnostics, bool frequencyOnly) {
    directory_=directory;diagnostics_=diagnostics;frequencyOnly_=frequencyOnly;
    if(topology_.amd){InitializeAmd();return;}
    // Other platforms retain topology/PDH; opt in only after register validation.
    supported_=topology_.intel && (frequencyOnly_ || (topology_.family==6 && (topology_.model==0x97 || topology_.model==0x9A)));
    if(!supported_)return;
    msrReady_=msr_.OpenAndLoad((directory+L"IntelMSR.bin").c_str(),sensorError_);if(!msrReady_)return;DWORD error=0;
    int r[4]{};__cpuid(r,0);if(r[0]>=0x16){__cpuidex(r,0x16,0);if(r[2]>0 && r[2]<=1000)busMHz_=static_cast<unsigned>(r[2]);}
    // Preserve the legacy --once Intel frequency scope without enabling unverified sensors.
    if(frequencyOnly_)return;
    tjMax_.resize(topology_.cores.size());coreAvailable_.assign(topology_.cores.size(),true);
    for(std::size_t i=0;i<topology_.cores.size();++i){Affinity a(topology_.logical[topology_.cores[i].siblings.front()]);std::uint64_t value=0;if(a.Valid() && msr_.ReadMsr(IntelRegisters::TjMax,value,error)){const int tj=static_cast<int>((value>>16)&255);if(tj>0 && tj<=125)tjMax_[i]=tj;}}
    for(unsigned package=0;package<topology_.packages;++package){Package p;for(std::size_t i=0;i<topology_.logical.size();++i)if(topology_.logical[i].package==package){p.representative=i;p.tjMax=tjMax_[topology_.logical[i].physical];break;}Affinity a(topology_.logical[p.representative]);std::uint64_t unit=0;if(a.Valid() && msr_.ReadMsr(IntelRegisters::RaplUnit,unit,error))p.energyUnit=std::ldexp(1.0,-static_cast<int>((unit>>8)&31));packages_.push_back(p);}
    // Capability probe only; never reprogram counters owned by Windows/profilers.
    // PawnIO rejects IA32_FIXED_CTR0; live IPC/MPKI use the independent ETW backend.
    std::uint64_t probe=0;msr_.ReadMsr(IntelRegisters::FixedCtr0,probe,pmuError_);
    directory_=directory;diagnostics_=diagnostics;
}
void HardwareSampler::InitializeAmd() {
    // OS-reported MHz is a labelled fallback, never an assumed reference clock.
    if(PdhOpenQueryW(nullptr,0,&frequencyQuery_)==ERROR_SUCCESS){
        frequencyCounters_.resize(topology_.logical.size());
        for(std::size_t i=0;i<topology_.logical.size();++i){const auto& cpu=topology_.logical[i];
            const auto path=L"\\Processor Information("+std::to_wstring(cpu.group)+L","+std::to_wstring(cpu.number)+L")\\Processor Frequency";
            PdhAddEnglishCounterW(frequencyQuery_,path.c_str(),0,&frequencyCounters_[i]);}
        PdhCollectQueryData(frequencyQuery_);
    }
    supported_=topology_.family==0x17 || topology_.family==0x19 || topology_.family==0x1A;
    if(!supported_ || topology_.logical.empty())return;
    msrReady_=msr_.OpenAndLoad((directory_+L"AMDFamily17.bin").c_str(),sensorError_);if(!msrReady_)return;
    frequencyAvailable_.assign(topology_.logical.size(),true);
    if(frequencyOnly_)return;
    // SMN module addresses only bus 0 / device 0. Do not duplicate it on multi-socket systems.
    amdThermal_=topology_.packages==1;
    // Only the validated HX 370 SKU has a known thermal limit here; do not guess others.
    if(topology_.family==0x1A && topology_.model==0x24 && topology_.name.find(L"HX 370")!=std::wstring::npos)packageTjMax_=100;
    for(unsigned package=0;package<topology_.packages;++package){Package p;p.thermal=false;
        for(std::size_t i=0;i<topology_.logical.size();++i)if(topology_.logical[i].package==package){p.representative=i;break;}
        Affinity a(topology_.logical[p.representative]);std::uint64_t unit=0;DWORD error=0;
        if(a.Valid() && msr_.ReadMsr(AmdRegisters::PowerUnit,unit,error) && unit!=~0ull && unit!=0)p.energyUnit=std::ldexp(1.0,-static_cast<int>((unit>>8)&31));
        packages_.push_back(p);
    }
}
void HardwareSampler::PollAmd(HardwareReading& reading) {
    const auto before=msr_.ReadCount();DWORD error=0;
    if(msrReady_){
        for(std::size_t i=0;i<topology_.logical.size();++i){if(!frequencyAvailable_[i])continue;Affinity a(topology_.logical[i]);std::uint64_t value=0;
            if(a.Valid() && msr_.ReadMsr(AmdRegisters::Frequency,value,error))reading.frequency[i]=DecodeAmdFrequency(topology_.family,value);
            else frequencyAvailable_[i]=false;
        }
        if(amdThermal_){std::uint32_t value=0;
            if(msr_.ReadSmn(AmdRegisters::Temperature,value,error))reading.packageTemperature=DecodeAmdTemperature(value,topology_.name);
            else if(error!=ERROR_TIMEOUT)amdThermal_=false;
        }
        double watts=0;bool allPower=!packages_.empty();
        for(auto& package:packages_){Affinity a(topology_.logical[package.representative]);std::uint64_t value=0;
            const double now=MonotonicSeconds(),dt=now-package.lastTime;
            if(a.Valid() && package.power && package.energyUnit>0 && msr_.ReadMsr(AmdRegisters::PackageEnergy,value,error)){
                std::uint64_t delta=0;if(package.energy.Update(value,32,dt,delta)){const double power=static_cast<double>(delta)*package.energyUnit/dt;if(power>=0 && power<2000)watts+=power;else allPower=false;}else allPower=false;
            }else{package.power=false;allPower=false;package.energy.Reset();}
            package.lastTime=now;
        }
        if(allPower)reading.packageWatts=watts;
    }
    if(frequencyQuery_ && std::any_of(reading.frequency.begin(),reading.frequency.end(),[](double value){return value<0;}) && PdhCollectQueryData(frequencyQuery_)==ERROR_SUCCESS){
        for(std::size_t i=0;i<frequencyCounters_.size();++i)if(reading.frequency[i]<0 && frequencyCounters_[i]){PDH_FMT_COUNTERVALUE value{};
            if(PdhGetFormattedCounterValue(frequencyCounters_[i],PDH_FMT_DOUBLE,nullptr,&value)==ERROR_SUCCESS && (value.CStatus==PDH_CSTATUS_VALID_DATA || value.CStatus==PDH_CSTATUS_NEW_DATA) && std::isfinite(value.doubleValue) && value.doubleValue>0 && value.doubleValue<=10000){reading.frequency[i]=value.doubleValue/1000;reading.osFrequency=true;}
        }
    }
    reading.packageTjMax=packageTjMax_;reading.ioctlCount=msr_.ReadCount()-before;
}
void HardwareSampler::InitializeImc() {
    imcAttempted_=true;
    imcReady_=topology_.packages==1 && imc_.OpenAndLoad((directory_+L"IntelMCHBAR.bin").c_str(),imcError_);
    bool anyBank=false;
    for(std::size_t bank=0;imcReady_ && bank<4;++bank){std::uint64_t read=0,write=0;
        if(!imc_.ReadMmioQword(IntelRegisters::Imc[bank*2],read,imcError_) || !imc_.ReadMmioQword(IntelRegisters::Imc[bank*2+1],write,imcError_)){imcReady_=false;break;}
        if(diagnostics_)std::printf("IMC bank%zu read=%llX write=%llX\n",bank,read,write);
        // Unimplemented MMIO banks return all ones. Do not count or repeatedly poll them.
        if(read==~0ull && write==~0ull)continue;
        if(read==~0ull || write==~0ull){imcReady_=false;imcError_=ERROR_INVALID_DATA;break;}
        imcActive_[bank*2]=imcActive_[bank*2+1]=true;anyBank=true;
    }
    imcReady_=imcReady_ && anyBank;if(!imcReady_ && imcError_==0)imcError_=ERROR_NOT_SUPPORTED;
}

void HardwareSampler::Poll(HardwareReading& reading, bool memoryEnabled) {
    reading.frequency.assign(topology_.logical.size(),-1);reading.cores.assign(topology_.cores.size(),{});
    reading.packageThrottled=false;
    reading.ioctlCount=0;reading.osFrequency=false;reading.packageTjMax=0;reading.sensorError=sensorError_;
    reading.packageTemperature=reading.packageWatts=reading.readGBs=reading.writeGBs=-1;
    if(topology_.amd){PollAmd(reading);return;}
    if(!msrReady_)return;DWORD error=0;const auto before=msr_.ReadCount()+imc_.ReadCount();
    for(std::size_t i=0;i<topology_.logical.size();++i){const auto& cpu=topology_.logical[i];Affinity a(cpu);if(!a.Valid())continue;std::uint64_t value=0;
        if(msr_.ReadMsr(IntelRegisters::Frequency,value,error) && ((value>>8)&255))reading.frequency[i]=static_cast<double>((value>>8)&255)*busMHz_/1000;
        if(!frequencyOnly_ && topology_.cores[cpu.physical].siblings.front()==i && coreAvailable_[cpu.physical]) {auto& core=reading.cores[cpu.physical];core.tjMax=tjMax_[cpu.physical];if(msr_.ReadMsr(IntelRegisters::CoreThermal,value,error)){core.temperature=DecodeTemperature(value,core.tjMax);core.throttled=(value&1)!=0;}else coreAvailable_[cpu.physical]=false;}
    }
    if(frequencyOnly_){reading.ioctlCount=msr_.ReadCount()-before;return;}
    double watts=0;bool allPower=true;
    for(auto& package:packages_){Affinity a(topology_.logical[package.representative]);std::uint64_t value=0;if(!a.Valid()){allPower=false;continue;}
        if(package.thermal){if(msr_.ReadMsr(IntelRegisters::PackageThermal,value,error)){reading.packageTemperature=(std::max)(reading.packageTemperature,DecodeTemperature(value,package.tjMax));reading.packageThrottled=reading.packageThrottled || (value&1)!=0;}else package.thermal=false;}
        const double now=MonotonicSeconds(),dt=now-package.lastTime;
        if(package.power && package.energyUnit>0 && msr_.ReadMsr(IntelRegisters::PackageEnergy,value,error)){std::uint64_t delta=0;if(package.energy.Update(value,32,dt,delta)){const double power=static_cast<double>(delta)*package.energyUnit/dt;if(power>=0 && power<2000)watts+=power;else allPower=false;}else allPower=false;}else{package.power=false;allPower=false;}
        package.lastTime=now;
    }
    if(allPower)reading.packageWatts=watts;
    if(memoryEnabled && !imcAttempted_)InitializeImc();
    if(!memoryEnabled){lastImcTime_=0;for(auto& counter:imcCounters_)counter.Reset();}
    if(memoryEnabled && imcReady_){const double now=MonotonicSeconds(),dt=now-lastImcTime_;double reads=0,writes=0;bool valid=true;
        for(std::size_t i=0;i<IntelRegisters::Imc.size();++i){if(!imcActive_[i])continue;std::uint64_t value=0,delta=0;if(!imc_.ReadMmioQword(IntelRegisters::Imc[i],value,imcError_) || value==~0ull){valid=false;imcReady_=false;if(imcError_==0)imcError_=ERROR_INVALID_DATA;break;}
            if(!imcCounters_[i].Update(value,32,dt,delta)){valid=false;continue;}if(i%2)writes+=static_cast<double>(delta)*64;else reads+=static_cast<double>(delta)*64;}
        if(valid && dt<=2 && reads/dt<256e9 && writes/dt<256e9){reading.readGBs=reads/dt/1e9;reading.writeGBs=writes/dt/1e9;}lastImcTime_=now;
    }
    reading.ioctlCount=msr_.ReadCount()+imc_.ReadCount()-before;
}

std::wstring ShortCpuName(std::wstring name) {
    for(const auto* token:{L"(R)",L"(TM)"}){std::size_t at;while((at=name.find(token))!=std::wstring::npos)name.erase(at,wcslen(token));}
    const auto intel=name.find(L"Intel");
    if(intel!=std::wstring::npos){
        for(const auto* family:{L"i3-",L"i5-",L"i7-",L"i9-"}){const auto start=name.find(family);if(start!=std::wstring::npos){const auto end=name.find(L' ',start);return L"Intel "+name.substr(start,end==std::wstring::npos?end:end-start);}}
        name.erase(0,intel);
    }
    const auto at=name.find(L" @");if(at!=std::wstring::npos)name.erase(at);
    for(const auto* suffix:{L" CPU",L" Processor",L" with Radeon",L" w/ Radeon"}){const auto pos=name.find(suffix);if(pos!=std::wstring::npos)name.erase(pos);}
    const auto cores=name.find(L"-Core");if(cores!=std::wstring::npos){auto pos=name.rfind(L' ',cores);if(pos!=std::wstring::npos)name.erase(pos);}
    while(!name.empty() && name.back()==L' ')name.pop_back();
    return name.empty()?L"CPU":name;
}
