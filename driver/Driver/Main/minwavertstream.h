/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    minwavertstream.h

Abstract:

    Definition of wavert miniport class.
--*/

#ifndef _SIMPLEAUDIOSAMPLE_MINWAVERTSTREAM_H_
#define _SIMPLEAUDIOSAMPLE_MINWAVERTSTREAM_H_

#include "common.h"
#include "PcmClock.h"
#include "CapturePackets.h"

//
// Structure to store notifications events in a protected list
//
typedef struct _NotificationListEntry
{
    LIST_ENTRY  ListEntry;
    PKEVENT     NotificationEvent;
} NotificationListEntry;

EXT_CALLBACK   TimerNotifyRT;

//=============================================================================
// Referenced Forward
//=============================================================================
class CMiniportWaveRT;
typedef CMiniportWaveRT *PCMiniportWaveRT;

//=============================================================================
// Classes
//=============================================================================
///////////////////////////////////////////////////////////////////////////////
// CMiniportWaveRTStream 
// 
class CMiniportWaveRTStream : 
    public IMiniportWaveRTInputStream,
    public IMiniportWaveRTStreamNotification,
    public CUnknown
{
protected:
    PPORTWAVERTSTREAM           m_pPortStream;
    LIST_ENTRY                  m_NotificationList;
    PEX_TIMER                   m_pNotificationTimer;
    ULONG                       m_ulNotificationIntervalMs;
    
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportWaveRTStream);
    ~CMiniportWaveRTStream();

    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;
    IMP_IMiniportWaveRTInputStream;
    IMP_IMiniportWaveRT;

    NTSTATUS                    Init
    ( 
        _In_  PCMiniportWaveRT    Miniport,
        _In_  PPORTWAVERTSTREAM   Stream,
        _In_  ULONG               Channel,
        _In_  BOOLEAN             Capture,
        _In_  PKSDATAFORMAT       DataFormat,
        _In_  GUID                SignalProcessingMode
    );

    // Friends
    friend class                CMiniportWaveRT;
    friend EXT_CALLBACK         TimerNotifyRT;
protected:
    CMiniportWaveRT*            m_pMiniport;
    ULONG                       m_ulPin;
    BOOLEAN                     m_bCapture;
    BOOLEAN                     m_bUnregisterStream;
    ULONG                       m_ulDmaBufferSize;
    BYTE*                       m_pDmaBuffer;
    ULONG                       m_ulNotificationsPerBuffer;
    KSSTATE                     m_KsState;
    ULONGLONG                   m_ullPlayPosition;
    ULONGLONG                   m_ullWritePosition;
    ULONGLONG                   m_ullLinearPosition;
    ULONGLONG                   m_ullDmaTimeStamp;
    LARGE_INTEGER               m_ullPerformanceCounterFrequency;
    TitanAudio::PcmClock         m_Clock;
    TitanAudio::CapturePackets   m_CapturePackets;
    ULONGLONG                   m_NotifiedPacket;
    ULONGLONG                   m_DiagnosticDmaLostBytes;
    ULONGLONG                   m_DiagnosticSkippedPackets;
    ULONGLONG                   m_DiagnosticReadPackets;
    ULONGLONG                   m_DiagnosticLastPacket;
    ULONGLONG                   m_DiagnosticEventSignals;
    ULONGLONG                   m_DiagnosticMaximumAdvance;
    ULONG                       m_DiagnosticRun;
    ULONG                       m_ulDmaMovementRate;
    PWAVEFORMATEXTENSIBLE       m_pWfExt;
    GUID                        m_SignalProcessingMode;
    KSPIN_LOCK                  m_PositionSpinLock;
    KMUTEX                      m_ManagementLock;

public:

    GUID GetSignalProcessingMode()
    {
        return m_SignalProcessingMode;
    }
    
private:
    NTSTATUS SetStateLocked(_In_ KSSTATE state);
    VOID ArmNotificationTimer(_In_ PEX_TIMER timer);
    VOID PublishBuffer(_In_ BYTE* buffer, _In_ ULONG bytes, _In_ ULONG notifications);

    //
    // Helper functions.
    //
    
#pragma code_seg()

    VOID WriteBytes
    (
        _In_ ULONGLONG ByteDisplacement
    );
        
    VOID ReadBytes
    (
        _In_ ULONGLONG ByteDisplacement
    );
    
    VOID UpdatePosition
    (
        _In_ LARGE_INTEGER ilQPC
    );
    

};
typedef CMiniportWaveRTStream *PCMiniportWaveRTStream;
#endif // _SIMPLEAUDIOSAMPLE_MINWAVERTSTREAM_H_

