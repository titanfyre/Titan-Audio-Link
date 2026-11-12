#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Native Windows 10/11 x64. Link ole32, uuid, propsys, cfgmgr32 and avrt.
// Calls initialize COM on their own thread. No default audio devices are changed.
namespace AudioLink {
enum class Direction { Playback, Recording };
struct Error {
    HRESULT code = S_OK;
    std::wstring stage;
    std::wstring message;
    explicit operator bool() const noexcept { return FAILED(code); }
};
struct Endpoint {
    std::wstring id, name, adapter_instance;
    Direction direction = Direction::Playback;
    DWORD state = 0;
    bool titan = false; // Exact product marker AND owned root hardware/service.
    std::uint32_t sample_rate = 0, channels = 0, bits_per_sample = 0;
    double default_period_ms = 0, minimum_period_ms = 0;
    Error format_error;
};
struct Inventory {
    std::vector<Endpoint> endpoints;
    Error error;
};
// include_other_audio exposes read-only endpoint inventory; Test still refuses it.
Inventory List(bool include_other_audio = false);
struct Frame {
    Direction direction = Direction::Playback;
    std::vector<float> samples; // Interleaved stereo, nominal -1..1, 48000 frames/s.
    std::uint32_t frames = 0;
    std::uint64_t position = 0, qpc_100ns = 0;
    DWORD flags = 0; // AUDCLNT_BUFFERFLAGS_* for recording; zero for playback.
};
struct StreamResult {
    Error error;
    bool cancelled = false;
    std::uint64_t frames = 0;
    std::uint32_t discontinuities = 0, timestamp_errors = 0;
};
// Blocking event-driven stream; run on a worker thread. Endpoint direction is
// detected automatically. Playback callback fills the requested samples; recording
// callback reads supplied samples. Return false to stop. Do not resize samples.
// Frame references are valid ONLY during the callback. Callback must finish within
// an audio period; file/network/UI work belongs on another thread.
// max_duration_ms=0 runs until callback false / stop_event. Other values: 1..60000.
// Lifecycle and callbacks stay on this thread. All buffers are bounded; no disk I/O.
StreamResult Stream(const std::wstring& endpoint_id,
                    const std::function<bool(Frame&)>& callback,
                    HANDLE stop_event = nullptr, std::uint32_t max_duration_ms = 0);
struct TestResult {
    Error error;
    bool cancelled = false, matched = false;
    std::uint32_t matched_channels = 0; // Both independent stereo sequences must match.
    std::uint64_t verified_frames = 0; // Complete stereo sequence; never a short anchor.
    std::uint64_t rendered_frames = 0, captured_frames = 0;
    std::uint32_t discontinuities = 0, timestamp_errors = 0, silent_packets = 0;
    double elapsed_ms = 0, capture_rms = 0, correlation = 0;
    double max_sample_error = 0; // Largest full-fixture residual after constant gain fit.
    // Match between known generated sample indices and captured sample indices.
    // This is NOT hardware, acoustic, or application end-to-end latency.
    std::int64_t sample_offset_frames = 0;
    double sample_offset_ms = 0;
    // WASAPI packet timestamp minus QPC time at corresponding render submission.
    // Includes engine buffering; scheduler/timestamp uncertainty applies.
    double observed_delivery_ms = 0;
    std::wstring render_id, capture_id;
};
// Generates a quiet deterministic test signal into one virtual playback endpoint,
// then reads ONLY its paired Titan virtual recording endpoint. Never a physical mic.
// duration_ms: 500..5000. stop_event: optional caller-owned manual-reset event.
// No audio is written to disk. Run off the UI thread. Single test per pair at a time.
TestResult Test(const std::wstring& render_id, const std::wstring& capture_id,
                std::uint32_t duration_ms = 1500, HANDLE stop_event = nullptr);
std::wstring Describe(const Error& error);
} // namespace AudioLink
