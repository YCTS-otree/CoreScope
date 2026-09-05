#include <ntddk.h>
#include <wdmsec.h>
#include <intrin.h>

#include "../shared/protocol.h"

#define IA32_PERF_STATUS 0x198u

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD CpuFreqUnload;
_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH CpuFreqCreateClose;
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH CpuFreqDeviceControl;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, CpuFreqCreateClose)
#pragma alloc_text(PAGE, CpuFreqDeviceControl)
#pragma alloc_text(PAGE, CpuFreqUnload)
#endif

static const GUID CPU_FREQ_DEVICE_CLASS_GUID = {
    0xa1e86b9d, 0x8e5d, 0x49d0, {0xa8, 0x42, 0xdd, 0x83, 0x0e, 0xd5, 0xe9, 0x92}};

static ULONG g_busFrequencyMHz;

static NTSTATUS CompleteIrp(_Inout_ PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

_Success_(return != FALSE)
static BOOLEAN IsSupportedIntelProcessor(_Out_ PULONG busFrequencyMHz)
{
    int registers[4] = {0};
    __cpuid(registers, 0);
    if (registers[0] < 0x16 || registers[1] != 0x756e6547 ||
        registers[3] != 0x49656e69 || registers[2] != 0x6c65746e) {
        return FALSE;
    }

    __cpuid(registers, 0x16);
    if (registers[2] <= 0 || registers[2] > 1000) {
        return FALSE;
    }

    *busFrequencyMHz = (ULONG)registers[2];
    return TRUE;
}

_Success_(return != FALSE)
static BOOLEAN ReadProcessorFrequency(
    USHORT group,
    ULONG processor,
    _Out_ PCPU_FREQ_ENTRY entry)
{
    GROUP_AFFINITY affinity = {0};
    GROUP_AFFINITY previousAffinity = {0};
    ULONG64 perfStatus = 0;
    BOOLEAN succeeded = TRUE;

    RtlZeroMemory(entry, sizeof(*entry));
    entry->Group = group;
    entry->Processor = processor;

    if (processor >= sizeof(KAFFINITY) * 8) {
        return FALSE;
    }

    affinity.Group = group;
    affinity.Mask = ((KAFFINITY)1) << processor;
    KeSetSystemGroupAffinityThread(&affinity, &previousAffinity);

    __try {
        perfStatus = __readmsr(IA32_PERF_STATUS);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        succeeded = FALSE;
    }

    KeRevertToUserGroupAffinityThread(&previousAffinity);

    entry->PerfStatus = perfStatus;
    entry->Ratio = (ULONG)((perfStatus >> 8) & 0xffu);
    entry->FrequencyMHz = entry->Ratio * g_busFrequencyMHz;
    entry->Valid = (UINT16)(succeeded && entry->Ratio != 0);
    return entry->Valid != 0;
}

C_ASSERT(sizeof(CPU_FREQ_ENTRY) == 24);
C_ASSERT(CPU_FREQ_RESPONSE_HEADER_SIZE == 24);

_Use_decl_annotations_
NTSTATUS CpuFreqCreateClose(PDEVICE_OBJECT deviceObject, PIRP irp)
{
    UNREFERENCED_PARAMETER(deviceObject);
    PAGED_CODE();
    return CompleteIrp(irp, STATUS_SUCCESS, 0);
}

_Use_decl_annotations_
NTSTATUS CpuFreqDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PCPU_FREQ_RESPONSE response;
    ULONG processorCount;
    ULONG requiredSize;
    ULONG outputLength;
    ULONG flatIndex = 0;
    USHORT groupCount;
    USHORT group;

    UNREFERENCED_PARAMETER(deviceObject);
    PAGED_CODE();

    stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->Parameters.DeviceIoControl.IoControlCode != CPU_FREQ_IOCTL_GET_FREQUENCIES) {
        return CompleteIrp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    processorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (processorCount == 0 ||
        processorCount > (MAXULONG - CPU_FREQ_RESPONSE_HEADER_SIZE) / sizeof(CPU_FREQ_ENTRY)) {
        return CompleteIrp(irp, STATUS_INVALID_DEVICE_STATE, 0);
    }

    requiredSize = CPU_FREQ_RESPONSE_HEADER_SIZE + processorCount * sizeof(CPU_FREQ_ENTRY);
    outputLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (outputLength < requiredSize || irp->AssociatedIrp.SystemBuffer == NULL) {
        return CompleteIrp(irp, STATUS_BUFFER_TOO_SMALL, requiredSize);
    }

    response = (PCPU_FREQ_RESPONSE)irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(response, requiredSize);
    response->Size = requiredSize;
    response->Version = CPU_FREQ_PROTOCOL_VERSION;
    response->ProcessorCount = processorCount;
    response->BusFrequencyMHz = g_busFrequencyMHz;

    groupCount = KeQueryActiveGroupCount();
    for (group = 0; group < groupCount; ++group) {
        ULONG index;
        ULONG count = KeQueryActiveProcessorCountEx(group);
        for (index = 0; index < count && flatIndex < processorCount; ++index, ++flatIndex) {
            if (!ReadProcessorFrequency(group, index, &response->Entries[flatIndex])) {
                ++response->FailedReads;
            }
        }
    }

    if (flatIndex != processorCount) {
        return CompleteIrp(irp, STATUS_INVALID_DEVICE_STATE, 0);
    }
    return CompleteIrp(irp, STATUS_SUCCESS, requiredSize);
}

_Use_decl_annotations_
VOID CpuFreqUnload(PDRIVER_OBJECT driverObject)
{
    UNICODE_STRING symbolicLink;
    PAGED_CODE();

    RtlInitUnicodeString(&symbolicLink, L"\\DosDevices\\CpuFreqMonitor");
    IoDeleteSymbolicLink(&symbolicLink);
    if (driverObject->DeviceObject != NULL) {
        IoDeleteDevice(driverObject->DeviceObject);
    }
}

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    UNICODE_STRING deviceName;
    UNICODE_STRING symbolicLink;
    UNICODE_STRING securityDescriptor;
    PDEVICE_OBJECT deviceObject = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(registryPath);

    if (!IsSupportedIntelProcessor(&g_busFrequencyMHz)) {
        return STATUS_NOT_SUPPORTED;
    }

    RtlInitUnicodeString(&deviceName, L"\\Device\\CpuFreqMonitor");
    RtlInitUnicodeString(&securityDescriptor,
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;BU)");

    status = IoCreateDeviceSecure(driverObject, 0, &deviceName,
        CPU_FREQ_DEVICE_TYPE, FILE_DEVICE_SECURE_OPEN, FALSE,
        &securityDescriptor, &CPU_FREQ_DEVICE_CLASS_GUID, &deviceObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    deviceObject->Flags |= DO_BUFFERED_IO;
    driverObject->MajorFunction[IRP_MJ_CREATE] = CpuFreqCreateClose;
    driverObject->MajorFunction[IRP_MJ_CLOSE] = CpuFreqCreateClose;
    driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = CpuFreqDeviceControl;
    driverObject->DriverUnload = CpuFreqUnload;

    RtlInitUnicodeString(&symbolicLink, L"\\DosDevices\\CpuFreqMonitor");
    status = IoCreateSymbolicLink(&symbolicLink, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
