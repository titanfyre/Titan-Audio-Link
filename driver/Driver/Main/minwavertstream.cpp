#include "definitions.h"
#include <limits.h>
#include <ks.h>
#include "endpoints.h"
#include "minwavert.h"
#include "minwavertstream.h"
#include "TransportTrace.h"
#include "PacketDeadline.h"
#define MINWAVERTSTREAM_POOLTAG 'SRWM'

#pragma warning (disable : 4127)

namespace {
// Control paths only. Timer and GetPosition never take this sleeping lock.
class StreamManagementGuard {
    KMUTEX* mutex_;
public:
    explicit StreamManagementGuard(KMUTEX* mutex) : mutex_(mutex) {
        NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
        // Infinite non-alertable KernelMode wait on a mutex completes successfully.
        const NTSTATUS status = KeWaitForSingleObject(mutex_, Executive, KernelMode, FALSE, NULL);
        NT_ASSERT(status == STATUS_SUCCESS);
        UNREFERENCED_PARAMETER(status);
    }
    ~StreamManagementGuard() { KeReleaseMutex(mutex_, FALSE); }
    StreamManagementGuard(const StreamManagementGuard&) = delete;
    StreamManagementGuard& operator=(const StreamManagementGuard&) = delete;
};
}

//=============================================================================
// CMiniportWaveRTStream
//=============================================================================

//=============================================================================
#pragma code_seg("PAGE")
CMiniportWaveRTStream::~CMiniportWaveRTStream
( 
    void 
)
/*++

Routine Description:

  Destructor for wavertstream 

Arguments:

Return Value:

  NT status code.

--*/
{
    PAGED_CODE();
    // ExDeleteTimer with Wait=TRUE drains callbacks before any referenced state is freed.
    if (m_pNotificationTimer) {
        ExDeleteTimer(m_pNotificationTimer, TRUE, TRUE, NULL);
        m_pNotificationTimer = NULL;
    }
    if (m_NotificationList.Flink) while (!IsListEmpty(&m_NotificationList)) {
        PLIST_ENTRY entry = RemoveHeadList(&m_NotificationList);
        ExFreePoolWithTag(CONTAINING_RECORD(entry, NotificationListEntry, ListEntry), MINWAVERTSTREAM_POOLTAG);
    }
    if (m_pMiniport && m_bUnregisterStream) {
        m_pMiniport->GetAdapterCommObj()->CableState(m_bCapture, FALSE);
        m_pMiniport->GetAdapterCommObj()->CableConfigure(m_bCapture, 0);
    }
    if (NULL != m_pMiniport)
    {
    
        if (m_bUnregisterStream)
        {
            m_pMiniport->StreamClosed(m_ulPin, this);
            m_bUnregisterStream = FALSE;
        }
        
        m_pMiniport->Release();
        m_pMiniport = NULL;
    }




    if (m_pWfExt)
    {
        ExFreePoolWithTag( m_pWfExt, MINWAVERTSTREAM_POOLTAG );
        m_pWfExt = NULL;
    }
    DPF_ENTER(("[CMiniportWaveRTStream::~CMiniportWaveRTStream]"));
} // ~CMiniportWaveRTStream

//=============================================================================
#pragma code_seg("PAGE")

