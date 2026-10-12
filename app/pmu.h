#pragma once
#include "hardware.h"
#include <memory>
struct TmaReading { double retiring=-1, frontend=-1, badSpec=-1, backend=-1, core=-1, memory=-1; double l1=-1,l2=-1,l3=-1,dram=-1,topAge=-1,memoryAge=-1; std::uint64_t slots=0,memoryCycles=0; };
TmaReading CalculateTma(const std::array<std::uint64_t,5>& counts);
TmaReading CalculateMemoryBound(const std::array<std::uint64_t,5>& counts);
struct PmuReading { TmaReading tma; DWORD tmaError=ERROR_NOT_SUPPORTED; std::vector<double> ipc; double pCoreMpki=-1; DWORD error=ERROR_NOT_SUPPORTED; std::uint64_t events=0; };
class PmuSampler {
public:
 explicit PmuSampler(const CpuTopology& topology, const wchar_t* diagnosticFile=nullptr);
 ~PmuSampler();
 PmuSampler(const PmuSampler&)=delete;
 PmuSampler& operator=(const PmuSampler&)=delete;
 PmuReading Poll(bool enabled,bool llcEnabled,bool tmaEnabled=false);
private:
 struct Impl; std::unique_ptr<Impl> impl_;
};
bool PmuDelta(const std::array<std::uint64_t,3>& previous,const std::array<std::uint64_t,3>& current,double seconds,std::array<std::uint64_t,3>& delta);
