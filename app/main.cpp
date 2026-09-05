#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "../shared/protocol.h"

static_assert(sizeof(CPU_FREQ_ENTRY) == 24);
static_assert(CPU_FREQ_RESPONSE_HEADER_SIZE == 24);

namespace {
constexpr DWORD SAMPLE_INTERVAL_MS = 1000;
std::atomic_bool g_stopRequested{false};
HANDLE g_stopEvent = nullptr;

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
} // namespace

int main()
{
    const DWORD processorCount = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (processorCount == 0 || processorCount >
        (std::numeric_limits<DWORD>::max() - CPU_FREQ_RESPONSE_HEADER_SIZE) / sizeof(CPU_FREQ_ENTRY)) {
        PrintError("Cannot enumerate logical processors", GetLastError());
        return 1;
    }

    const std::size_t responseSize = CPU_FREQ_RESPONSE_HEADER_SIZE +
        static_cast<std::size_t>(processorCount) * sizeof(CPU_FREQ_ENTRY);
    std::vector<std::uint8_t> buffer(responseSize);
    HANDLE device = CreateFileW(L"\\\\.\\CpuFreqMonitor", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        PrintError("Cannot open CpuFreqMonitor driver (run install.cmd as administrator)", GetLastError());
        return 1;
    }

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent == nullptr || !SetConsoleCtrlHandler(ConsoleControlHandler, TRUE)) {
        PrintError("Cannot initialize Ctrl+C handling", GetLastError());
        if (g_stopEvent != nullptr) CloseHandle(g_stopEvent);
        CloseHandle(device);
        g_stopEvent = nullptr;
        return 1;
    }

    while (!g_stopRequested.load(std::memory_order_relaxed)) {
        DWORD bytesReturned = 0;
        const BOOL succeeded = DeviceIoControl(device, static_cast<DWORD>(CPU_FREQ_IOCTL_GET_FREQUENCIES),
            nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesReturned, nullptr);
        if (!succeeded) {
            PrintError("Driver query failed", GetLastError());
        } else if (bytesReturned < CPU_FREQ_RESPONSE_HEADER_SIZE) {
        PrintError("Driver returned malformed data", static_cast<DWORD>(ERROR_INVALID_DATA));
        } else {
            const auto* response = reinterpret_cast<const CPU_FREQ_RESPONSE*>(buffer.data());
            const std::size_t expectedSize = CPU_FREQ_RESPONSE_HEADER_SIZE +
                static_cast<std::size_t>(response->ProcessorCount) * sizeof(CPU_FREQ_ENTRY);
            if (response->Version != CPU_FREQ_PROTOCOL_VERSION || response->Size != bytesReturned ||
                expectedSize != bytesReturned) {
                PrintError("Driver protocol mismatch", static_cast<DWORD>(ERROR_REVISION_MISMATCH));
            } else {
                PrintTimestamp();
                const int labelWidth = response->ProcessorCount >= 100 ? 3 : 2;
                for (UINT32 i = 0; i < response->ProcessorCount; ++i) {
                    const CPU_FREQ_ENTRY& entry = response->Entries[i];
                    if (entry.Valid != 0) {
                        std::printf(" CPU%0*u=%lu MHz ", labelWidth, i,
                            static_cast<unsigned long>(entry.FrequencyMHz));
                    } else {
                        std::printf(" CPU%0*u=N/A ", labelWidth, i);
                    }
                }
                std::printf("\n");
                std::fflush(stdout);
            }
        }
        if (WaitForSingleObject(g_stopEvent, SAMPLE_INTERVAL_MS) == WAIT_OBJECT_0) break;
    }

    SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
    CloseHandle(g_stopEvent);
    CloseHandle(device);
    g_stopEvent = nullptr;
    return 0;
}