NTSTATUS
CMiniportWaveRTStream::Init
( 
    _In_ PCMiniportWaveRT           Miniport_,
    _In_ PPORTWAVERTSTREAM          PortStream_,
    _In_ ULONG                      Pin_,
    _In_ BOOLEAN                    Capture_,
    _In_ PKSDATAFORMAT              DataFormat_,
    _In_ GUID                       SignalProcessingMode
)
/*++

Routine Description:

  Initializes the stream object.

Arguments:

  Miniport_ -

  Pin_ -

  DataFormat -

  SignalProcessingMode - The driver uses the signalProcessingMode to configure
    driver and/or hardware specific signal processing to be applied to this new
    stream.

Return Value:

  NT status code.

--*/
{
    PAGED_CODE();

    PWAVEFORMATEX pWfEx = NULL;
    NTSTATUS ntStatus = STATUS_SUCCESS;

    m_pMiniport = NULL;
    m_ulPin = 0;
    m_bUnregisterStream = FALSE;
    m_bCapture = FALSE;
    m_ulDmaBufferSize = 0;
    m_pDmaBuffer = NULL;
    m_ulNotificationsPerBuffer = 0;
    m_KsState = KSSTATE_STOP;
    m_ullPlayPosition = 0;
    m_ullWritePosition = 0;
    m_ullDmaTimeStamp = 0;
    m_Clock.Reset();
    m_CapturePackets.Configure(0);
    m_NotifiedPacket = 0;
    m_DiagnosticDmaLostBytes = m_DiagnosticSkippedPackets = m_DiagnosticReadPackets = 0;
    m_DiagnosticLastPacket = ~ULONGLONG(0);
    m_DiagnosticEventSignals = m_DiagnosticMaximumAdvance = 0;
    m_DiagnosticRun = 0;
    m_ulDmaMovementRate = 0;
    m_pWfExt = NULL;
    m_ullLinearPosition = 0;
    m_SignalProcessingMode = SignalProcessingMode;


    m_pPortStream = PortStream_;
    InitializeListHead(&m_NotificationList);
    m_ulNotificationIntervalMs = 0;

    // Initialize the spinlock to synchronize position updates
    KeInitializeSpinLock(&m_PositionSpinLock);
    KeInitializeMutex(&m_ManagementLock, 0);
    (void)KeQueryPerformanceCounter(&m_ullPerformanceCounterFrequency);
    if (m_ullPerformanceCounterFrequency.QuadPart <= 0 ||
        static_cast<ULONGLONG>(m_ullPerformanceCounterFrequency.QuadPart) > TitanAudio::CapturePackets::MaximumQpcFrequency)
        return STATUS_NOT_SUPPORTED;

    m_pNotificationTimer = ExAllocateTimer(
         TimerNotifyRT,
         this,
         EX_TIMER_HIGH_RESOLUTION
    );
    if (!m_pNotificationTimer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pWfEx = GetWaveFormatEx(DataFormat_);
    if (NULL == pWfEx) 
    { 
        return STATUS_UNSUCCESSFUL; 
    }

    if (pWfEx->nChannels != 2 || pWfEx->nSamplesPerSec != 48000 ||
        pWfEx->wBitsPerSample != 16 || pWfEx->nBlockAlign != 4 ||
        pWfEx->nAvgBytesPerSec != 192000 ||
        (pWfEx->wFormatTag != WAVE_FORMAT_PCM && pWfEx->wFormatTag != WAVE_FORMAT_EXTENSIBLE) ||
        (pWfEx->wFormatTag == WAVE_FORMAT_PCM && pWfEx->cbSize != 0) ||
        (pWfEx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pWfEx->cbSize != 22))
        return STATUS_NO_MATCH;

    m_pMiniport = reinterpret_cast<CMiniportWaveRT*>(Miniport_);
    if (m_pMiniport == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    m_pMiniport->AddRef();
    if (!NT_SUCCESS(ntStatus))
    {
        return ntStatus;
    }
    m_ulPin = Pin_;
    m_bCapture = Capture_;
    m_ulDmaMovementRate = pWfEx->nAvgBytesPerSec;

    m_pWfExt = (PWAVEFORMATEXTENSIBLE)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(WAVEFORMATEX) + pWfEx->cbSize, MINWAVERTSTREAM_POOLTAG);
    if (m_pWfExt == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(m_pWfExt, pWfEx, sizeof(WAVEFORMATEX) + pWfEx->cbSize);




    //
    // Register this stream.
    //
    ntStatus = m_pMiniport->StreamCreated(m_ulPin, this);
    if (NT_SUCCESS(ntStatus))
    {
        m_bUnregisterStream = TRUE;
    }

    return ntStatus;
} // Init

//=============================================================================
#pragma code_seg("PAGE")
STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStream::NonDelegatingQueryInterface
( 
    _In_ REFIID  Interface,
    _COM_Outptr_ PVOID * Object 
)
/*++

Routine Description:

  QueryInterface

Arguments:

  Interface - GUID

  Object - interface pointer to be returned

Return Value:

  NT status code.

--*/
{
    PAGED_CODE();

    ASSERT(Object);

    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
    {
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERTSTREAM(this)));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStream))
    {
        *Object = PVOID(PMINIPORTWAVERTSTREAM(this));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStreamNotification))
    {
        *Object = PVOID(PMINIPORTWAVERTSTREAMNOTIFICATION(this));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTInputStream) && m_bCapture)
    {
        *Object = PVOID(PMINIPORTWAVERTINPUTSTREAM(this));
    }
    else
    {
        *Object = NULL;
    }

    if (*Object)
    {
        PUNKNOWN(*Object)->AddRef();
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
} // NonDelegatingQueryInterface

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CMiniportWaveRTStream::AllocateBufferWithNotification
(
    _In_    ULONG               NotificationCount_,
    _In_    ULONG               RequestedSize_,
    _Out_   PMDL                *AudioBufferMdl_,
    _Out_   ULONG               *ActualSize_,
    _Out_   ULONG               *OffsetFromFirstPage_,
    _Out_   MEMORY_CACHING_TYPE *CacheType_
)
{
    PAGED_CODE();
    StreamManagementGuard guard(&m_ManagementLock);
    if (m_pDmaBuffer || m_ulDmaBufferSize) return STATUS_INVALID_DEVICE_STATE;

    if ( (RequestedSize_ > 192000) || (RequestedSize_ < m_pWfExt->Format.nBlockAlign) )
    { 
        return STATUS_UNSUCCESSFUL; 
    }
    
    if (m_bCapture && NotificationCount_ != 2) return STATUS_NOT_SUPPORTED;
    if (!TitanAudio::CapturePackets::SupportsLayout(m_bCapture != FALSE, NotificationCount_, RequestedSize_))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RequestedSize_ -= RequestedSize_ % (m_pWfExt->Format.nBlockAlign);
    
    PHYSICAL_ADDRESS highAddress;
    highAddress.HighPart = 0;
    highAddress.LowPart = MAXULONG;

    PMDL pBufferMdl = m_pPortStream->AllocatePagesForMdl (highAddress, RequestedSize_);

    if (NULL == pBufferMdl)
    {
        return STATUS_UNSUCCESSFUL;
    }

    // From MSDN: 
    // "Since the Windows audio stack does not support a mechanism to express memory access 
    //  alignment requirements for buffers, audio drivers must select a caching type for mapped
    //  memory buffers that does not impose platform-specific alignment requirements. In other 
    //  words, the caching type used by the audio driver for mapped memory buffers, must not make 
    //  assumptions about the memory alignment requirements for any specific platform.
    //
    //  This method maps the physical memory pages in the MDL into kernel-mode virtual memory. 
    //  Typically, the miniport driver calls this method if it requires software access to the 
    //  scatter-gather list for an audio buffer. In this case, the storage for the scatter-gather 
    //  list must have been allocated by the IPortWaveRTStream::AllocatePagesForMdl or 
    //  IPortWaveRTStream::AllocateContiguousPagesForMdl method. 
    //
    //  A WaveRT miniport driver should not require software access to the audio buffer itself."
    //   
    BYTE* buffer = (BYTE*)m_pPortStream->MapAllocatedPages(pBufferMdl, MmCached);
    if (!buffer) { m_pPortStream->FreePagesFromMdl(pBufferMdl); return STATUS_INSUFFICIENT_RESOURCES; }
    RtlZeroMemory(buffer, RequestedSize_);
    PublishBuffer(buffer, RequestedSize_, NotificationCount_);

    *AudioBufferMdl_ = pBufferMdl;
    *ActualSize_ = RequestedSize_;
    *OffsetFromFirstPage_ = 0;
    *CacheType_ = MmCached;

    return STATUS_SUCCESS;
}

