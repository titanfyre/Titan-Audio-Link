#include "../SDK/AudioLink.h"
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {
HANDLE stopEvent = nullptr;
BOOL WINAPI Stop(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) { if (stopEvent) SetEvent(stopEvent); return TRUE; }
    return FALSE;
}
void Print(const AudioLink::TestResult& r) {
    wprintf(L"%ls\nRendered: %llu frames | Captured: %llu frames\n", r.cancelled ? L"Stopped by user." : (r.error ? L"TEST FAILED" : L"TEST PASSED"), r.rendered_frames, r.captured_frames);
    wprintf(L"Signal matched: %ls | Correlation: %.6f | Capture RMS: %.6f\n",r.matched?L"yes":L"no",r.correlation,r.capture_rms);
    wprintf(L"Independent stereo channels matched: %u / 2\n",r.matched_channels);
    wprintf(L"Complete stereo frames verified: %llu | Maximum gain-fitted sample error: %.8f\n",r.verified_frames,r.max_sample_error);
    wprintf(L"Discontinuous packets: %u | Timestamp errors: %u | Silent packets: %u\n",r.discontinuities,r.timestamp_errors,r.silent_packets);
    wprintf(L"Sample offset: %lld frames (%.3f ms) | Observed render-submission to capture timestamp: %.3f ms\n",r.sample_offset_frames,r.sample_offset_ms,r.observed_delivery_ms);
    wprintf(L"Elapsed: %.1f ms. These are software endpoint observations, not hardware or acoustic latency.\n",r.elapsed_ms);
    if(r.error) wprintf(L"Error: %ls\n",AudioLink::Describe(r.error).c_str());
}
}
int wmain(int argc,wchar_t** argv) {
    const bool all=argc==3 && wcscmp(argv[1],L"--list")==0 && wcscmp(argv[2],L"--all")==0;
    if(argc==1 || (argc>=2 && wcscmp(argv[1],L"--list")==0 && (argc==2 || all))) {
        auto list=AudioLink::List(all);
        if(list.error) { fwprintf(stderr,L"%ls\n",AudioLink::Describe(list.error).c_str()); return 1; }
        if(list.endpoints.empty()) wprintf(L"No verified Titan Audio Link endpoint. Install the signed driver, then refresh.\n");
        for(const auto& e:list.endpoints) {
            wprintf(L"[%ls] %ls%ls\n  State: 0x%X | %u Hz | %u channels | %u bits | Engine period %.3f ms (minimum %.3f ms)\n  Endpoint: %ls\n  Adapter: %ls\n",
                e.direction==AudioLink::Direction::Playback?L"Playback destination":L"Virtual microphone",e.name.c_str(),e.titan?L" [verified Titan]":L" [other audio: read-only]",
                e.state,e.sample_rate,e.channels,e.bits_per_sample,e.default_period_ms,e.minimum_period_ms,e.id.c_str(),e.adapter_instance.c_str());
            if(e.format_error) wprintf(L"  Format status: %ls\n",AudioLink::Describe(e.format_error).c_str());
        }
        return 0;
    }
    if(argc==2 && wcscmp(argv[1],L"--preview")==0) {
        wprintf(L"OFFLINE PREVIEW: synthetic display data only. No endpoint opened and no audio played or recorded.\n");
        wprintf(L"Playback destination -> same-adapter virtual microphone\n48 kHz | stereo | PCM16 driver transport\nSelect --list for actual endpoint inventory. Select --test to generate and verify a bounded virtual test signal.\n");
        return 0;
    }
    if((argc==2 || argc==4) && wcscmp(argv[1],L"--test")==0) {
        std::wstring render,capture;
        if(argc==4) { render=argv[2]; capture=argv[3]; }
        else {
            const auto list=AudioLink::List();
            if(list.error) { fwprintf(stderr,L"%ls\n",AudioLink::Describe(list.error).c_str()); return 1; }
            unsigned pairs=0;
            for(const auto& out:list.endpoints) for(const auto& in:list.endpoints)
                if(out.direction==AudioLink::Direction::Playback && in.direction==AudioLink::Direction::Recording &&
                   out.adapter_instance==in.adapter_instance && out.state==1 && in.state==1) { render=out.id; capture=in.id; ++pairs; }
            if(pairs!=1) { fwprintf(stderr,L"Expected one active Titan pair, found %u. Use --list and --test \"playback endpoint ID\" \"recording endpoint ID\" to choose explicitly.\n",pairs); return 2; }
        }
        stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!stopEvent) return 1;
        SetConsoleCtrlHandler(Stop,TRUE);
        wprintf(L"Generating a quiet test signal into the selected Titan virtual cable for 1.5 seconds. Only its paired virtual microphone is read. Ctrl+C stops.\n");
        const auto result=AudioLink::Test(render,capture,1500,stopEvent);
        SetConsoleCtrlHandler(Stop,FALSE); CloseHandle(stopEvent); stopEvent=nullptr;
        Print(result); return result.error?1:(result.cancelled?3:0);
    }
    fwprintf(stderr,L"Usage: AudioLinkCLI --list [--all]\n       AudioLinkCLI --test [\"playback endpoint ID\" \"recording endpoint ID\"]\n       AudioLinkCLI --preview\nNo defaults are changed. Test never opens a physical microphone.\n");
    return 2;
}
