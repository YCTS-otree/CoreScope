// SPDX-License-Identifier: GPL-3.0-only
#include "pawnio_client.h"

#include <winternl.h>
#include <winioctl.h>

#include <array>
#include <cstring>
#include <limits>

namespace {
constexpr ULONG PAWNIO_DEVICE_TYPE = 41394;
constexpr DWORD IOCTL_PAWNIO_LOAD_BINARY =
    CTL_CODE(PAWNIO_DEVICE_TYPE, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr DWORD IOCTL_PAWNIO_EXECUTE_FUNCTION =
    CTL_CODE(PAWNIO_DEVICE_TYPE, 0x841, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr std::size_t FUNCTION_NAME_LENGTH = 32;

DWORD NtStatusToError(NTSTATUS status)
{
    return RtlNtStatusToDosError(status);
}
} // namespace

PawnIoClient::~PawnIoClient()
{
    if (handle_ != nullptr) CloseHandle(handle_);
    if (pciMutex_ != nullptr) CloseHandle(pciMutex_);
}

bool PawnIoClient::ReadFileBytes(const wchar_t* path, std::vector<std::uint8_t>& bytes, DWORD& error)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        error = GetLastError();
        CloseHandle(file);
        return false;
    }
    if (size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) > std::numeric_limits<DWORD>::max()) {
        error = ERROR_FILE_INVALID;
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD bytesRead = 0;
    const BOOL succeeded = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesRead, nullptr);
    error = succeeded && bytesRead == bytes.size() ? ERROR_SUCCESS : GetLastError();
    if (succeeded && bytesRead != bytes.size()) error = ERROR_HANDLE_EOF;
    CloseHandle(file);
    return error == ERROR_SUCCESS;
}

bool PawnIoClient::OpenAndLoad(const wchar_t* modulePath, DWORD& error)
{
    if (handle_ != nullptr) {
        error = ERROR_ALREADY_INITIALIZED;
        return false;
    }

    UNICODE_STRING deviceName{};
    RtlInitUnicodeString(&deviceName, L"\\Device\\PawnIO");
    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &deviceName, 0, nullptr, nullptr);
    IO_STATUS_BLOCK ioStatus{};
    const NTSTATUS status = NtOpenFile(&handle_, GENERIC_READ | GENERIC_WRITE, &attributes, &ioStatus,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (status < 0) {
        handle_ = nullptr;
        error = NtStatusToError(status);
        return false;
    }

    std::vector<std::uint8_t> module;
    if (!ReadFileBytes(modulePath, module, error)) return false;

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(handle_, IOCTL_PAWNIO_LOAD_BINARY, module.data(),
        static_cast<DWORD>(module.size()), nullptr, 0, &bytesReturned, nullptr)) {
        error = GetLastError();
        return false;
    }

    error = ERROR_SUCCESS;
    return true;
}

bool PawnIoClient::Execute(const char* functionName, const std::uint64_t* input, std::size_t inputCount,
    std::uint64_t* output, std::size_t outputCount, DWORD& error) const
{
    if (handle_ == nullptr || functionName == nullptr || strlen(functionName) >= FUNCTION_NAME_LENGTH ||
        inputCount > (std::numeric_limits<DWORD>::max() - FUNCTION_NAME_LENGTH) / sizeof(std::uint64_t) ||
        outputCount > std::numeric_limits<DWORD>::max() / sizeof(std::uint64_t)) {
        error = ERROR_INVALID_PARAMETER;
        return false;
    }

    std::array<std::uint8_t, FUNCTION_NAME_LENGTH + 8 * sizeof(std::uint64_t)> request{};
    if(inputCount>8) {error=ERROR_INVALID_PARAMETER;return false;}
    ++readCount_;
    strcpy_s(reinterpret_cast<char*>(request.data()), FUNCTION_NAME_LENGTH, functionName);
    if (inputCount != 0) {
        memcpy(request.data() + FUNCTION_NAME_LENGTH, input, inputCount * sizeof(std::uint64_t));
    }

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(handle_, IOCTL_PAWNIO_EXECUTE_FUNCTION, request.data(),
        static_cast<DWORD>(FUNCTION_NAME_LENGTH + inputCount * sizeof(std::uint64_t)), output, static_cast<DWORD>(outputCount * sizeof(std::uint64_t)),
        &bytesReturned, nullptr)) {
        error = GetLastError();
        return false;
    }
    if (bytesReturned != outputCount * sizeof(std::uint64_t)) {
        error = ERROR_INVALID_DATA;
        return false;
    }

    error = ERROR_SUCCESS;
    return true;
}

bool PawnIoClient::ReadMsr(std::uint32_t index, std::uint64_t& value, DWORD& error) const
{
    const std::uint64_t input = index;
    value = 0;
    return Execute("ioctl_read_msr", &input, 1, &value, 1, error);
}

bool PawnIoClient::ReadMmioQword(std::uint32_t offset, std::uint64_t& value, DWORD& error) const { const std::uint64_t input=offset;value=0;return Execute("ioctl_read_qword",&input,1,&value,1,error); }

bool PawnIoClient::ReadSmn(std::uint32_t offset, std::uint32_t& value, DWORD& error) const {
    value=0;
    // Official module requires the shared Access_PCI mutant around the index/data pair.
    if(!pciMutex_){pciMutex_=CreateMutexW(nullptr,FALSE,L"Global\\Access_PCI");
        if(!pciMutex_ && GetLastError()==ERROR_ACCESS_DENIED)pciMutex_=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,L"Global\\Access_PCI");
        if(!pciMutex_){error=GetLastError();return false;}}
    const auto wait=WaitForSingleObject(pciMutex_,10);
    if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED){error=wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();return false;}
    const std::uint64_t input=offset;std::uint64_t output=0;
    const bool success=Execute("ioctl_read_smn",&input,1,&output,1,error);
    ReleaseMutex(pciMutex_);
    if(success)value=static_cast<std::uint32_t>(output);
    return success;
}
