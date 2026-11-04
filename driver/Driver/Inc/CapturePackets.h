// Copyright (c) 2026 TitanCSA. Metadata for complete captured WaveRT packets.
#pragma once
#include "PcmClock.h"

namespace TitanAudio {
class CapturePackets {
public:
    static constexpr uint64_t MaximumPacketBytes = 7680; //40ms native PCM quantum.
    static constexpr uint64_t MaximumQpcFrequency = 100000000000ULL;
    static bool SupportsLayout(bool capture, uint64_t notifications, uint64_t bufferBytes) {
        if ((notifications != 1 && notifications != 2) || (capture && notifications != 2) ||
            bufferBytes < 192 || bufferBytes > MaximumPacketBytes * 2 || bufferBytes % notifications != 0)
            return false;
        return bufferBytes / notifications <= MaximumPacketBytes && (bufferBytes / notifications) % 192 == 0;
    }
    void Configure(uint64_t packetBytes) {
        packetFrames_ = (packetBytes >= 192 && packetBytes <= MaximumPacketBytes && packetBytes % 192 == 0) ? packetBytes / 4 : 0;
        Reset();
    }
    void Reset() { latest_ = lastRead_ = ~uint64_t(0); currentFirst_ = latestFirst_ = 0; started_ = false; }
    void Start(uint64_t qpc, uint64_t linearBytes, uint64_t fractionalFrame) {
        if (!started_ || (packetFrames_ && linearBytes % (packetFrames_ * 4) == 0 && fractionalFrame == 0)) {
            currentFirst_ = qpc; started_ = true;
        }
    }
    bool Advance(uint64_t beforeBytes, uint64_t currentBytes, uint64_t qpc,
                 uint64_t frequency, uint64_t fractionalFrame) {
        if (!packetFrames_ || packetFrames_ > 48000 || !frequency || frequency > MaximumQpcFrequency ||
            currentBytes < beforeBytes || beforeBytes % 4 || currentBytes % 4 || fractionalFrame >= 10000000)
            return false;
        const uint64_t before = beforeBytes / 4 / packetFrames_;
        const uint64_t after = currentBytes / 4 / packetFrames_;
        if (after > before) {
            latest_ = after - 1;
            latestFirst_ = (latest_ == before && started_) ? currentFirst_ :
                FirstSampleQpc(currentBytes / 4 - latest_ * packetFrames_, fractionalFrame, qpc, frequency);
            currentFirst_ = FirstSampleQpc(currentBytes / 4 - after * packetFrames_, fractionalFrame, qpc, frequency);
            started_ = true;
        }
        return true;
    }
    bool Read(uint64_t& packet, uint64_t& firstSampleQpc) {
        if (latest_ == ~uint64_t(0) || latest_ == lastRead_) return false;
        packet = latest_; firstSampleQpc = latestFirst_; lastRead_ = latest_; return true;
    }
private:
    static uint64_t FirstSampleQpc(uint64_t framesBehind, uint64_t remainder, uint64_t qpc, uint64_t frequency) {
        // framesBehind <2 native packets (<=96000frames), frequency<=100GHz.
        // Split whole ticks before the fractional-frame numerator; no product
        // exceeds1e18. Exact rational conversion loses less than one QPC tick.
        const uint64_t whole = framesBehind * frequency;
        const uint64_t numerator = (whole % 48000) * 10000000 + remainder * frequency;
        const uint64_t ticks = whole / 48000 + numerator / (48000ULL * 10000000);
        return ticks <= qpc ? qpc - ticks : 0;
    }
    uint64_t packetFrames_, latest_, lastRead_, currentFirst_, latestFirst_;
    bool started_;
};
}
