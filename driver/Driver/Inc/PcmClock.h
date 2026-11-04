// Copyright (c) 2026 TitanCSA. Exact whole-frame48kHzPCM16stereo clock.
#pragma once
#ifdef _KERNEL_MODE
#include <ntddk.h>
using uint64_t = unsigned long long;
#else
#include <stdint.h>
#endif

namespace TitanAudio {
class PcmClock {
public:
    void Reset() { remainder_ = 0; }
    // QPC converted to100ns. Split whole seconds before multiplying, so even
    // UINT64_MAX elapsed100ns cannot overflow intermediate or returned bytes.
    uint64_t Advance(uint64_t elapsed100ns) {
        constexpr uint64_t units = 10000000;
        const uint64_t fractional = (elapsed100ns % units) * 48000 + remainder_;
        const uint64_t frames = (elapsed100ns / units) * 48000 + fractional / units;
        remainder_ = fractional % units;
        return frames * 4;
    }
    uint64_t FractionalRemainder() const { return remainder_; }
private:
    uint64_t remainder_;
};
}
