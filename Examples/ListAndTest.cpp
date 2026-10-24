#include "../SDK/AudioLink.h"
#include <iostream>

int main() {
    // Each adapter is a virtual cable: playback in, virtual microphone out.
    const auto inventory = AudioLink::List();
    if (inventory.error) { std::wcerr << AudioLink::Describe(inventory.error) << L'\n'; return 1; }
    for (const auto& endpoint : inventory.endpoints)
        std::wcout << endpoint.name << L"\n  " << endpoint.id << L'\n';
    // Explicitly choose same-adapter endpoint IDs from the list, then:
    // const auto result = AudioLink::Test(playback_id, recording_id);
    // if (result.error) std::wcerr << AudioLink::Describe(result.error);
    return 0;
}