//=============================================================================
#pragma code_seg("PAGE")
VOID CMiniportWaveRTStream::FreeBufferWithNotification
(
    _In_        PMDL    Mdl_,
    _In_        ULONG   Size_
)
{
    UNREFERENCED_PARAMETER(Size_);

    PAGED_CODE();
    StreamManagementGuard guard(&m_ManagementLock);
    (void)SetStateLocked(KSSTATE_STOP);
    if (m_pNotificationTimer) { ExDeleteTimer(m_pNotificationTimer, TRUE, TRUE, NULL); m_pNotificationTimer = NULL; }


    if (Mdl_ != NULL)
    {
        if (m_pDmaBuffer != NULL)
        {
            m_pPortStream->UnmapAllocatedPages(m_pDmaBuffer, Mdl_);
            m_pDmaBuffer = NULL;
        }
        
        m_pPortStream->FreePagesFromMdl(Mdl_);
    }
    
    m_ulDmaBufferSize = 0;
    m_ulNotificationsPerBuffer = 0;
    m_CapturePackets.Configure(0);
    m_pMiniport->GetAdapterCommObj()->CableConfigure(m_bCapture, 0);

    return;
}

//=============================================================================
#pragma code_seg("PAGE")
#pragma code_seg()
NTSTATUS CMiniportWaveRTStream::RegisterNotificationEvent(_In_ PKEVENT NotificationEvent_)
{
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!NotificationEvent_) return STATUS_INVALID_PARAMETER;
    auto entry = static_cast<NotificationListEntry*>(ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NotificationListEntry), MINWAVERTSTREAM_POOLTAG));
    if (!entry) return STATUS_INSUFFICIENT_RESOURCES;
    entry->NotificationEvent = NotificationEvent_;
    NTSTATUS status = STATUS_SUCCESS; ULONG count = 0; KIRQL irql;
    KeAcquireSpinLock(&m_PositionSpinLock, &irql);
    for (PLIST_ENTRY it = m_NotificationList.Flink; it != &m_NotificationList; it = it->Flink) {
        ++count;
        if (CONTAINING_RECORD(it, NotificationListEntry, ListEntry)->NotificationEvent == NotificationEvent_)
            status = STATUS_OBJECT_NAME_COLLISION;
    }
    if (count >= 4) status = STATUS_INSUFFICIENT_RESOURCES;
    if (NT_SUCCESS(status)) InsertTailList(&m_NotificationList, &entry->ListEntry);
    KeReleaseSpinLock(&m_PositionSpinLock, irql);
    if (!NT_SUCCESS(status)) ExFreePoolWithTag(entry, MINWAVERTSTREAM_POOLTAG);
    return status;
}

