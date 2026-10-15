// SPDX-License-Identifier: GPL-3.0-only
#define NOMINMAX
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <intrin.h>
#include <array>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <chrono>
#include <cwchar>
#include <string>
#include <cstring>
#include <algorithm>
#include <random>
#include <numeric>
#pragma comment(lib,"advapi32.lib")
// Diagnostic only: cycle-domain P-core memory stall response, not accuracy certification.
struct Stats {
 unsigned long long events=0, valid=0, resets=0;
 bool seen=false; unsigned long long previousTime=0;
 std::array<unsigned long long,16> prev{},sum{},nonzero{};
};
struct Phase {unsigned cpu;char mode[16];unsigned long long start,end;Stats stats;};
Stats stats[256];std::vector<Phase> phases;
std::array<std::wstring,16> names{};unsigned counterCount=0;
unsigned long long total=0, pmc=0;unsigned malformed=0;
void WINAPI Record(EVENT_RECORD* r) {
 ++total;
 const GUID perfInfo={0xce1dbfb4,0x137e,0x4da6,{0x87,0xb0,0x3f,0x59,0xaa,0x10,0x2c,0xbc}};
 if(IsEqualGUID(r->EventHeader.ProviderId,perfInfo) && r->EventHeader.EventDescriptor.Opcode==48){
  // PmcCounterConfig_V2: uint32 count followed by NUL-terminated UTF-16 names.
  if(r->UserDataLength>=4){unsigned count=0;std::memcpy(&count,r->UserData,4);
   const auto* begin=reinterpret_cast<const wchar_t*>(static_cast<const unsigned char*>(r->UserData)+4);
   const auto* end=reinterpret_cast<const wchar_t*>(static_cast<const unsigned char*>(r->UserData)+r->UserDataLength);
   if(count<=16){counterCount=count;for(unsigned i=0;i<count;i++){const auto* p=begin;while(p<end && *p)++p;if(p==end){++malformed;break;}names[i].assign(begin,p);begin=p+1;}}
  }
 }
 for(USHORT i=0;i<r->ExtendedDataCount;i++) {
  auto& e=r->ExtendedData[i];if(e.ExtType!=EVENT_HEADER_EXT_TYPE_PMC_COUNTERS)continue;
  ++pmc;if(!counterCount)counterCount=e.DataSize/8;if(counterCount>16 || e.DataSize!=counterCount*8){++malformed;continue;}
  auto* v=reinterpret_cast<const unsigned long long*>(e.DataPtr);
  unsigned cpu=r->BufferContext.ProcessorIndex;if(cpu>=256)continue;
  auto& s=stats[cpu];++s.events;
  if(s.seen){bool monotonic=true;for(int j=0;j<static_cast<int>(counterCount);j++)if(v[j]<s.prev[j])monotonic=false;
   if(monotonic){++s.valid;for(int j=0;j<static_cast<int>(counterCount);j++){auto d=v[j]-s.prev[j];s.sum[j]+=d;s.nonzero[j]+=d!=0;
    for(auto& p:phases)if(p.cpu==cpu && s.previousTime>=p.start && static_cast<unsigned long long>(r->EventHeader.TimeStamp.QuadPart)>=p.start && static_cast<unsigned long long>(r->EventHeader.TimeStamp.QuadPart)<p.end){p.stats.sum[j]+=d;p.stats.nonzero[j]+=d!=0;}
   }}else ++s.resets;
  }
  for(int j=0;j<static_cast<int>(counterCount);j++)s.prev[j]=v[j];s.seen=true;s.previousTime=static_cast<unsigned long long>(r->EventHeader.TimeStamp.QuadPart);
 }
}
unsigned long long FileTimeNow(){FILETIME t{};GetSystemTimePreciseAsFileTime(&t);return (static_cast<unsigned long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
int Load(){
 auto previous=SetThreadAffinityMask(GetCurrentThread(),1);if(!previous)return 5;volatile unsigned long long sink=0;
 for(unsigned bytes:{16u*1024u,512u*1024u,8u*1024u*1024u,128u*1024u*1024u}){
  std::vector<unsigned long long> mem(bytes/8),order(bytes/64);std::iota(order.begin(),order.end(),0ull);std::mt19937_64 rng(42);std::shuffle(order.begin(),order.end(),rng);
  for(size_t i=0;i<order.size();++i)mem[order[i]*8]=order[(i+1)%order.size()]*8;
  unsigned long long x=0;auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);while(std::chrono::steady_clock::now()<end){for(int k=0;k<10000;++k)x=mem[x];sink=x;}
  const auto start=FileTimeNow();end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(std::chrono::steady_clock::now()<end){for(int k=0;k<10000;++k)x=mem[x];sink=x;}
  std::printf("PHASE 0 %uKiB %llu %llu\n",bytes/1024,start,FileTimeNow());std::fflush(stdout);
 }
 SetThreadAffinityMask(GetCurrentThread(),previous);return sink==~0ull?6:0;
}
int Index(const wchar_t* value){for(int i=0;i<static_cast<int>(counterCount);i++)if(names[i]==value)return i;return -1;}
void Print(const Stats& s,int,int cycles,int){
 for(unsigned i=0;i<counterCount;++i)std::printf(" %ls=%llu(%.3f%%)",names[i].c_str(),s.sum[i],s.sum[cycles]?100.*s.sum[i]/s.sum[cycles]:0.);
 const int loads=Index(L"CFMMemV5ABoundLoads"),l1=Index(L"CFMMemV5XStallL1"),l2=Index(L"CFMMemV5YStallL2"),l3=Index(L"CFMMemV5ZStallL3");
 if(loads>=0 && l1>=0 && l2>=0 && l3>=0 && s.sum[cycles]){
  const double unit=100./s.sum[cycles];std::printf(" L1Bound=%.3f L2Bound=%.3f L3Bound=%.3f DRAMBound=%.3f",std::max(0.,double(s.sum[loads])-double(s.sum[l1]))*unit,(double(s.sum[l1])-double(s.sum[l2]))*unit,(double(s.sum[l2])-double(s.sum[l3]))*unit,double(s.sum[l3])*unit);
 }std::puts("");
}
int wmain(int argc,wchar_t** argv){
 if(argc==2 && wcscmp(argv[1],L"--load")==0)return Load();
 if(argc<2 || argc>3)return 2;
 if(argc==3){FILE* file=nullptr;if(_wfopen_s(&file,argv[2],L"r"))return 7;Phase p{};while(std::fscanf(file,"PHASE %u %15s %llu %llu\n",&p.cpu,p.mode,&p.start,&p.end)==4)phases.push_back(p);std::fclose(file);}
 EVENT_TRACE_LOGFILEW log{};log.LogFileName=argv[1];log.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;log.EventRecordCallback=Record;
 auto handle=OpenTraceW(&log);if(handle==INVALID_PROCESSTRACE_HANDLE){std::printf("OpenTrace error=%lu\n",GetLastError());return 3;}
 auto result=ProcessTrace(&handle,1,nullptr,nullptr);CloseTrace(handle);
 std::printf("ProcessTrace=%lu events=%llu pmc_events=%llu lost=%lu malformed=%u\n",result,total,pmc,log.LogfileHeader.EventsLost,malformed);
 for(int i=0;i<static_cast<int>(counterCount);i++)std::printf("counter[%d]=%ls\n",i,names[i].c_str());
 int ins=Index(L"CFMMemV5ABoundLoads"),cycles=Index(L"UnhaltedCoreCyclesFixed"),misses=Index(L"CFMMemV5ZStallL3");
 if(cycles<0){std::puts("Unknown counter mapping: refusing to calculate ratios");return 8;}
 for(unsigned cpu=0;cpu<256;cpu++){auto& s=stats[cpu];if(!s.events)continue;
  auto previous=SetThreadAffinityMask(GetCurrentThread(),DWORD_PTR(1)<<cpu);int regs[4]{};if(previous){__cpuidex(regs,0x1a,0);SetThreadAffinityMask(GetCurrentThread(),previous);}
  unsigned type=static_cast<unsigned>(regs[0])>>24;unsigned unavailable=0;if(previous){auto old=SetThreadAffinityMask(GetCurrentThread(),DWORD_PTR(1)<<cpu);__cpuidex(regs,0xA,0);unavailable=static_cast<unsigned>(regs[1]);SetThreadAffinityMask(GetCurrentThread(),old);}std::printf("arch_event_unavailable=%X ",unavailable);
  std::printf("LP%u type=%02X events=%llu deltas=%llu resets=%llu ",cpu,type,s.events,s.valid,s.resets);Print(s,ins,cycles,misses);
 }
 for(auto& p:phases){std::printf("PHASE LP%u %s seconds=%.3f ",p.cpu,p.mode,double(p.end-p.start)/1e7);Print(p.stats,ins,cycles,misses);}
 return result || !pmc || log.LogfileHeader.EventsLost || malformed ? 4:0;
}






