#pragma once

#include "driver.h"

NTSTATUS
VhfActivate(
    _In_ PDEVICE_CONTEXT DeviceContext
);

NTSTATUS
VhfDeactivate(
    _In_ PDEVICE_CONTEXT DeviceContext
);

NTSTATUS
VhfSubmitInputReport(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_reads_bytes_(ReportSize) PUCHAR ReportData,
    _In_ ULONG ReportSize
);
