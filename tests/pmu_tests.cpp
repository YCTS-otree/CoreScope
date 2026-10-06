#include "../app/pmu.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <limits>
#include <algorithm>
#include <evntrace.h>
int wmain(int argc,wchar_t** argv){
 auto tma=CalculateTma({1000,300,0,600,400});assert(tma.slots==1000 && std::abs(tma.retiring-30)<1e-8 && std::abs(tma.frontend-10)<1e-8 && tma.badSpec==0 && std::abs(tma.core-20)<1e-8);
 assert(CalculateTma({0,0,0,0,0}).slots==0);
 auto inconsistent=CalculateTma({100,60,30,40,50});assert(inconsistent.frontend<0 && inconsistent.core<0 && inconsistent.badSpec==30);
 std::array<std::uint64_t,3> d{};
 assert(PmuDelta({100,200,10},{110,220,11},.25,d) && d[0]==10 && d[1]==20 && d[2]==1);
 assert(!PmuDelta({100,200,10},{99,220,11},.25,d));
 assert(!PmuDelta({100,200,10},{110,220,9},.25,d));
 assert(!PmuDelta({100,200,10},{110,200,11},.25,d));
 assert(!PmuDelta({100,200,10},{110,220,11},3,d));
 assert(!PmuDelta({100,200,10},{110,220,11},0,d));
 assert(!PmuDelta({100,200,10},{110,220,11},std::numeric_limits<double>::quiet_NaN(),d));
 assert(!PmuDelta({100,200,10},{110,100000000000ull,11},.25,d));
 CpuTopology unsupported;PmuSampler unavailable(unsupported);assert(unavailable.Poll(true,true).error==ERROR_NOT_SUPPORTED);
 std::puts("PASS: counter reset/wrap, zero cycles, suspend, invalid time, spike and unsupported platform");
  if(argc>=2 && wcscmp(argv[1],L"--orphan")==0){
  const auto topology=DetectTopology();auto* abandoned=new PmuSampler(topology);
  auto r=abandoned->Poll(true,true);Sleep(1500);ExitProcess(r.error?1u:0u);
 }
 if(argc>=2 && wcscmp(argv[1],L"--denied")==0){
  const auto topology=DetectTopology();PmuSampler denied(topology);auto r=denied.Poll(true,true);
  if(r.error!=ERROR_ACCESS_DENIED && r.error!=ERROR_PRIVILEGE_NOT_HELD && r.error!=ERROR_NOT_ALL_ASSIGNED)return 1;
  if(std::any_of(r.ipc.begin(),r.ipc.end(),[](double ipc){return ipc>=0;}))return 2;
  std::printf("PASS: unprivileged fallback error=%lu\n",r.error);return 0;
 }
 if(argc>=2 && wcscmp(argv[1],L"--cleanup")==0){
  const auto topology=DetectTopology();{PmuSampler sampler(topology);auto r=sampler.Poll(true,false);if(r.error){std::printf("cleanup acquisition error=%lu\n",r.error);return 14;}sampler.Poll(false,false);}
  struct {EVENT_TRACE_PROPERTIES p{};wchar_t name[128]{};} props;props.p.Wnode.BufferSize=sizeof(props);props.p.LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
  const auto status=ControlTraceW(0,L"CpuFreqMonitor.PMU.v1",&props.p,EVENT_TRACE_CONTROL_QUERY);std::printf("Own session cleanup query=%lu\n",status);return status==ERROR_WMI_INSTANCE_NOT_FOUND?0:15;
 }
 if(argc>=2 && wcscmp(argv[1],L"--tma")==0){
  const auto topology=DetectTopology();PmuSampler sampler(topology);unsigned good=0;
  for(int phase=0;phase<3;++phase){const bool tmaEnabled=phase!=1;const bool llc=phase==0;
   for(int i=0;i<32;++i){Sleep(250);auto r=sampler.Poll(true,llc,tmaEnabled);if(r.error){std::printf("FAIL phase=%d error=%lu tmaError=%lu\n",phase,r.error,r.tmaError);return 10;}
    if(tmaEnabled && r.tma.slots && r.tmaError==0){++good;if(i==31)std::printf("phase=%d slots=%llu retiring=%.2f bad=%.2f memory=%.2f IPC0=%.2f MPKI=%.2f\n",phase,r.tma.slots,r.tma.retiring,r.tma.badSpec,r.tma.memory,r.ipc[0],r.pCoreMpki);}
    if(!tmaEnabled && (r.tma.slots || r.tmaError==ERROR_SUCCESS))return 11;
    if(!llc && r.pCoreMpki>=0)return 12;
   }
  }
  sampler.Poll(false,false,false);if(good<5)return 13;
  std::printf("PASS: TMA + IPC + LLC, normal IPC mode, TMA + IPC without LLC, disable good=%u\n",good);return 0;
 }
 if(argc<2 || wcscmp(argv[1],L"--live"))return 0;
 const auto topology=DetectTopology();
 wchar_t traceFile[MAX_PATH]{};if(!GetFullPathNameW(L"build\\pmu-native.etl",MAX_PATH,traceFile,nullptr))return 7;
 PmuSampler sampler(topology,traceFile);
 auto wait=[&](bool llc){unsigned good=0;double mpki=-1;
  for(unsigned i=0;i<40;++i){Sleep(250);const auto r=sampler.Poll(true,llc);if(r.error){std::printf("ETW error=%lu events=%llu\n",r.error,r.events);return false;}
   const auto valid=std::count_if(r.ipc.begin(),r.ipc.end(),[](double ipc){return std::isfinite(ipc) && ipc>=0;});
   if(static_cast<std::size_t>(valid)==topology.logical.size() && (!llc || r.pCoreMpki>=0)){++good;mpki=r.pCoreMpki;}
  }
  std::printf("mode LLC=%d complete_snapshots=%u MPKI=%.4f\n",llc,good,mpki);return good>0;
 };
 if(!wait(true))return 1;
 {PmuSampler peer(topology);auto r=peer.Poll(true,true);if(r.error!=ERROR_BUSY)return 2;}
 if(!wait(false))return 3;
 auto off=sampler.Poll(false,false);if(off.pCoreMpki>=0 || std::any_of(off.ipc.begin(),off.ipc.end(),[](double ipc){return ipc>=0;}))return 4;
 if(!wait(true))return 5;
 sampler.Poll(false,false);
 struct {EVENT_TRACE_PROPERTIES p{};wchar_t name[128]{};} props;
 props.p.Wnode.BufferSize=sizeof(props);props.p.LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
 auto status=ControlTraceW(0,L"CpuFreqMonitor.PMU.v1",&props.p,EVENT_TRACE_CONTROL_QUERY);
 if(status!=ERROR_WMI_INSTANCE_NOT_FOUND){std::printf("cleanup status=%lu\n",status);return 6;}
 std::puts("PASS: live all-LP data, peer conflict, IPC-only restart, disable/re-enable orphan recovery and session cleanup");
}