//=============================================================================
#pragma code_seg("PAGE")
#pragma code_seg()
NTSTATUS CMiniportWaveRTStream::UnregisterNotificationEvent(_In_ PKEVENT NotificationEvent_)
{
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL); NotificationListEntry* found = NULL; KIRQL irql;
    KeAcquireSpinLock(&m_PositionSpinLock, &irql);
    for (PLIST_ENTRY it = m_NotificationList.Flink; it != &m_NotificationList; it = it->Flink) {
        auto entry = CONTAINING_RECORD(it, NotificationListEntry, ListEntry);
        if (entry->NotificationEvent == NotificationEvent_) { found = entry; RemoveEntryList(it); break; }
    }
    KeReleaseSpinLock(&m_PositionSpinLock, irql);
    const NTSTATUS status = found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
    if (found) ExFreePoolWithTag(found, MINWAVERTSTREAM_POOLTAG);
    return status;
}

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CMiniportWaveRTStream::GetClockRegister
(
    _Out_ PKSRTAUDIO_HWREGISTER Register_
)
{
    UNREFERENCED_PARAMETER(Register_);

    PAGED_CODE();

    return STATUS_NOT_IMPLEMENTED;
}

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CMiniportWaveRTStream::GetPositionRegister
(
    _Out_ PKSRTAUDIO_HWREGISTER Register_
)
{
    UNREFERENCED_PARAMETER(Register_);

    PAGED_CODE();

    return STATUS_NOT_IMPLEMENTED;
}

//=============================================================================
#pragma code_seg("PAGE")
VOID CMiniportWaveRTStream::GetHWLatency
(
    _Out_ PKSRTAUDIO_HWLATENCY  Latency_
)
{
    PAGED_CODE();

    ASSERT(Latency_);

    Latency_->ChipsetDelay = 0;
    Latency_->CodecDelay = 0;
    Latency_->FifoSize = 0;
}

