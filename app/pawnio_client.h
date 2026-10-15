// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class PawnIoClient final {
public:
    PawnIoClient() = default;
    ~PawnIoClient();

    PawnIoClient(const PawnIoClient&) = delete;
    PawnIoClient& operator=(const PawnIoClient&) = delete;

    bool OpenAndLoad(const wchar_t* modulePath, DWORD& error);
    bool ReadMsr(std::uint32_t index, std::uint64_t& value, DWORD& error) const;
    bool ReadSmn(std::uint32_t offset, std::uint32_t& value, DWORD& error) const;

    bool ReadMmioQword(std::uint32_t offset, std::uint64_t& value, DWORD& error) const;
    std::uint64_t ReadCount() const { return readCount_; }
private:
    mutable std::uint64_t readCount_=0;
    static bool ReadFileBytes(const wchar_t* path, std::vector<std::uint8_t>& bytes, DWORD& error);
    bool Execute(const char* functionName, const std::uint64_t* input, std::size_t inputCount,
        std::uint64_t* output, std::size_t outputCount, DWORD& error) const;

    HANDLE handle_{nullptr};
    mutable HANDLE pciMutex_{nullptr};
};
