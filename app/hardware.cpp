#include "hardware.h"
#include <intrin.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace {
// Intel SDM Vol.4; platform selection is centralized here.
struct IntelRegisters {
    static constexpr unsigned Frequency=0x198, CoreThermal=0x19C, TjMax=0x1A2;
    static constexpr unsigned PackageThermal=0x1B1, RaplUnit=0x606, PackageEnergy=0x611, FixedCtr0=0x309;
    // Intel PCM TGLClientBW, also selected for Alder Lake. Four 64-bit readouts,
    // 32-bit free-running counter arithmetic, 64 bytes per transaction.
    static constexpr std::array<unsigned,8> Imc={0x5058,0x50A0,0xD858,0xD8A0,0x15058,0x150A0,0x1D858,0x1D8A0};
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
CpuTopology DetectTopology() {
    CpuTopology result;int r[4]{};__cpuid(r,0);const int maxLeaf=r[0];
    result.intel=r[1]==0x756e6547 && r[3]==0x49656e69 && r[2]==0x6c65746e;
    __cpuid(r,1); const unsigned signature=static_cast<unsigned>(r[0]);result.family=(signature>>8)&15;if(result.family==15)result.family+=(signature>>20)&255;
    result.model=(signature>>4)&15;if(result.family==6 || result.family==15)result.model|=(signature>>12)&0xf0;
    __cpuid(r,static_cast<int>(0x80000000));if(static_cast<unsigned>(r[0])>=0x80000004u) {char name[49]{};for(unsigned i=0;i<3;++i){__cpuid(r,static_cast<int>(0x80000002u+i));memcpy(name+i*16,r,16);}result.name.assign(name,name+strlen(name));while(!result.name.empty() && result.name.front()==L' ')result.name.erase(0,1);}
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
        const int leaf=maxLeaf>=0x1F?0x1F:0xB; if(maxLeaf>=leaf){__cpuidex(r,leaf,0);cpu.apicId=static_cast<unsigned>(r[3]);cpu.hardwareCore=cpu.apicId>>(r[0]&31);}
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
void HardwareSampler::Initialize(const std::wstring& directory, bool diagnostics) {
    // Other platforms retain topology/PDH; opt in only after register validation.
    supported_=topology_.intel && topology_.family==6 && (topology_.model==0x97 || topology_.model==0x9A);
    if(!supported_)return;
    DWORD error=0;msrReady_=msr_.OpenAndLoad((directory+L"IntelMSR.bin").c_str(),error);if(!msrReady_)return;
    int r[4]{};__cpuid(r,0);if(r[0]>=0x16){__cpuidex(r,0x16,0);if(r[2]>0 && r[2]<=1000)busMHz_=static_cast<unsigned>(r[2]);}
    tjMax_.resize(topology_.cores.size());coreAvailable_.assign(topology_.cores.size(),true);
    for(std::size_t i=0;i<topology_.cores.size();++i){Affinity a(topology_.logical[topology_.cores[i].siblings.front()]);std::uint64_t value=0;if(a.Valid() && msr_.ReadMsr(IntelRegisters::TjMax,value,error)){const int tj=static_cast<int>((value>>16)&255);if(tj>0 && tj<=125)tjMax_[i]=tj;}}
    for(unsigned package=0;package<topology_.packages;++package){Package p;for(std::size_t i=0;i<topology_.logical.size();++i)if(topology_.logical[i].package==package){p.representative=i;p.tjMax=tjMax_[topology_.logical[i].physical];break;}Affinity a(topology_.logical[p.representative]);std::uint64_t unit=0;if(a.Valid() && msr_.ReadMsr(IntelRegisters::RaplUnit,unit,error))p.energyUnit=std::ldexp(1.0,-static_cast<int>((unit>>8)&31));packages_.push_back(p);}
    // Capability probe only; never reprogram counters owned by Windows/profilers.
    // 0.2.11 rejects IA32_FIXED_CTR0; IPC and LLC/MPKI must stay unavailable.
    std::uint64_t probe=0;msr_.ReadMsr(IntelRegisters::FixedCtr0,probe,pmuError_);
    imcReady_=topology_.packages==1 && imc_.OpenAndLoad((directory+L"IntelMCHBAR.bin").c_str(),imcError_);
    bool anyBank=false;
    for(std::size_t bank=0;imcReady_ && bank<4;++bank){std::uint64_t read=0,write=0;
        if(!imc_.ReadMmioQword(IntelRegisters::Imc[bank*2],read,imcError_) || !imc_.ReadMmioQword(IntelRegisters::Imc[bank*2+1],write,imcError_)){imcReady_=false;break;}
        if(diagnostics)std::printf("IMC bank%zu read=%llX write=%llX\n",bank,read,write);
        // Unimplemented MMIO banks return all ones. Do not count or repeatedly poll them.
        if(read==~0ull && write==~0ull)continue;
        if(read==~0ull || write==~0ull){imcReady_=false;imcError_=ERROR_INVALID_DATA;break;}
        imcActive_[bank*2]=imcActive_[bank*2+1]=true;anyBank=true;
    }
    imcReady_=imcReady_ && anyBank;if(!imcReady_ && imcError_==0)imcError_=ERROR_NOT_SUPPORTED;
}
void HardwareSampler::Poll(HardwareReading& reading) {
    reading.frequency.assign(topology_.logical.size(),-1);reading.cores.assign(topology_.cores.size(),{});
    reading.packageThrottled=false;
    reading.packageTemperature=reading.packageWatts=reading.readGBs=reading.writeGBs=-1;
    if(!msrReady_)return;DWORD error=0;const auto before=msr_.ReadCount()+imc_.ReadCount();
    for(std::size_t i=0;i<topology_.logical.size();++i){const auto& cpu=topology_.logical[i];Affinity a(cpu);if(!a.Valid())continue;std::uint64_t value=0;
        if(msr_.ReadMsr(IntelRegisters::Frequency,value,error) && ((value>>8)&255))reading.frequency[i]=static_cast<double>((value>>8)&255)*busMHz_/1000;
        if(topology_.cores[cpu.physical].siblings.front()==i && coreAvailable_[cpu.physical]) {auto& core=reading.cores[cpu.physical];core.tjMax=tjMax_[cpu.physical];if(msr_.ReadMsr(IntelRegisters::CoreThermal,value,error)){core.temperature=DecodeTemperature(value,core.tjMax);core.throttled=(value&1)!=0;}else coreAvailable_[cpu.physical]=false;}
    }
    double watts=0;bool allPower=true;
    for(auto& package:packages_){Affinity a(topology_.logical[package.representative]);std::uint64_t value=0;if(!a.Valid()){allPower=false;continue;}
        if(package.thermal){if(msr_.ReadMsr(IntelRegisters::PackageThermal,value,error)){reading.packageTemperature=(std::max)(reading.packageTemperature,DecodeTemperature(value,package.tjMax));reading.packageThrottled=reading.packageThrottled || (value&1)!=0;}else package.thermal=false;}
        const double now=MonotonicSeconds(),dt=now-package.lastTime;
        if(package.power && package.energyUnit>0 && msr_.ReadMsr(IntelRegisters::PackageEnergy,value,error)){std::uint64_t delta=0;if(package.energy.Update(value,32,dt,delta)){const double power=static_cast<double>(delta)*package.energyUnit/dt;if(power>=0 && power<2000)watts+=power;else allPower=false;}else allPower=false;}else{package.power=false;allPower=false;}
        package.lastTime=now;
    }
    if(allPower)reading.packageWatts=watts;
    if(imcReady_){const double now=MonotonicSeconds(),dt=now-lastImcTime_;double reads=0,writes=0;bool valid=true;
        for(std::size_t i=0;i<IntelRegisters::Imc.size();++i){if(!imcActive_[i])continue;std::uint64_t value=0,delta=0;if(!imc_.ReadMmioQword(IntelRegisters::Imc[i],value,imcError_) || value==~0ull){valid=false;imcReady_=false;if(imcError_==0)imcError_=ERROR_INVALID_DATA;break;}
            if(!imcCounters_[i].Update(value,32,dt,delta)){valid=false;continue;}if(i%2)writes+=static_cast<double>(delta)*64;else reads+=static_cast<double>(delta)*64;}
        if(valid && dt<=2 && reads/dt<256e9 && writes/dt<256e9){reading.readGBs=reads/dt/1e9;reading.writeGBs=writes/dt/1e9;}lastImcTime_=now;
    }
    reading.ioctlCount=msr_.ReadCount()+imc_.ReadCount()-before;
}
