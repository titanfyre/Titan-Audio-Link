#include "../SDK/AudioLink.h"
#include <cmath>
#include <iostream>

int main() {
    const auto devices = AudioLink::List();
    if (devices.error) { std::wcerr << AudioLink::Describe(devices.error); return 1; }
    std::wstring playback;
    for (const auto& device : devices.endpoints)
        if (device.direction == AudioLink::Direction::Playback && device.state == 1) {
            if (!playback.empty()) { std::wcerr << L"Select a particular endpoint when multiple virtual cables are installed."; return 2; }
            playback = device.id;
        }
    if (playback.empty()) { std::wcerr << L"Install the signed Titan Audio Link driver first."; return 1; }
    std::wcout << L"Generating a quiet 440 Hz tone into the virtual cable for two seconds. Select its virtual microphone in the receiving application.\n";
    double phase = 0;
    const auto result = AudioLink::Stream(playback, [&](AudioLink::Frame& frame) {
        for (unsigned i = 0; i < frame.frames; ++i) {
            const float sample = static_cast<float>(0.1 * std::sin(phase));
            frame.samples[i * 2] = frame.samples[i * 2 + 1] = sample;
            phase += 6.283185307179586 * 440 / 48000;
            if (phase > 6.283185307179586) phase -= 6.283185307179586;
        }
        return true;
    }, nullptr, 2000);
    if (result.error) { std::wcerr << AudioLink::Describe(result.error); return 1; }
    std::wcout << L"Sent " << result.frames << L" stereo frames. No physical output or default audio device was selected.\n";
    return 0;
}
