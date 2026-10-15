// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pmu.h"
#include <evntrace.h>
#include <evntcons.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <shlobj.h>
namespace {
// Preserve legacy owner/session and registered event names for upgrade interoperability.
constexpr wchar_t SessionName[]=L"CpuFreqMonitor.PMU.v1";
constexpr GUID SessionGuid={0x62c40534,0x96a8,0x4ec9,{0xb3,0x23,0x6c,0x2b,0x47,0xdf,0x8e,0x81}};
constexpr GUID ThreadGuid={0x3d6fa8d1,0xfe05,0x11d0,{0x9d,0xda,0x00,0xc0,0x4f,0xd7,0xba,0x7c}};
constexpr GUID PerfGuid={0xce1dbfb4,0x137e,0x4da6,{0x87,0xb0,0x3f,0x59,0xaa,0x10,0x2c,0xbc}};
constexpr const wchar_t* Names[]={L"InstructionsRetiredFixed",L"UnhaltedCoreCyclesFixed",L"LLCMisses",L"CFMTmaV1Slots",L"CFMTmaV1Retired",L"CFMTmaV1BadSpec",L"CFMTmaV1Backend",L"CFMTmaV1Memory",L"CFMMemV5ABoundLoads",L"CFMMemV5XStallL1",L"CFMMemV5YStallL2",L"CFMMemV5ZStallL3"};
struct Properties {
 EVENT_TRACE_PROPERTIES value{};wchar_t name[128]{};wchar_t file[MAX_PATH]{};
 Properties(){value.Wnode.BufferSize=sizeof(Properties);value.Wnode.Guid=SessionGuid;value.Wnode.ClientContext=1;value.Wnode.Flags=WNODE_FLAG_TRACED_GUID;value.LoggerNameOffset=offsetof(Properties,name);}
};
bool Sources(std::array<ULONG,12>& sources,unsigned mask){
 ULONG bytes=0;auto status=TraceQueryInformation(0,TraceProfileSourceListInfo,nullptr,0,&bytes);
 if((status!=ERROR_INSUFFICIENT_BUFFER && status!=ERROR_BAD_LENGTH) || bytes<sizeof(PROFILE_SOURCE_INFO) || bytes>1024*1024)return false;
 std::vector<unsigned char> buffer(bytes);
 if(TraceQueryInformation(0,TraceProfileSourceListInfo,buffer.data(),bytes,&bytes)!=ERROR_SUCCESS)return false;
 unsigned found=0;
 for(std::size_t offset=0;offset+offsetof(PROFILE_SOURCE_INFO,Description)<buffer.size();){
  auto* info=reinterpret_cast<const PROFILE_SOURCE_INFO*>(buffer.data()+offset);
  const std::size_t size=info->NextEntryOffset?info->NextEntryOffset:buffer.size()-offset;
  if(size>buffer.size()-offset || size<offsetof(PROFILE_SOURCE_INFO,Description)+sizeof(wchar_t))return false;
  const auto chars=(size - offsetof(PROFILE_SOURCE_INFO,Description))/sizeof(wchar_t);
  const auto* end=std::find(info->Description,info->Description+chars,L'\0');if(end==info->Description+chars)return false;
  for(unsigned i=0;i<12;++i)if((mask&(1u<<i)) && std::wstring(info->Description,end)==Names[i]){sources[i]=info->Source;found|=1u<<i;}
  if(!info->NextEntryOffset)break;offset+=size;
 }
 return found==mask;
}
DWORD WprCommand(const std::wstring& exe,const std::wstring& args){
 std::wstring command=L"\""+exe+L"\" "+args;STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return GetLastError();
 DWORD code=ERROR_TIMEOUT;const auto wait=WaitForSingleObject(process.hProcess,5000);
 if(wait==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);else{TerminateProcess(process.hProcess,ERROR_TIMEOUT);WaitForSingleObject(process.hProcess,1000);}
 CloseHandle(process.hThread);CloseHandle(process.hProcess);return code;
}
DWORD RegisterTma(){
 wchar_t module[32768]{};const DWORD length=GetModuleFileNameW(nullptr,module,32768);if(!length || length>=32768)return ERROR_INVALID_NAME;
 std::wstring profile(module);profile=profile.substr(0,profile.find_last_of(L"\\")+1)+L"tma-register.wprp";
 if(GetFileAttributesW(profile.c_str())==INVALID_FILE_ATTRIBUTES)return ERROR_FILE_NOT_FOUND;
 PWSTR folder=nullptr;std::wstring exe;
 if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86,0,nullptr,&folder))){exe=std::wstring(folder)+L"\\Windows Kits\\10\\Windows Performance Toolkit\\wpr.exe";CoTaskMemFree(folder);}
 if(exe.empty() || GetFileAttributesW(exe.c_str())==INVALID_FILE_ATTRIBUTES){wchar_t system[MAX_PATH]{};if(!GetSystemDirectoryW(system,MAX_PATH))return GetLastError();exe=std::wstring(system)+L"\\wpr.exe";}
 // Exclusive product owner mutex is already held. Reclaim only our named registrar instance.
 const std::wstring instance=L" -instancename CpuFreqMonitor.TMA.Register.v1";
 WprCommand(exe,L"-cancel"+instance);
 const auto status=WprCommand(exe,L"-start \""+profile+L"!TmaRegister\""+instance);
 const auto cleanup=WprCommand(exe,L"-cancel"+instance);
 return status?status:cleanup;
}

}
bool PmuDelta(const std::array<std::uint64_t,3>& previous,const std::array<std::uint64_t,3>& current,double seconds,std::array<std::uint64_t,3>& delta){
 delta={};if(!std::isfinite(seconds) || seconds<=0 || seconds>2)return false;
 for(unsigned i=0;i<3;++i){if(current[i]<previous[i])return false;delta[i]=current[i]-previous[i];}
 if(static_cast<double>(delta[1])>seconds*2e10 || static_cast<double>(delta[0])>seconds*1e12)return false;
 return delta[1]>0 && delta[0]<=delta[1]*64;
}
struct PmuSampler::Impl {
 const CpuTopology& topology;std::wstring diagnosticFile;
 struct Cpu {bool seen=false;std::uint64_t time=0;std::array<std::uint64_t,12> previous{},pending{},published{};double newest=0,publishedAt=0;};
 std::vector<Cpu> cpus;std::vector<std::size_t> systemIndex;std::mutex mutex;
 TRACEHANDLE session=0,consumer=INVALID_PROCESSTRACE_HANDLE;HANDLE owner=nullptr;std::thread worker;
 std::atomic<DWORD> error{ERROR_NOT_SUPPORTED};std::atomic<bool> stopping{false};
 std::array<int,12> mapping{{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};bool invalidEpoch=false;unsigned count=0,mask=0;std::vector<unsigned> active;std::uint64_t events=0;
 double frequency=0,startedAt=0,retryAt=0,checkedAt=0,publishedAt=0;bool requested=false,requestedLlc=false,requestedTma=false;DWORD registrationError=ERROR_SUCCESS,tmaFailure=ERROR_SUCCESS;double tmaRetryAt=0;bool collectingTma=false,collectingMemory=false;TmaReading cachedTop,cachedMemory;double topAt=0,memoryAt=0;std::vector<double> ipcCache,ipcAt;double mpkiCache=-1,mpkiAt=0;
 explicit Impl(const CpuTopology& t,const wchar_t* file):topology(t),diagnosticFile(file?file:L""),cpus(t.logical.size()){
  ipcCache.assign(cpus.size(),-1);ipcAt.assign(cpus.size(),0);
  LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=static_cast<double>(f.QuadPart);
  for(WORD group=0;group<GetActiveProcessorGroupCount();++group)for(DWORD n=0;n<GetActiveProcessorCount(group);++n){
   auto it=std::find_if(t.logical.begin(),t.logical.end(),[&](const LogicalCpu& cpu){return cpu.group==group && cpu.number==n;});
   systemIndex.push_back(it==t.logical.end()?cpus.size():static_cast<std::size_t>(it-t.logical.begin()));
  }
 }
 ~Impl(){Stop();}
 void Stop(bool clear=true){
  if(clear){cachedTop={};cachedMemory={};topAt=memoryAt=0;std::fill(ipcCache.begin(),ipcCache.end(),-1);std::fill(ipcAt.begin(),ipcAt.end(),0);mpkiCache=-1;mpkiAt=0;}
  stopping=true;
  if(session){Properties p;ControlTraceW(session,nullptr,&p.value,EVENT_TRACE_CONTROL_STOP);session=0;}
  if(consumer!=INVALID_PROCESSTRACE_HANDLE){CloseTrace(consumer);consumer=INVALID_PROCESSTRACE_HANDLE;}
  if(worker.joinable())worker.join();
  if(clear && owner){ReleaseMutex(owner);CloseHandle(owner);owner=nullptr;}
  std::lock_guard<std::mutex> lock(mutex);cpus.assign(cpus.size(),{});mapping={{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};events=0;invalidEpoch=false;
 }
 static void WINAPI OnRecord(EVENT_RECORD* r){auto* self=static_cast<Impl*>(r->UserContext);if(self && !self->stopping){try{self->Record(r);}catch(...){self->error=ERROR_NOT_ENOUGH_MEMORY;}}}
 void Record(EVENT_RECORD* r){
  std::lock_guard<std::mutex> lock(mutex);
  if(IsEqualGUID(r->EventHeader.ProviderId,PerfGuid) && r->EventHeader.EventDescriptor.Opcode==48){
   if(r->UserDataLength<4){error=ERROR_INVALID_DATA;return;}
   ULONG n=0;std::memcpy(&n,r->UserData,4);if(n!=count){error=ERROR_INVALID_DATA;return;}
   const auto* p=reinterpret_cast<const wchar_t*>(static_cast<const unsigned char*>(r->UserData)+4);
   const auto* end=reinterpret_cast<const wchar_t*>(static_cast<const unsigned char*>(r->UserData)+r->UserDataLength);
   std::array<int,12> next{{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};
   for(unsigned j=0;j<count;++j){auto* terminator=std::find(p,end,L'\0');if(terminator==end){error=ERROR_INVALID_DATA;return;}
    for(auto k:active)if(std::wstring(p,terminator)==Names[k])next[k]=static_cast<int>(j);p=terminator+1;
   }
   for(auto k:active)if(next[k]<0){error=ERROR_INVALID_DATA;return;}
   if(next!=mapping){mapping=next;cpus.assign(cpus.size(),{});}return;
  }
  if(!IsEqualGUID(r->EventHeader.ProviderId,ThreadGuid) || r->EventHeader.EventDescriptor.Opcode!=36)return;
  for(auto k:active)if(mapping[k]<0)return;
  const auto index=r->BufferContext.ProcessorIndex;if(index>=systemIndex.size() || systemIndex[index]>=cpus.size())return;
  for(USHORT i=0;i<r->ExtendedDataCount;++i){auto& ext=r->ExtendedData[i];if(ext.ExtType!=EVENT_HEADER_EXT_TYPE_PMC_COUNTERS)continue;
   if(ext.DataSize!=count*sizeof(std::uint64_t) || !ext.DataPtr){error=ERROR_INVALID_DATA;return;}
   std::array<std::uint64_t,12> values{};auto* raw=reinterpret_cast<const std::uint64_t*>(ext.DataPtr);
   for(auto k:active)values[k]=raw[mapping[k]];
   const auto time=static_cast<std::uint64_t>(r->EventHeader.TimeStamp.QuadPart);auto& cpu=cpus[systemIndex[index]];std::array<std::uint64_t,12> delta{};
   const double dt=time>cpu.time?static_cast<double>(time-cpu.time)/frequency:0;
   std::array<std::uint64_t,3> basic{};bool valid=cpu.seen && PmuDelta({cpu.previous[0],cpu.previous[1],cpu.previous[2]},{values[0],values[1],values[2]},dt,basic);
   bool discontinuity=cpu.seen && (time<cpu.time || dt>2);
   for(auto k:active){if(values[k]<cpu.previous[k]){valid=false;discontinuity=true;}else delta[k]=values[k]-cpu.previous[k];}
   if(requestedTma && topology.logical[systemIndex[index]].kind==CoreKind::Performance)for(auto k:active)if(k>=3 && static_cast<double>(delta[k])>dt*2e12)valid=false;
   if(valid){
    for(auto k:active)cpu.pending[k]+=delta[k];cpu.newest=static_cast<double>(time)/frequency;
   }else if(cpu.seen){
    // Zero cycles on an idle interval is not a reset of the other counter group.
    // Only a P-core discontinuity invalidates cached P-core TMA; E cores are excluded.
    if(topology.logical[systemIndex[index]].kind==CoreKind::Performance && discontinuity)invalidEpoch=true;
    cpu.pending={};cpu.published={};cpu.publishedAt=0;}
   cpu.previous=values;cpu.time=time;cpu.seen=true;++events;
  }
 }
 DWORD ResolveMapping(const std::array<ULONG,12>& sources){
  ULONG bytes=0;auto status=TraceQueryInformation(0,TracePmcSessionInformation,nullptr,0,&bytes);
  if((status!=ERROR_INSUFFICIENT_BUFFER && status!=ERROR_BAD_LENGTH) || bytes<sizeof(ETW_PMC_SESSION_INFO) || bytes>1024*1024)return status?status:ERROR_INVALID_DATA;
  std::vector<unsigned char> buffer(bytes);
  status=TraceQueryInformation(0,TracePmcSessionInformation,buffer.data(),bytes,&bytes);if(status!=ERROR_SUCCESS)return status;
  for(std::size_t offset=0;offset+sizeof(ETW_PMC_SESSION_INFO)<=buffer.size();){
   const auto* info=reinterpret_cast<const ETW_PMC_SESSION_INFO*>(buffer.data()+offset);
   const std::size_t size=info->NextEntryOffset?info->NextEntryOffset:buffer.size()-offset;
   if(size>buffer.size()-offset || size<sizeof(*info))return ERROR_INVALID_DATA;
   if(info->LoggerId==static_cast<USHORT>(session & 0xffff)){
    if(info->ProfileSourceCount!=count || size<sizeof(*info)+count*sizeof(ULONG))return ERROR_INVALID_DATA;
    const auto* actual=reinterpret_cast<const ULONG*>(buffer.data()+offset+sizeof(*info));
    std::array<int,12> next{{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};
    for(auto k:active)for(unsigned j=0;j<count;++j)if(actual[j]==sources[k])next[k]=static_cast<int>(j);
    for(auto k:active)if(next[k]<0)return ERROR_INVALID_DATA;
    std::lock_guard<std::mutex> lock(mutex);mapping=next;return ERROR_SUCCESS;
   }
   if(!info->NextEntryOffset)break;offset+=size;
  }
  return ERROR_INVALID_DATA;
 }
 DWORD Start(bool llc,bool tma,bool memory=false){
  collectingTma=tma;collectingMemory=tma && memory;
  if(!topology.intel || topology.family!=6 || (topology.model!=0x97 && topology.model!=0x9A))return ERROR_NOT_SUPPORTED;
  for(auto& cpu:topology.logical)if(cpu.pmuVersion<2 || cpu.fixedCount<2 || cpu.fixedWidth!=48)return ERROR_NOT_SUPPORTED;
  HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token))return GetLastError();
  TOKEN_PRIVILEGES privileges{};privileges.PrivilegeCount=1;
  const bool lookup=LookupPrivilegeValueW(nullptr,L"SeSystemProfilePrivilege",&privileges.Privileges[0].Luid)!=FALSE;
  privileges.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;SetLastError(ERROR_SUCCESS);
  const bool adjusted=lookup && AdjustTokenPrivileges(token,FALSE,&privileges,0,nullptr,nullptr)!=FALSE;
  const DWORD privilegeError=GetLastError();CloseHandle(token);
  if(!adjusted || privilegeError!=ERROR_SUCCESS)return privilegeError?privilegeError:ERROR_PRIVILEGE_NOT_HELD;
  mask=3u|(llc?4u:0u)|(tma?0xf8u:0u);active.clear();
  if(collectingMemory){
   // On the verified Windows 10 host, filling six generic positions and placing
   // restricted sources last avoids zero counts. This is not a portable HAL contract.
   // Fill all six available generic
   // counters and submit restricted MEMORY_ACTIVITY events last (PMC 0..3).
   // Two filler sources keep this placement stable with LLC disabled too.
   active={0,1,3,8,llc?2u:4u,9,10,11};mask=0;for(auto k:active)mask|=1u<<k;
  }else for(unsigned k=0;k<12;++k)if(mask&(1u<<k))active.push_back(k);count=static_cast<unsigned>(active.size());std::array<ULONG,12> sources{};
  if(!owner){
   owner=CreateMutexW(nullptr,TRUE,L"Global\\CpuFreqMonitor.PMU.Owner.v1");const auto mutexError=GetLastError();
   if(!owner)return mutexError;if(mutexError==ERROR_ALREADY_EXISTS){CloseHandle(owner);owner=nullptr;return ERROR_BUSY;}
  }
  if(!Sources(sources,mask)){
   if(tma){registrationError=RegisterTma();if(registrationError || !Sources(sources,mask))return registrationError?registrationError:ERROR_NOT_SUPPORTED;}
   else return ERROR_NOT_SUPPORTED;
  }
  // Only reclaim our GUID + name after acquiring the exclusive process owner lock.
  Properties old;auto status=ControlTraceW(0,SessionName,&old.value,EVENT_TRACE_CONTROL_QUERY);
  if(status==ERROR_SUCCESS){if(!IsEqualGUID(old.value.Wnode.Guid,SessionGuid))return ERROR_ALREADY_EXISTS;
   status=ControlTraceW(0,SessionName,&old.value,EVENT_TRACE_CONTROL_STOP);if(status!=ERROR_SUCCESS)return status;
  }else if(status!=ERROR_WMI_INSTANCE_NOT_FOUND)return status;
  Properties props;props.value.BufferSize=64;props.value.MinimumBuffers=(std::max)(32ul,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)*2);
  props.value.MaximumBuffers=props.value.MinimumBuffers*2;props.value.FlushTimer=1;
  props.value.LogFileMode=EVENT_TRACE_REAL_TIME_MODE|EVENT_TRACE_SYSTEM_LOGGER_MODE;
  if(!diagnosticFile.empty()){if(diagnosticFile.size()>=MAX_PATH)return ERROR_INVALID_NAME;wcscpy_s(props.file,diagnosticFile.c_str());props.value.LogFileNameOffset=offsetof(Properties,file);props.value.LogFileMode|=EVENT_TRACE_FILE_MODE_SEQUENTIAL;}props.value.EnableFlags=EVENT_TRACE_FLAG_CSWITCH;
  status=StartTraceW(&session,SessionName,&props.value);if(status!=ERROR_SUCCESS){session=0;return status;}
  // Attach before configuring counters. The submitted list defines direct API order;
  // newer Windows configuration queries and optional metadata cross-check it.
  EVENT_TRACE_LOGFILEW log{};log.LoggerName=const_cast<wchar_t*>(SessionName);log.Context=this;
  log.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD|PROCESS_TRACE_MODE_RAW_TIMESTAMP;log.EventRecordCallback=OnRecord;
  consumer=OpenTraceW(&log);if(consumer==INVALID_PROCESSTRACE_HANDLE)return GetLastError();
  stopping=false;error=ERROR_SUCCESS;startedAt=checkedAt=publishedAt=MonotonicSeconds();
  const auto trace=consumer;
  try{worker=std::thread([this,trace]{TRACEHANDLE handle=trace;const auto result=ProcessTrace(&handle,1,nullptr,nullptr);if(!stopping)error=result?result:ERROR_WMI_INSTANCE_NOT_FOUND;});}catch(...){return ERROR_NOT_ENOUGH_MEMORY;}
  std::array<ULONG,12> submitted{};for(unsigned j=0;j<count;++j)submitted[j]=sources[active[j]];
  status=TraceSetInformation(session,TracePmcCounterListInfo,submitted.data(),count*sizeof(ULONG));if(status!=ERROR_SUCCESS)return status;
  status=ResolveMapping(sources);
  if(status==ERROR_NOT_SUPPORTED || status==ERROR_INVALID_PARAMETER){
   // Direct API collection preserves the submitted ULONG source-list order.
   // WPR instead builds its own list; any live config metadata is cross-checked.
   std::lock_guard<std::mutex> lock(mutex);for(unsigned j=0;j<count;++j)mapping[active[j]]=static_cast<int>(j);
  }else if(status!=ERROR_SUCCESS)return status;
  CLASSIC_EVENT_ID event{};event.EventGuid=ThreadGuid;event.Type=36;return TraceSetInformation(session,TracePmcEventListInfo,&event,sizeof(event));
 }
 PmuReading Poll(bool enabled,bool llc,bool tma){
  PmuReading r;r.ipc.assign(cpus.size(),-1);double now=MonotonicSeconds();
  if(enabled!=requested || llc!=requestedLlc || tma!=requestedTma){Stop();requested=enabled;requestedLlc=llc;requestedTma=tma;registrationError=ERROR_SUCCESS;tmaFailure=ERROR_SUCCESS;tmaRetryAt=0;retryAt=0;error=ERROR_NOT_SUPPORTED;}
  if(!enabled)return r;
  if(session && collectingTma && now-startedAt>=5 && error==ERROR_SUCCESS){
   const bool nextMemory=!collectingMemory;Stop(false);
   const auto status=Start(llc,true,nextMemory);
   if(status){Stop();tmaFailure=status;tmaRetryAt=MonotonicSeconds()+30;
    const auto basic=Start(llc,false);if(basic){Stop();error=basic;retryAt=MonotonicSeconds()+30;}
   }
  }
  if(session && tma && !collectingTma && now>=tmaRetryAt){Stop();retryAt=0;}
  if(!session && now>=retryAt){
   auto status=Start(llc,tma);
   if(status!=ERROR_SUCCESS && tma){Stop();tmaFailure=status;tmaRetryAt=MonotonicSeconds()+30;status=Start(llc,false);}
   else if(status==ERROR_SUCCESS && tma)tmaFailure=ERROR_SUCCESS;
   if(status!=ERROR_SUCCESS){Stop();error=status;retryAt=MonotonicSeconds()+30;}
  }
  now=MonotonicSeconds();
  if(session && now-checkedAt>=1){Properties p;auto status=ControlTraceW(session,nullptr,&p.value,EVENT_TRACE_CONTROL_QUERY);checkedAt=now;
   if(status!=ERROR_SUCCESS)error=status;else if(p.value.EventsLost || p.value.RealTimeBuffersLost || p.value.LogBuffersLost)error=ERROR_INVALID_DATA;
  }
  if(session && error==ERROR_SUCCESS && now-startedAt>5){std::lock_guard<std::mutex> lock(mutex);if(!events)error=ERROR_TIMEOUT;}
  if(session && error!=ERROR_SUCCESS){const auto status=error.load();const bool fallback=collectingTma;Stop();error=status;retryAt=now+30;
   if(fallback){tmaFailure=status;tmaRetryAt=MonotonicSeconds()+30;const auto basicStatus=Start(llc,false);if(basicStatus){Stop();error=basicStatus;}}
  }
  r.error=error.load();r.tmaError=tma?(tmaFailure?tmaFailure:r.error):ERROR_NOT_SUPPORTED;if(!session || r.error!=ERROR_SUCCESS)return r;
  std::lock_guard<std::mutex> lock(mutex);
  if(invalidEpoch){cachedTop={};cachedMemory={};topAt=memoryAt=0;std::fill(ipcCache.begin(),ipcCache.end(),-1);mpkiCache=-1;invalidEpoch=false;}
  r.events=events;const bool publish=now-publishedAt>=1;if(publish)publishedAt=now;double instructions=0,misses=0,mpkiNewest=now;bool allP=true;unsigned pCount=0;std::array<std::uint64_t,5> tmaCounts{},memoryCounts{};bool allTma=true;
  for(std::size_t i=0;i<cpus.size();++i){auto& cpu=cpus[i];if(publish){cpu.published=cpu.pending;cpu.publishedAt=cpu.newest;cpu.pending={};}
   const bool valid=cpu.published[1]>0 && now-cpu.publishedAt>=0 && now-cpu.publishedAt<2;
   if(valid){r.ipc[i]=static_cast<double>(cpu.published[0])/static_cast<double>(cpu.published[1]);ipcCache[i]=r.ipc[i];ipcAt[i]=cpu.publishedAt;}
   else if(now-startedAt<2 && ipcCache[i]>=0 && now>=ipcAt[i] && now-ipcAt[i]<2)r.ipc[i]=ipcCache[i];
   if(topology.logical[i].kind==CoreKind::Performance){++pCount;if(collectingTma){
    if(!valid || (!collectingMemory && !cpu.published[3]))allTma=false;
    else if(collectingMemory){memoryCounts[0]+=cpu.published[1];for(unsigned k=0;k<4;++k)memoryCounts[k+1]+=cpu.published[k+8];}
    else for(unsigned k=0;k<5;++k)tmaCounts[k]+=cpu.published[k+3];
   }if(!valid || !cpu.published[0])allP=false;else{instructions+=static_cast<double>(cpu.published[0]);misses+=static_cast<double>(cpu.published[2]);mpkiNewest=(std::min)(mpkiNewest,cpu.publishedAt);}}
  }
  if(collectingTma && publish && allTma && pCount){
   if(collectingMemory){cachedMemory=CalculateMemoryBound(memoryCounts);memoryAt=now;}
   else{cachedTop=CalculateTma(tmaCounts);topAt=now;}
  }
  // Each partition comes from one simultaneous counter group; never subtract
  // counters from different epochs. Expire cached groups after a missed full turn.
  if(collectingTma){
   if(topAt>0 && now>=topAt && now-topAt<12){r.tma=cachedTop;r.tma.topAge=now-topAt;}
   if(memoryAt>0 && now>=memoryAt && now-memoryAt<12){r.tma.l1=cachedMemory.l1;r.tma.l2=cachedMemory.l2;r.tma.l3=cachedMemory.l3;r.tma.dram=cachedMemory.dram;r.tma.memoryCycles=cachedMemory.memoryCycles;r.tma.memoryAge=now-memoryAt;}
  }
  if(llc && allP && pCount && instructions>0){r.pCoreMpki=misses*1000/instructions;mpkiCache=r.pCoreMpki;mpkiAt=mpkiNewest;}
  else if(now-startedAt<2 && llc && mpkiCache>=0 && now>=mpkiAt && now-mpkiAt<2)r.pCoreMpki=mpkiCache;return r;
 }
};
PmuSampler::PmuSampler(const CpuTopology& topology,const wchar_t* file):impl_(std::make_unique<Impl>(topology,file)){}
PmuSampler::~PmuSampler()=default;
PmuReading PmuSampler::Poll(bool enabled,bool llc,bool tma){return impl_->Poll(enabled,llc,tma);}

TmaReading CalculateTma(const std::array<std::uint64_t,5>& counts){
 TmaReading r;if(!counts[0])return r;r.slots=counts[0];const double unit=100./static_cast<double>(counts[0]);
 r.retiring=static_cast<double>(counts[1])*unit;r.badSpec=static_cast<double>(counts[2])*unit;r.backend=static_cast<double>(counts[3])*unit;r.memory=static_cast<double>(counts[4])*unit;
 r.frontend=100.-r.retiring-r.badSpec-r.backend;r.core=r.backend-r.memory;
 // Never clamp inconsistent event sets into a plausible partition. Independent raw ratios remain visible.
 if(r.frontend<0 || r.frontend>100)r.frontend=-1;if(r.core<0 || r.core>100)r.core=-1;
 return r;
}

TmaReading CalculateMemoryBound(const std::array<std::uint64_t,5>& counts){
 TmaReading r;if(!counts[0])return r;
 const double cycles=static_cast<double>(counts[0]);
 for(unsigned i=1;i<5;++i)if(static_cast<double>(counts[i])>cycles*1.02)return r;
 r.memoryCycles=counts[0];const double unit=100./cycles;
 // Intel Alder Lake formulas: load-bound minus L1 stalls; successive stall
 // differences; L3 stalls. The denominator is thread cycles, not top-level slots.
 r.l1=(std::max)(0.,static_cast<double>(counts[1])-static_cast<double>(counts[2]))*unit;
 r.l2=(static_cast<double>(counts[2])-static_cast<double>(counts[3]))*unit;
 r.l3=(static_cast<double>(counts[3])-static_cast<double>(counts[4]))*unit;
 r.dram=static_cast<double>(counts[4])*unit;
 for(auto* v:{&r.l1,&r.l2,&r.l3,&r.dram})if(!std::isfinite(*v) || *v<0 || *v>100)*v=-1;
 return r;
}
