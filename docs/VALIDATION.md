# Validation and current scope

Signed evaluation build. Compilation, offline tests and source review do not establish that a signed audio driver works correctly in every Windows audio stack.

## Implemented boundary

- One playback and one recording endpoint per root adapter, using standard Windows PortCls/WaveRT.
- Native 48 kHz, stereo, signed 16-bit PCM; separate bounded transport per adapter.
- Explicit endpoint selection and a 160 ms ring capacity. The reserve is 5 ms plus both configured native packet durations (25 ms with the usual two 10 ms packets), with a maximum reserve of 85 ms.
- Native C++ endpoint inventory, deterministic fixture comparison and worker-thread PCM streaming.
- Shared-mode WASAPI is the SDK's supported streaming path. Native capture requires two DMA notifications per cyclic buffer; single-notification and non-notification capture are rejected. Native packet durations must be an integer number of milliseconds between 1 and 40 ms. Exclusive capture is unsupported in this preview.
- No ASIO component, network streaming, physical audio hardware emulation, echo cancellation or general mixer is claimed.

## Test status

The native C++ Release and AddressSanitizer builds pass the offline PCM conversion, fixture detection, endpoint attribution and argument guards. The installer has 60 focused identity, signature-policy and embedded-license checks. These checks do not install a driver on the development PC.

Private production-header tests cover the bounded PCM ring (725,840 assertions), capture-packet metadata (1,320,522), fractional audio clock (1,550,128) and notification deadlines (1,952,013). All four suites pass normally and under AddressSanitizer: 5,548,503 assertions per configuration. Kernel compilation uses W4/WX and PREfast; all eleven translation-unit reports contain zero reported defects. INF Windows/Universal checks and catalog generation pass.

Two completed Windows 10 19045 guest campaigns used guest-only test-signed copies of the current unsigned submission image, SHA-256 `430b587e1dc06f8ac53b48ca4ad193a76b15ffa6d6c479b340628b2919a3e6f2`. One ran normally; the other enabled Driver Verifier for `TitanAudioLink.sys` with flags `0x001209BB`. Each campaign created two adapters and four active endpoints and passed 38 checks: six complete stereo fixtures, cross-pair refusal, pre-signalled cancellation, recovery from a deliberate callback exception, simultaneous pair isolation in both directions, repeated open/close, device removal and driver unload. No bugcheck was observed in either campaign. The guest shut down normally after each campaign, and its original baseline was restored. These are not Microsoft-signed production-package tests.

The twelve fixtures verified 384,000 stereo frames in total. Whole-fixture correlation ranged from `0.999999992368` to `0.999999992412`; the largest gain-fitted sample error was `0.0000563775` (about 1.85 PCM16 quanta). Each fixture reported one WASAPI discontinuity flag and zero timestamp errors. The isolation captures had zero packet-position gaps or regressions in either direction; their first recorded packet carried the single reported discontinuity flag. Nominally quiet channels had peak magnitude of one PCM16 quantum and RMS about `0.00001444`, within the stated conversion-noise bounds.

Observed render-submission-to-capture timestamps across both campaigns ranged from approximately 50.31 to 56.71 ms in this guest configuration; sample-index alignment was 58.33 ms. These are software endpoint measurements with the configured reserve and Windows engine buffering, not physical, acoustic or application end-to-end latency guarantees.

Dedicated private ETW traces contained 21 product stop-summary records per campaign. Their DMA-loss, ring-missing, ring-drop and skipped-packet counters were all zero; the trace headers reported no lost events or buffers. These counters cover the observed streams, not every possible failure or an extended soak test.

For this test-signed submission image, the Windows 11, HVCI/Secure Boot, installer-recovery and extended soak checks remain separate. An earlier candidate failed Driver Verifier; its crash evidence and the other failed experiments are retained privately. Their results are not replaced by the successful current-image campaigns. No unsigned driver or lab certificate is supplied in the public ZIP.

The release notes will identify actual tests and any remaining blockers. A sample waveform or sample-mode screenshot is never evidence of live kernel transport.

## Signed 0.2.0 demo on Windows 11

The exact published demo ZIP installed driver 0.2.0.0 in a Windows 11 build 26100 x64 Hyper-V guest with Secure Boot enabled. The installed INF, driver, clock and trusted Microsoft catalog matched the ZIP manifest. Setup and the bundled console tool verified one started playback/recording pair.

The generated-signal test verified 72,000 complete stereo frames with both channels matched and correlation 1.000000. It reported one WASAPI discontinuity flag and zero timestamp errors. Uninstall removed the pair, and the subsequent inventory found no Titan Audio Link endpoints. This run covered a short signal on one pair. The older 0.1.1 campaign below covers different scenarios.

[Exact demo release and SHA-256](https://github.com/titanfyre/Titan-Audio-Link/releases/tag/v0.2.0-demo) | [First-run guide](https://titancsa.github.io/guides/titan-audio-link-first-run/)

With the updated 0.2.0 ZIP, Windows 11 also passed installation, stereo capture and complete removal with Secure Boot and Memory Integrity enabled. In a separate Windows 11 test, OBS Studio 33.0.3 measured silence, the demo's 440 Hz tone and then silence on the Titan capture endpoint. The OBS test measured the input meter. [Memory Integrity report](https://titancsa.github.io/assets/titan-audio-link-memory-integrity-2026-11-26.json) · [OBS report](https://titancsa.github.io/assets/titan-audio-link-obs-check-2026-11-27.json)

## Measurement boundaries

The test plays a quiet deterministic fixture to a verified virtual playback endpoint and reads only its paired verified virtual recording endpoint. No physical microphone or speaker is involved. Audio is not saved to a file.

The sample offset is a correlation alignment between generated and captured sample indices. The observed delivery interval compares the render submission's QPC time to a WASAPI capture packet timestamp. Both include Windows engine buffering and scheduler/timestamp uncertainty; these are not acoustic, network, application end-to-end or physical hardware latency measurements.

Windows audio periods are API-reported configuration values. WASAPI discontinuity/timestamp flags describe those packets and do not expose a complete internal driver-ring overrun/underrun counter. Zero flags alone do not prove uninterrupted or bit-perfect audio.

Kernel source is adapted from Microsoft's SimpleAudioSample, whose original render-discard and tone-generation behavior must not be mistaken for an audio link. The product's transport replaces those data paths; verification must compare data supplied on the render side against data observed on capture.

## Earlier signed evaluation 0.1.1 on Windows 10

The [0.1.1 evaluation release](https://github.com/titanfyre/Titan-Audio-Link/releases/tag/v0.1.1) uses the earlier 0.1.0.0 driver and has its own evaluation terms. Its returned package was verified against the submitted image: unchanged INF and Authenticode-protected SYS content, trusted Microsoft catalog, and INF/SYS catalog membership. Both self-contained installers embed the exact verified files.

The actual console installer, live functionality and uninstall/unload passed on Windows 10 build 19045 x64 twice: normally and under standard Driver Verifier targeted at this product and the other two evaluation drivers. Guest test-signing was disabled; no lab certificate was used. 38 checks on two virtual audio pairs per campaign: six complete stereo fixtures, pair/channel isolation, cancellation, callback recovery and repeated stream open/close. No new minidump was observed. Both campaigns restored the original VM and preserved its historical checkpoints. No driver was installed on the development host.

These are bounded VM tests, not a Windows 11, Secure Boot/HVCI, physical hardware, long-duration or customer-deployment qualification. Installer UI resources are byte-verified; the automated install/uninstall runs used the console setup.