//=============================================================================
#pragma code_seg("PAGE")
VOID CMiniportWaveRTStream::FreeAudioBuffer
(
_In_opt_    PMDL        Mdl_,
_In_        ULONG       Size_
)
{
    UNREFERENCED_PARAMETER(Size_);

    PAGED_CODE();
    StreamManagementGuard guard(&m_ManagementLock);
    (void)SetStateLocked(KSSTATE_STOP);
    if (m_pNotificationTimer) { ExDeleteTimer(m_pNotificationTimer, TRUE, TRUE, NULL); m_pNotificationTimer = NULL; }


    if (Mdl_ != NULL)
    {
        if (m_pDmaBuffer != NULL)
        {
            m_pPortStream->UnmapAllocatedPages(m_pDmaBuffer, Mdl_);
            m_pDmaBuffer = NULL;
        }

        m_pPortStream->FreePagesFromMdl(Mdl_);
    }

    m_ulDmaBufferSize = 0;
    m_ulNotificationsPerBuffer = 0;
    m_pMiniport->GetAdapterCommObj()->CableConfigure(m_bCapture, 0);
}

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CMiniportWaveRTStream::AllocateAudioBuffer
(
_In_    ULONG                   RequestedSize_,
_Out_   PMDL                   *AudioBufferMdl_,
_Out_   ULONG                  *ActualSize_,
_Out_   ULONG                  *OffsetFromFirstPage_,
_Out_   MEMORY_CACHING_TYPE    *CacheType_
)
{
    PAGED_CODE();
    StreamManagementGuard guard(&m_ManagementLock);
    // This preview guarantees capture metadata only for two-notification streams.
    if (m_bCapture) return STATUS_NOT_SUPPORTED;
    if (m_pDmaBuffer || m_ulDmaBufferSize) return STATUS_INVALID_DEVICE_STATE;

    if (!TitanAudio::CapturePackets::SupportsLayout(false, 1, RequestedSize_))
    {
        return STATUS_UNSUCCESSFUL;
    }

    RequestedSize_ -= RequestedSize_ % (m_pWfExt->Format.nBlockAlign);

    PHYSICAL_ADDRESS highAddress;
    highAddress.HighPart = 0;
    highAddress.LowPart = MAXULONG;

    PMDL pBufferMdl = m_pPortStream->AllocatePagesForMdl(highAddress, RequestedSize_);

    if (NULL == pBufferMdl)
    {
        return STATUS_UNSUCCESSFUL;
    }

    // From MSDN: 
    // "Since the Windows audio stack does not support a mechanism to express memory access 
    //  alignment requirements for buffers, audio drivers must select a caching type for mapped
    //  memory buffers that does not impose platform-specific alignment requirements. In other 
    //  words, the caching type used by the audio driver for mapped memory buffers, must not make 
    //  assumptions about the memory alignment requirements for any specific platform.
    //
    //  This method maps the physical memory pages in the MDL into kernel-mode virtual memory. 
    //  Typically, the miniport driver calls this method if it requires software access to the 
    //  scatter-gather list for an audio buffer. In this case, the storage for the scatter-gather 
    //  list must have been allocated by the IPortWaveRTStream::AllocatePagesForMdl or 
    //  IPortWaveRTStream::AllocateContiguousPagesForMdl method. 
    //
    //  A WaveRT miniport driver should not require software access to the audio buffer itself."
    //   
    BYTE* buffer = (BYTE*)m_pPortStream->MapAllocatedPages(pBufferMdl, MmCached);
    if (!buffer) { m_pPortStream->FreePagesFromMdl(pBufferMdl); return STATUS_INSUFFICIENT_RESOURCES; }
    RtlZeroMemory(buffer, RequestedSize_);
    PublishBuffer(buffer, RequestedSize_, 0);

    *AudioBufferMdl_ = pBufferMdl;
    *ActualSize_ = RequestedSize_;
    *OffsetFromFirstPage_ = 0;
    *CacheType_ = MmCached;

    return STATUS_SUCCESS;
}

//=============================================================================
#pragma code_seg()
NTSTATUS CMiniportWaveRTStream::GetPosition
(
    _Out_   KSAUDIO_POSITION    *Position_
)
{
    NTSTATUS ntStatus;

    KIRQL oldIrql;
    KeAcquireSpinLock(&m_PositionSpinLock, &oldIrql);

    if (m_KsState == KSSTATE_RUN)
    {
        //
        // Get the current time and update position.
        //
        LARGE_INTEGER ilQPC = KeQueryPerformanceCounter(NULL);
        UpdatePosition(ilQPC);
    }

    Position_->PlayOffset = m_ullPlayPosition;
    Position_->WriteOffset = m_ullWritePosition;

    KeReleaseSpinLock(&m_PositionSpinLock, oldIrql);

    ntStatus = STATUS_SUCCESS;
    
    return ntStatus;
}

