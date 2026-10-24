# Native C++ SDK

The complete user-mode SDK consists of `AudioLink.h` and `AudioLink.cpp`. It uses documented Windows MMDevice, Configuration Manager and WASAPI APIs. No caller-provided kernel code, pointer, memory address or private IOCTL is accepted.

| Function | Purpose |
|---|---|
| `List(includeOtherAudio = false)` | Return endpoint metadata and any enumeration error. Other products are read-only even when included. |
| `Stream(endpointId, callback, stopEvent = nullptr, durationMs = 0)` | Stream through one verified Titan endpoint. Playback callbacks fill samples; recording callbacks inspect them. |
| `Test(renderId, captureId, durationMs = 1500, stopEvent = nullptr)` | Send and detect a bounded known signal through a paired virtual path. |
| `Describe(error)` | Format a detailed English stage/message/HRESULT diagnostic. |

Endpoint selection uses both the product endpoint marker and an actual matching root adapter ancestor. Names alone never authorize a stream. A test also requires matching adapter instances, correct directions, active endpoints and supported formats.

`Frame.samples` is interleaved stereo float data at 48,000 frames per second. It is valid during the callback only. Do not retain references or resize the supplied buffer. Playback values are sanitized/converted to the supported engine format. Capture flags and timestamps retain the Windows API measurement boundary.

`Frame.frames` counts stereo frames, so `samples.size()` is exactly `frames * 2`. For recording, `position` is the packet's Windows device-frame position and `qpc_100ns` is its WASAPI timestamp; inspect `flags` for discontinuity or timestamp-error indications. For playback, `position` counts frames submitted by this SDK call, while `qpc_100ns` is sampled before invoking the fill callback. It does not predict when the audio becomes audible.

`Stream` blocks its calling worker thread and uses audio events to wait. COM initialization, Windows audio clients, buffer ownership, callbacks and shutdown all remain on that thread. Return false from a callback or signal the caller-owned stop event to stop. Keep that event alive until the function returns. Callback exceptions become errors and release the Windows buffer before cleanup. The SDK does not make a slow callback meet a real-time deadline.

`Stream` uses shared-mode WASAPI. A duration of `0` runs until the callback or stop event requests an end; a nonzero duration must be 1-60,000 ms. `Test` accepts 500-5,000 ms. These durations control the complete operation, not the audio packet size. Windows chooses the shared-engine geometry. At the native driver boundary, capture requires two notifications per cyclic buffer and an integer packet duration of 1-40 ms; exclusive capture is unsupported. The transport reserves 5 ms plus both native packet durations, typically 25 ms with two 10 ms periods. These settings do not imply a measured 25 ms application-to-application delay.

The SDK bounds buffers and test durations. It does not write audio to disk, open a network connection, change default devices, bypass Windows microphone permissions or capture a physical microphone. Device removal and audio-service failures must be handled through the returned error rather than assuming an endpoint ID remains usable indefinitely.

`TestResult.matched` requires both complete, independent left/right sequences to arrive with the same sample offset. A short correlation window finds a candidate alignment; every frame of both supplied sequences is then checked. `verified_frames` is the complete stereo-frame count on success and remains zero if complete validation fails. `matched_channels` reports how many whole channel sequences passed (`0`, `1` or `2`).

The full check requires correlation of at least `0.9999`, matching signs at every sample and bounded residual error after fitting one constant gain per channel. The PCM16 conversion-noise limits are four quanta peak and two quanta RMS (`1 quantum = 1/32768`). `max_sample_error` reports the largest residual. `capture_rms` and `correlation` cover the entire located fixture, not just its alignment window. Missing coverage, later dropped/shifted samples and later corruption fail validation. These bounds accommodate conversion noise; they do not promise floating-point bit identity.

Inspect the verified/captured/rendered frame counts, discontinuities, timestamp errors and the error field together. A successful short fixture is not a certification of arbitrary applications, buffer sizes, clock conditions or long-running audio stability.
