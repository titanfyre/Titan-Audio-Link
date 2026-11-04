// Copyright (c) 2026 TitanCSA. Adapter-local stereo48kHzPCM16 transport.
// The caller serializes access. No pointers escape and no allocations occur here.
#pragma once
#ifdef _KERNEL_MODE
#include <ntddk.h>
using uint8_t = unsigned char;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;
#else
#include <stdint.h>
#include <string.h>
#endif

namespace TitanAudio {
class PcmRing {
public:
    static constexpr uint32_t Capacity = 30720; // 160 ms at 192,000 bytes/second
    static constexpr uint32_t Preroll = 960;    // Base5ms plus one quantum per side, real latency.
    static constexpr uint32_t MaximumPeriodBytes = 7680; // Native1..40ms packets.
    static_assert(Preroll + 3 * MaximumPeriodBytes < Capacity, "Reserve plus one maximum quantum must fit.");
    void Initialize() { render_ = capture_ = false; dropped_ = missing_ = 0; renderPeriod_ = capturePeriod_ = 0; Reset(); }
    bool ConfigurePeriod(bool capture, uint32_t packetBytes) {
        if (packetBytes && (packetBytes < 192 || packetBytes > MaximumPeriodBytes || packetBytes % 192)) return false;
        uint32_t& side = capture ? capturePeriod_ : renderPeriod_;
        if (side != packetBytes) { side = packetBytes; Reset(); }
        return true;
    }
    uint32_t ReserveBytes() const { return Preroll + renderPeriod_ + capturePeriod_; }
    void SetRunning(bool capture, bool running) {
        bool& side = capture ? capture_ : render_;
        if (side != running) { side = running; Reset(); }
    }
    void Write(const uint8_t* source, uint32_t bytes) {
        if (!render_ || !capture_ || bytes % 4) return;
        if (bytes > Capacity) { dropped_ += bytes - Capacity; source += bytes - Capacity; bytes = Capacity; }
        if (bytes > Capacity - count_) {
            uint32_t excess = bytes - (Capacity - count_);
            head_ = (head_ + excess) % Capacity; count_ -= excess; dropped_ += excess;
        }
        uint32_t tail = (head_ + count_) % Capacity;
        uint32_t first = bytes < Capacity - tail ? bytes : Capacity - tail;
        memcpy(data_ + tail, source, first); memcpy(data_, source + first, bytes - first);
        count_ += bytes;
    }
    void Read(uint8_t* destination, uint32_t bytes) {
        if (!render_ || !capture_ || bytes % 4) { memset(destination, 0, bytes); return; }
        uint32_t copied = bytes < count_ ? bytes : count_;
        uint32_t first = copied < Capacity - head_ ? copied : Capacity - head_;
        memcpy(destination, data_ + head_, first); memcpy(destination + first, data_, copied - first);
        head_ = (head_ + copied) % Capacity; count_ -= copied;
        if (copied < bytes) { memset(destination + copied, 0, bytes - copied); missing_ += bytes - copied; }
    }
    uint32_t Queued() const { return count_; }
    uint64_t DroppedBytes() const { return dropped_; }
    uint64_t MissingBytes() const { return missing_; }
private:
    void Reset() { head_ = 0; count_ = ReserveBytes(); memset(data_, 0, count_); }
    uint8_t data_[Capacity];
    uint32_t head_, count_;
    uint32_t renderPeriod_, capturePeriod_;
    uint64_t dropped_, missing_;
    bool render_, capture_;
};
}
