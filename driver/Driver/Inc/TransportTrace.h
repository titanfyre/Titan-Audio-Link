// Copyright (c) 2026 TitanCSA. Private validation instrumentation; never PCM data.
#pragma once
#include <wdm.h>
#include <TraceLoggingProvider.h>
TRACELOGGING_DECLARE_PROVIDER(g_TitanAudioTrace);

struct TitanAudioStats {
    ULONG Adapter;
    ULONG Queued;
    ULONG Reserve;
    ULONGLONG Missing;
    ULONGLONG Dropped;
    ULONGLONG Written;
    ULONGLONG Read;
    ULONG MaximumWrite;
    ULONG MaximumRead;
};
