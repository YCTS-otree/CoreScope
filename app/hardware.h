#pragma once
#include "pawnio_client.h"
#include <array>
#include <string>
#include <vector>

enum class CoreKind { Unknown, Performance, Efficiency };
struct LogicalCpu {
    WORD group=0; BYTE number=0; unsigned physical=0, package=0, apicId=0, hardwareCore=0;
    CoreKind kind=CoreKind::Unknown; unsigned pmuVersion=0, fixedCount=0, fixedWidth=0;
};
struct PhysicalCpu { std::vector<std::size_t> siblings; CoreKind kind=CoreKind::Unknown; unsigned package=0; };
struct CpuTopology {
    std::wstring name=L"CPU"; bool intel=false; unsigned family=0, model=0, packages=0;
    std::vector<LogicalCpu> logical; std::vector<PhysicalCpu> cores;
};
CpuTopology DetectTopology();
std::wstring ShortCpuName(std::wstring name);
const wchar_t* CoreKindName(CoreKind kind);
struct CoreReading { double temperature=-1; int tjMax=0; bool throttled=false; };
struct HardwareReading {
    std::vector<double> frequency;
    std::vector<CoreReading> cores;
    bool packageThrottled=false;
    double packageTemperature=-1, packageWatts=-1, readGBs=-1, writeGBs=-1;
    // MPKI is filled independently by PmuSampler; unavailable values stay negative.
    double llcMissRate=-1, llcReferenceRate=-1, llcMpki=-1;
    std::uint64_t ioctlCount=0;
};
struct CounterDelta {
    std::uint64_t previous=0; bool valid=false;
    bool Update(std::uint64_t current, unsigned width, double seconds, std::uint64_t& delta);
    void Reset() { valid=false; }
};
struct AlertFilter {
    double ema=-1, heldSeconds=0; int level=0;
    int Update(double value, double seconds, double orange, double red);
};
class HardwareSampler {
public:
    explicit HardwareSampler(const CpuTopology& topology);
    void Initialize(const std::wstring& directory, bool diagnostics=false);
    void Poll(HardwareReading& reading, bool memoryEnabled=true);
    bool Available() const { return msrReady_; }
    DWORD PmuError() const { return pmuError_; }
    DWORD ImcError() const { return imcError_; }
private:
    void InitializeImc();
    std::wstring directory_;
    bool diagnostics_=false, imcAttempted_=false;
    const CpuTopology& topology_;
    PawnIoClient msr_, imc_;
    bool msrReady_=false, imcReady_=false, supported_=false;
    DWORD pmuError_=ERROR_NOT_SUPPORTED, imcError_=ERROR_NOT_SUPPORTED;
    unsigned busMHz_=100;
    std::vector<int> tjMax_;
    std::vector<bool> coreAvailable_;
    struct Package { std::size_t representative=0; int tjMax=0; double energyUnit=0, lastTime=0; CounterDelta energy; bool thermal=true, power=true; };
    std::vector<Package> packages_;
    std::array<CounterDelta,8> imcCounters_{};
    std::array<bool,8> imcActive_{};
    double lastImcTime_=0;
};
double MonotonicSeconds();