#pragma code_seg()
// Complete captured packets provide an unambiguous stream index and first-sample
// raw QPC; the OS does not need to guess wraps from a sampled cyclic byte cursor.
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS CMiniportWaveRTStream::GetReadPacket(_Out_ ULONG* packet, _Out_ DWORD* flags,
    _Out_ ULONG64* firstSampleQpc, _Out_ BOOL* moreData)
{
    KIRQL irql; KeAcquireSpinLock(&m_PositionSpinLock, &irql);
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    *packet = 0; *flags = 0; *firstSampleQpc = 0; *moreData = FALSE;
    if (m_KsState < KSSTATE_PAUSE) status = STATUS_INVALID_DEVICE_STATE;
    else if (!m_bCapture || !m_ulNotificationsPerBuffer) status = STATUS_NOT_SUPPORTED;
    else if (!m_pDmaBuffer || !m_ulDmaBufferSize) status = STATUS_INVALID_DEVICE_STATE;
    else {
        if (m_KsState == KSSTATE_RUN) UpdatePosition(KeQueryPerformanceCounter(NULL));
        uint64_t number = 0, timestamp = 0;
        if (m_CapturePackets.Read(number, timestamp)) {
            m_DiagnosticSkippedPackets += (m_DiagnosticLastPacket == ~ULONGLONG(0)) ? number : number - m_DiagnosticLastPacket - 1;
            m_DiagnosticLastPacket = number; ++m_DiagnosticReadPackets;
            *packet = static_cast<ULONG>(number); *firstSampleQpc = timestamp; status = STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&m_PositionSpinLock, irql);
    return status;
}

NTSTATUS CMiniportWaveRTStream::SetState(_In_ KSSTATE state)
{
    StreamManagementGuard guard(&m_ManagementLock);
    return SetStateLocked(state);
}
NTSTATUS CMiniportWaveRTStream::SetStateLocked(_In_ KSSTATE State_)
{
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (State_ < KSSTATE_STOP || State_ > KSSTATE_RUN) return STATUS_INVALID_PARAMETER;
    if (State_ == m_KsState) return STATUS_SUCCESS;
    if (State_ == KSSTATE_RUN && (!m_pDmaBuffer || !m_ulDmaBufferSize)) return STATUS_INVALID_DEVICE_STATE;
    if (State_ != KSSTATE_RUN && m_pNotificationTimer) {
        // Drain only this stream timer; never flush every processor's unrelated DPCs.
        ExDeleteTimer(m_pNotificationTimer, TRUE, TRUE, NULL);
        m_pNotificationTimer = NULL;
    }
    if (State_ == KSSTATE_RUN && m_ulNotificationIntervalMs && !m_pNotificationTimer) {
        m_pNotificationTimer = ExAllocateTimer(TimerNotifyRT, this, EX_TIMER_HIGH_RESOLUTION);
        if (!m_pNotificationTimer) return STATUS_INSUFFICIENT_RESOURCES;
    }
    KIRQL irql; KeAcquireSpinLock(&m_PositionSpinLock, &irql);
    LARGE_INTEGER qpc = KeQueryPerformanceCounter(&m_ullPerformanceCounterFrequency);
    const bool leavingRun = m_KsState == KSSTATE_RUN;
    if (leavingRun) UpdatePosition(qpc);
    TitanAudioStats stats{};
    ULONGLONG linear = m_ullLinearPosition;
    const ULONGLONG maximumAdvance = m_DiagnosticMaximumAdvance, dmaLost = m_DiagnosticDmaLostBytes;
    const ULONGLONG readPackets = m_DiagnosticReadPackets, skippedPackets = m_DiagnosticSkippedPackets;
    const ULONGLONG eventSignals = m_DiagnosticEventSignals;
    if (leavingRun) m_pMiniport->GetAdapterCommObj()->CableStats(&stats);
    if (State_ == KSSTATE_STOP) {
        m_ullPlayPosition = m_ullWritePosition = 0;
        m_ullLinearPosition = 0;
        m_Clock.Reset(); m_NotifiedPacket = 0;
        m_CapturePackets.Reset();
        m_DiagnosticLastPacket = ~ULONGLONG(0);
        }
    m_ullDmaTimeStamp = KSCONVERT_PERFORMANCE_TIME(m_ullPerformanceCounterFrequency.QuadPart, qpc);
    if (State_ == KSSTATE_RUN && m_bCapture)
        m_CapturePackets.Start(static_cast<ULONGLONG>(qpc.QuadPart), m_ullLinearPosition, m_Clock.FractionalRemainder());
    m_pMiniport->GetAdapterCommObj()->CableState(m_bCapture, State_ == KSSTATE_RUN);
    m_KsState = State_;
    if (State_ == KSSTATE_RUN) ++m_DiagnosticRun;
    if (State_ == KSSTATE_RUN && m_pNotificationTimer)
        ArmNotificationTimer(m_pNotificationTimer);
    KeReleaseSpinLock(&m_PositionSpinLock, irql);
    if (leavingRun) {
        // The PCM path increments bounded counters only. ETW emits once on RUN exit,
        // at PASSIVE_LEVEL and outside every spinlock. No sample bytes or pointers.
        TraceLoggingWrite(g_TitanAudioTrace, "RunExit",
            TraceLoggingLevel(4), TraceLoggingUInt32(stats.Adapter, "Adapter"),
            TraceLoggingBool(m_bCapture != FALSE, "Capture"), TraceLoggingUInt32(m_DiagnosticRun, "Run"),
            TraceLoggingUInt32(static_cast<ULONG>(State_), "NextState"),
            TraceLoggingUInt64(linear, "LinearBytes"), TraceLoggingUInt32(m_ulDmaBufferSize, "DmaBytes"),
            TraceLoggingUInt32(m_ulNotificationsPerBuffer, "NotificationsPerBuffer"),
            TraceLoggingUInt32(m_ulNotificationIntervalMs, "PeriodMs"),
            TraceLoggingUInt64(maximumAdvance, "MaximumAdvanceBytes"),
            TraceLoggingUInt64(dmaLost, "DmaLostBytes"),
            TraceLoggingUInt64(readPackets, "PacketsRead"),
            TraceLoggingUInt64(skippedPackets, "PacketsSkipped"),
            TraceLoggingUInt64(eventSignals, "EventSignals"),
            TraceLoggingUInt32(stats.Queued, "RingQueuedBytes"),
            TraceLoggingUInt32(stats.Reserve, "RingReserveBytes"),
            TraceLoggingUInt64(stats.Missing, "RingMissingBytes"), TraceLoggingUInt64(stats.Dropped, "RingDroppedBytes"),
            TraceLoggingUInt64(stats.Written, "RingRequestedWriteBytes"), TraceLoggingUInt64(stats.Read, "RingRequestedReadBytes"),
            TraceLoggingUInt32(stats.MaximumWrite, "MaximumRingWriteBytes"), TraceLoggingUInt32(stats.MaximumRead, "MaximumRingReadBytes"));
    }
    return STATUS_SUCCESS;
}
//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CMiniportWaveRTStream::SetFormat
(
    _In_    KSDATAFORMAT    *DataFormat_
)
{
    UNREFERENCED_PARAMETER(DataFormat_);

    PAGED_CODE();


    return STATUS_NOT_SUPPORTED;
}

#pragma code_seg()

//=============================================================================
#pragma code_seg()
VOID CMiniportWaveRTStream::PublishBuffer(_In_ BYTE* buffer, _In_ ULONG bytes, _In_ ULONG notifications)
{
    // Allocation/mapping/zeroing stay on the passive caller. This helper's
    // executable code is nonpaged while it atomically publishes the layout.
    // PAUSE packet queries can therefore never race metadata initialization.
    KIRQL irql; KeAcquireSpinLock(&m_PositionSpinLock, &irql);
    m_pDmaBuffer = buffer;
    m_ulDmaBufferSize = bytes;
    m_ulNotificationsPerBuffer = notifications;
    m_CapturePackets.Configure(notifications ? bytes / notifications : 0);
    m_ulNotificationIntervalMs = notifications ? (bytes * 1000 / m_ulDmaMovementRate) / notifications : 0;
    m_pMiniport->GetAdapterCommObj()->CableConfigure(m_bCapture, notifications ? bytes / notifications : bytes);
    KeReleaseSpinLock(&m_PositionSpinLock, irql);
}

VOID CMiniportWaveRTStream::ArmNotificationTimer(_In_ PEX_TIMER timer)
{
    // Caller holds the position lock; every deadline follows the PCM clock,
    // not the arrival of the previous callback. Period remains zero.
    if (!m_ulNotificationsPerBuffer || !m_ulDmaBufferSize) return;
    const LARGE_INTEGER qpc = KeQueryPerformanceCounter(NULL);
    const ULONGLONG now = KSCONVERT_PERFORMANCE_TIME(m_ullPerformanceCounterFrequency.QuadPart, qpc);
    const ULONGLONG processing = now >= m_ullDmaTimeStamp ? now - m_ullDmaTimeStamp : 0;
    const ULONGLONG delay = TitanAudio::PacketDeadline::Delay100ns(m_ullLinearPosition,
        m_Clock.FractionalRemainder(), m_ulDmaBufferSize / m_ulNotificationsPerBuffer, processing);
    if (delay) (void)ExSetTimer(timer, -static_cast<LONGLONG>(delay), 0, NULL);
}

VOID CMiniportWaveRTStream::UpdatePosition(_In_ LARGE_INTEGER qpc)
{
    if (!m_pDmaBuffer || !m_ulDmaBufferSize) return;
    const ULONGLONG now = KSCONVERT_PERFORMANCE_TIME(m_ullPerformanceCounterFrequency.QuadPart, qpc);
    if (now < m_ullDmaTimeStamp) { m_ullDmaTimeStamp = now; m_Clock.Reset(); return; }
    const ULONGLONG bytes = m_Clock.Advance(now - m_ullDmaTimeStamp);
    m_DiagnosticMaximumAdvance = max(m_DiagnosticMaximumAdvance, bytes);
    if (bytes > m_ulDmaBufferSize) m_DiagnosticDmaLostBytes += bytes - m_ulDmaBufferSize;
    if (m_bCapture) WriteBytes(bytes);
    else ReadBytes(bytes);
    m_ullLinearPosition += bytes;
    if (m_bCapture && m_ulNotificationsPerBuffer)
        (void)m_CapturePackets.Advance(m_ullLinearPosition - bytes, m_ullLinearPosition,
            static_cast<ULONGLONG>(qpc.QuadPart), static_cast<ULONGLONG>(m_ullPerformanceCounterFrequency.QuadPart),
            m_Clock.FractionalRemainder());
    m_ullPlayPosition = m_ullWritePosition = m_ullLinearPosition % m_ulDmaBufferSize;
    m_ullDmaTimeStamp = now;
}

//=============================================================================
#pragma code_seg()
VOID CMiniportWaveRTStream::WriteBytes(_In_ ULONGLONG displacement)
{
    if (!m_pDmaBuffer || !m_ulDmaBufferSize) return;
    // A scheduling gap cannot recover overwritten audio. Bound work to the most
    // recent physical buffer while preserving the complete64bit clock advance.
    ULONG bytes = static_cast<ULONG>(min(displacement, static_cast<ULONGLONG>(m_ulDmaBufferSize)));
    ULONG offset = static_cast<ULONG>((m_ullLinearPosition + displacement - bytes) % m_ulDmaBufferSize);
    while (bytes) {
        const ULONG run = min(bytes, m_ulDmaBufferSize - offset);
        m_pMiniport->GetAdapterCommObj()->CableRead(m_pDmaBuffer + offset, run);
        offset = (offset + run) % m_ulDmaBufferSize;
        bytes -= run;
    }
}

//=============================================================================
#pragma code_seg()
VOID CMiniportWaveRTStream::ReadBytes(_In_ ULONGLONG displacement)
{
    if (!m_pDmaBuffer || !m_ulDmaBufferSize) return;
    // A scheduling gap cannot recover overwritten audio. Bound work to the most
    // recent physical buffer while preserving the complete64bit clock advance.
    ULONG bytes = static_cast<ULONG>(min(displacement, static_cast<ULONGLONG>(m_ulDmaBufferSize)));
    ULONG offset = static_cast<ULONG>((m_ullLinearPosition + displacement - bytes) % m_ulDmaBufferSize);
    while (bytes) {
        const ULONG run = min(bytes, m_ulDmaBufferSize - offset);
        m_pMiniport->GetAdapterCommObj()->CableWrite(m_pDmaBuffer + offset, run);
        offset = (offset + run) % m_ulDmaBufferSize;
        bytes -= run;
    }
}

//=============================================================================
#pragma code_seg("PAGE")
//=============================================================================
#pragma code_seg()
void TimerNotifyRT(_In_ PEX_TIMER timer, _In_opt_ PVOID context)
{
    auto stream = static_cast<CMiniportWaveRTStream*>(context);
    if (!stream) return;
    KIRQL irql; KeAcquireSpinLock(&stream->m_PositionSpinLock, &irql);
    if (stream->m_KsState == KSSTATE_RUN && stream->m_pDmaBuffer &&
        stream->m_ulDmaBufferSize && stream->m_ulNotificationsPerBuffer) {
        LARGE_INTEGER qpc = KeQueryPerformanceCounter(NULL);
        stream->UpdatePosition(qpc);
        const ULONG packetBytes = stream->m_ulDmaBufferSize / stream->m_ulNotificationsPerBuffer;
        const ULONGLONG packet = stream->m_ullLinearPosition / packetBytes;
        if (packet > stream->m_NotifiedPacket) {
            stream->m_NotifiedPacket = packet;
            ++stream->m_DiagnosticEventSignals;
            for (PLIST_ENTRY it = stream->m_NotificationList.Flink; it != &stream->m_NotificationList; it = it->Flink)
                KeSetEvent(CONTAINING_RECORD(it, NotificationListEntry, ListEntry)->NotificationEvent, 0, FALSE);
        }
        // ExDeleteTimer disables rearming before draining callbacks. Its Timer
        // argument remains valid until this callback returns, even during delete.
        stream->ArmNotificationTimer(timer);
    }
    KeReleaseSpinLock(&stream->m_PositionSpinLock, irql);
}
