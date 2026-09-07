#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <vector>

#include "pawnio_client.h"
#include "monitor.h"

namespace {
constexpr DWORD SAMPLE_INTERVAL_MS = 1000;
constexpr std::uint32_t IA32_PERF_STATUS = 0x198;
std::atomic_bool g_stopRequested{false};
HANDLE g_stopEvent = nullptr;

struct ProcessorLocation {
    WORD Group;
    BYTE Number;
};

BOOL WINAPI ConsoleControlHandler(DWORD controlType)
{
    switch (controlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_stopRequested.store(true, std::memory_order_relaxed);
        if (g_stopEvent != nullptr) SetEvent(g_stopEvent);
        return TRUE;
    default:
        return FALSE;
    }
}

void PrintTimestamp()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::printf("[%02u:%02u:%02u.%03u]", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}

void PrintError(const char* message, DWORD status)
{
    PrintTimestamp();
    std::printf(" %s: 0x%08lX\n", message, status);
    std::fflush(stdout);
}

bool GetModulePath(std::array<wchar_t, MAX_PATH>& path, DWORD& error)
{
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        error = GetLastError();
        if (error == ERROR_SUCCESS) error = ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    wchar_t* separator = wcsrchr(path.data(), L'\\');
    if (separator == nullptr) {
        error = ERROR_INVALID_NAME;
        return false;
    }
    *separator = L'\0';
    if (wcscat_s(path.data(), path.size(), L"\\IntelMSR.bin") != 0) {
        error = ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

bool GetBusFrequencyMHz(std::uint32_t& busFrequencyMHz)
{
    int registers[4]{};
    __cpuid(registers, 0);
    const int maximumLeaf = registers[0];
    if (registers[1] != 0x756e6547 || registers[3] != 0x49656e69 || registers[2] != 0x6c65746e) {
        return false;
    }

    // Modern Intel client processors use a nominal 100 MHz reference clock.
    // Prefer CPUID.16H ECX when the platform exposes it, otherwise use that
    // architectural platform default (some firmware/hypervisors hide leaf 16H).
    busFrequencyMHz = 100;
    if (maximumLeaf >= 0x16) {
        __cpuidex(registers, 0x16, 0);
        if (registers[2] > 0 && registers[2] <= 1000) {
            busFrequencyMHz = static_cast<std::uint32_t>(registers[2]);
        }
    }
    return true;
}

std::vector<ProcessorLocation> EnumerateProcessors()
{
    std::vector<ProcessorLocation> processors;
    const WORD groupCount = GetActiveProcessorGroupCount();
    for (WORD group = 0; group < groupCount; ++group) {
        const DWORD count = GetActiveProcessorCount(group);
        for (DWORD number = 0; number < count && number < sizeof(KAFFINITY) * 8; ++number) {
            processors.push_back({group, static_cast<BYTE>(number)});
        }
    }
    return processors;
}
} // namespace

int wmain(int argc, wchar_t* argv[])
{
    if (argc == 2 && wcscmp(argv[1], L"--inspect") == 0) return RunMonitor(false, true, 60);
    if (argc == 2 && wcscmp(argv[1], L"--benchmark") == 0) return RunMonitor(false, true, 20);
    if (argc == 2 && wcscmp(argv[1], L"--sensor-test") == 0) return RunMonitor(false, true);
    if (argc == 1 || (argc == 2 && wcscmp(argv[1], L"--demo") == 0) || (argc == 2 && wcscmp(argv[1], L"--ui-test") == 0)) {
        return RunMonitor(argc == 2, argc == 2 && wcscmp(argv[1], L"--ui-test") == 0);
    }
    const bool runOnce = argc == 2 && wcscmp(argv[1], L"--once") == 0;
    if (argc > 1 && !runOnce) {
        std::printf("Usage: Cpu_freq_Moniter_v1.4.0.exe [--once | --demo | --ui-test | --sensor-test | --benchmark | --inspect]\n");
        return 2;
    }

    std::uint32_t busFrequencyMHz = 0;
    if (!GetBusFrequencyMHz(busFrequencyMHz)) {
        PrintError("Only Intel x64 processors are supported", ERROR_NOT_SUPPORTED);
        return 1;
    }

    const std::vector<ProcessorLocation> processors = EnumerateProcessors();
    if (processors.empty()) {
        PrintError("Cannot enumerate logical processors", GetLastError());
        return 1;
    }

    std::array<wchar_t, MAX_PATH> modulePath{};
    DWORD error = ERROR_SUCCESS;
    if (!GetModulePath(modulePath, error)) {
        PrintError("Cannot locate IntelMSR.bin", error);
        return 1;
    }

    PawnIoClient pawnIo;
    if (!pawnIo.OpenAndLoad(modulePath.data(), error)) {
        PrintError("Cannot initialize PawnIO (install the official signed PawnIO driver)", error);
        return 1;
    }

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent == nullptr || !SetConsoleCtrlHandler(ConsoleControlHandler, TRUE)) {
        PrintError("Cannot initialize Ctrl+C handling", GetLastError());
        if (g_stopEvent != nullptr) CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        return 1;
    }

    while (!g_stopRequested.load(std::memory_order_relaxed)) {
        PrintTimestamp();
        const int labelWidth = processors.size() >= 100 ? 3 : 2;
        for (std::size_t i = 0; i < processors.size(); ++i) {
            const ProcessorLocation processor = processors[i];
            GROUP_AFFINITY requested{};
            GROUP_AFFINITY previous{};
            requested.Group = processor.Group;
            requested.Mask = static_cast<KAFFINITY>(1) << processor.Number;

            std::uint64_t perfStatus = 0;
            DWORD readError = ERROR_SUCCESS;
            if (!SetThreadGroupAffinity(GetCurrentThread(), &requested, &previous)) {
                readError = GetLastError();
            } else {
                pawnIo.ReadMsr(IA32_PERF_STATUS, perfStatus, readError);
                if (!SetThreadGroupAffinity(GetCurrentThread(), &previous, nullptr) && readError == ERROR_SUCCESS) {
                    readError = GetLastError();
                }
            }

            const std::uint32_t ratio = static_cast<std::uint32_t>((perfStatus >> 8) & 0xffu);
            if (readError == ERROR_SUCCESS && ratio != 0) {
                std::printf(" CPU%0*zu=%lu MHz ", labelWidth, i,
                    static_cast<unsigned long>(ratio * busFrequencyMHz));
            } else {
                std::printf(" CPU%0*zu=N/A ", labelWidth, i);
            }
        }
        std::printf("\n");
        std::fflush(stdout);
        if (runOnce) break;
        if (WaitForSingleObject(g_stopEvent, SAMPLE_INTERVAL_MS) == WAIT_OBJECT_0) break;
    }

    SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    return 0;
}
