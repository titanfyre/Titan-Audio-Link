// Copyright (c) 2026 TitanCSA. QPC-sampled PCM packet boundary scheduling.
#pragma once
#include "PcmClock.h"

namespace TitanAudio {
class PacketDeadline {
public:
    // Clock remainder is a fractional frame with denominator10,000,000.
    // Subtract time spent after sampling QPC so callback processing cannot
    // accumulate phase drift. A late deadline requests a one-shot catch-up,
    // never a software busy wait. Zero means invalid native packet geometry.
    static uint64_t Delay100ns(uint64_t linearBytes, uint64_t fractionalFrame,
                              uint64_t packetBytes, uint64_t elapsedSinceSample) {
        if (linearBytes % 4 || fractionalFrame >= 10000000 ||
            packetBytes < 192 || packetBytes > 192000 || packetBytes % 192)
            return 0;
        const uint64_t frames = packetBytes / 4;
        const uint64_t remaining = frames - (linearBytes / 4) % frames;
        const uint64_t numerator = remaining * 10000000 - fractionalFrame;
        const uint64_t until = (numerator + 47999) / 48000;
        return elapsedSinceSample >= until ? 1 : until - elapsedSinceSample;
    }
};
}
