#include "AudioLink.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <devicetopology.h>
#include <functiondiscoverykeys_devpkey.h>
#include <initguid.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <avrt.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
using Microsoft::WRL::ComPtr;
namespace AudioLink {
namespace Detail {
constexpr PROPERTYKEY ProductKey = {{0x44ceb611,0xdf3d,0x4cf0,{0x84,0xa9,0x09,0x9a,0x40,0xd5,0x31,0x71}},2};
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
    bool valid() const noexcept { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};
struct FreeMem { void operator()(void* value) const noexcept { CoTaskMemFree(value); } };
template<class T> using CoMem = std::unique_ptr<T, FreeMem>;
struct Event {
    HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() { if (value) CloseHandle(value); }
};
struct Mmcss {
    DWORD index = 0;
    HANDLE value = AvSetMmThreadCharacteristicsW(L"Audio", &index);
    ~Mmcss() { if (value) AvRevertMmThreadCharacteristics(value); }
};
Error Fail(HRESULT code, const wchar_t* stage, std::wstring detail = {}) {
    Error error{code, stage, std::move(detail)};
    if (error.message.empty()) {
        wchar_t* text = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(code),
                       MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                       reinterpret_cast<wchar_t*>(&text), 0, nullptr);
        if (text) { error.message = text; LocalFree(text); }
        if (error.message.empty()) error.message = L"Windows audio operation failed.";
        while (!error.message.empty() && (error.message.back() == L'\r' || error.message.back() == L'\n')) error.message.pop_back();
    }
    return error;
}
std::wstring StringProperty(IPropertyStore* store, REFPROPERTYKEY key) {
    PROPVARIANT value; PropVariantInit(&value);
    std::wstring result;
    if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_LPWSTR && value.pwszVal) result = value.pwszVal;
    PropVariantClear(&value); return result;
}
bool RootProof(const wchar_t* instance,const wchar_t* hardware,std::size_t units,const wchar_t* service) noexcept {
    if(!instance || !hardware || !service || _wcsnicmp(instance,L"ROOT\\",5)!=0 ||
       _wcsicmp(service,L"TitanAudioLink")!=0 || units<2 || hardware[units-1]!=0 || hardware[units-2]!=0)return false;
    bool matched=false;
    for(std::size_t i=0;i<units && hardware[i];) {
        const auto remaining=units-i;const auto length=wcsnlen_s(hardware+i,remaining);
        if(length==remaining)return false;
        if(_wcsicmp(hardware+i,L"ROOT\\TitanAudioLink")==0)matched=true;
        i+=length+1;
    }
    return matched;
}
std::wstring RootIdentity(std::wstring instance) {
    DEVINST node = 0;
    if (instance.empty() || CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) return {};
    for (unsigned depth = 0; depth != 16; ++depth) {
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return {};
        // SetupDi-generated root nodes may be ROOT\\MEDIA\\0000. The instance
        // spelling is not the hardware ID; attribute by exact PnP properties.
        if (_wcsnicmp(id,L"ROOT\\",5)==0) {
            DEVPROPTYPE type=0;ULONG needed=0;
            const auto query=CM_Get_DevNode_PropertyW(node,&DEVPKEY_Device_HardwareIds,&type,nullptr,&needed,0);
            if(query==CR_BUFFER_SMALL && needed>=2*sizeof(wchar_t) && needed<=65536 && needed%sizeof(wchar_t)==0) {
                std::vector<wchar_t> hardware(needed/sizeof(wchar_t));
                if(CM_Get_DevNode_PropertyW(node,&DEVPKEY_Device_HardwareIds,&type,reinterpret_cast<BYTE*>(hardware.data()),&needed,0)==CR_SUCCESS && type==DEVPROP_TYPE_STRING_LIST && needed>=2*sizeof(wchar_t) && needed<=hardware.size()*sizeof(wchar_t) && needed%sizeof(wchar_t)==0) {
                    wchar_t service[128]{};ULONG bytes=sizeof(service);
                    if(CM_Get_DevNode_PropertyW(node,&DEVPKEY_Device_Service,&type,reinterpret_cast<BYTE*>(service),&bytes,0)==CR_SUCCESS && type==DEVPROP_TYPE_STRING && bytes>=sizeof(wchar_t) && bytes<=sizeof(service) && bytes%sizeof(wchar_t)==0 && service[bytes/sizeof(wchar_t)-1]==0 && RootProof(id,hardware.data(),needed/sizeof(wchar_t),service)) return id;
                }
            }
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) return {};
        node = parent;
    }
    return {};
}
std::wstring AdapterIdentity(IMMDevice* endpoint, IMMDeviceEnumerator* enumerator) {
    ComPtr<IDeviceTopology> topology; ComPtr<IConnector> connector;
    if (FAILED(endpoint->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, &topology)) ||
        FAILED(topology->GetConnector(0, &connector))) return {};
    wchar_t* rawId = nullptr;
    if (FAILED(connector->GetDeviceIdConnectedTo(&rawId))) return {};
    CoMem<wchar_t> id(rawId); ComPtr<IMMDevice> adapter; ComPtr<IPropertyStore> properties;
    if (FAILED(enumerator->GetDevice(id.get(), &adapter)) || FAILED(adapter->OpenPropertyStore(STGM_READ, &properties))) return {};
    return RootIdentity(StringProperty(properties.Get(), PKEY_Device_InstanceId));
}
struct Format {
    unsigned channels = 0, rate = 0, bytes = 0, stride = 0, valid_bits = 0;
    bool floating = false;
};
bool ParseFormat(const WAVEFORMATEX& wave, Format& format) noexcept {
    if (wave.nChannels < 1 || wave.nChannels > 8 || wave.nSamplesPerSec < 8000 || wave.nSamplesPerSec > 384000) return false;
    unsigned bits = wave.wBitsPerSample;
    bool floating = wave.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    unsigned valid = bits;
    if (wave.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (wave.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) return false;
        const auto& extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(wave);
        if (extended.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) floating = true;
        else if (extended.SubFormat != KSDATAFORMAT_SUBTYPE_PCM) return false;
        valid = extended.Samples.wValidBitsPerSample;
    } else if (wave.wFormatTag != WAVE_FORMAT_PCM && !floating) return false;
    if (floating ? (bits != 32 || valid != 32) :
        ((bits != 16 && bits != 24 && bits != 32) || valid < 2 || valid > bits)) return false;
    if (wave.nBlockAlign != wave.nChannels * (bits / 8) || wave.nAvgBytesPerSec != wave.nSamplesPerSec * wave.nBlockAlign) return false;
    format = {wave.nChannels, wave.nSamplesPerSec, bits / 8, wave.nBlockAlign, valid, floating};
    return true;
}
float ReadSample(const BYTE* p, const Format& f) noexcept {
    if (f.floating) { float v; std::memcpy(&v, p, sizeof(v)); return std::isfinite(v) ? v : 0.0f; }
    std::int32_t value = 0;
    if (f.bytes == 2) { std::int16_t v; std::memcpy(&v,p,2); value = v; }
    else if (f.bytes == 3) { value = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16)); if (value & 0x800000) value |= static_cast<std::int32_t>(0xff000000u); }
    else std::memcpy(&value,p,4);
    return static_cast<float>(static_cast<double>(value) / std::ldexp(1.0, static_cast<int>(f.bytes * 8 - 1)));
}
void WriteSample(BYTE* p, const Format& f, float sample) noexcept {
    if (!std::isfinite(sample)) sample = 0;
    sample = std::clamp(sample,-1.0f,1.0f);
    if (f.floating) { std::memcpy(p, &sample, 4); return; }
    const unsigned container_bits = f.bytes * 8;
    const double scale = std::ldexp(1.0, static_cast<int>(f.valid_bits - 1));
    const auto magnitude = static_cast<std::int64_t>(std::clamp(std::round(std::clamp(static_cast<double>(sample), -1.0, 1.0) * scale),-scale,scale-1));
    const auto value = static_cast<std::uint32_t>(magnitude * (std::int64_t{1} << (container_bits - f.valid_bits)));
    for (unsigned byte = 0; byte != f.bytes; ++byte) p[byte] = static_cast<BYTE>(value >> (byte * 8));
}
std::vector<float> Signal(std::size_t frames,std::uint32_t seed=0x4c6e6b31) {
    std::vector<float> data(frames);
    for (auto& value : data) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; value = (seed & 1) ? 0.12f : -0.12f; }
    return data;
}
struct Match { bool found = false; std::size_t source = 0, captured = 0; double correlation = 0, rms = 0; };
Match Analyze(const std::vector<float>& source, const std::vector<float>& captured) {
    Match result;
    if (source.size() < 256 || captured.size() < 256) return result;
    double energy = 0;
    for (float sample : captured) energy += static_cast<double>(sample) * sample;
    result.rms = std::sqrt(energy / static_cast<double>(captured.size()));
    if (result.rms < 0.0001) return result;
    std::unordered_map<std::uint32_t, std::size_t> codes;
    codes.reserve(source.size());
    std::uint32_t code = 0;
    for (std::size_t i = 0; i != source.size(); ++i) {
        code = (code << 1) | static_cast<unsigned>(source[i] > 0);
        if (i >= 31 && i + 224 < source.size()) codes.emplace(code, i - 31);
    }
    code = 0;
    for (std::size_t i = 0; i + 224 < captured.size(); ++i) {
        code = (code << 1) | static_cast<unsigned>(captured[i] > 0);
        if (i < 31) continue;
        const auto found = codes.find(code);
        if (found == codes.end()) continue;
        const auto at = i - 31;
        double product = 0, a = 0, b = 0;
        for (std::size_t j = 0; j != 256; ++j) {
            const double x = source[found->second + j], y = captured[at + j];
            product += x*y; a += x*x; b += y*y;
        }
        const double correlation = b > 0 ? product / std::sqrt(a*b) : 0;
        if (correlation > 0.98) return {true, found->second, at, correlation, result.rms};
    }
    return result;
}
struct FullMatch {bool covered=false,valid=false;std::size_t offset=0;double correlation=0,rms=0,maxError=0;};
FullMatch ValidateFull(const std::vector<float>& source,const std::vector<float>& captured,const Match& anchor) noexcept {
    FullMatch result;
    if(!anchor.found || anchor.captured<anchor.source || source.empty())return result;
    result.offset=anchor.captured-anchor.source;
    if(result.offset>captured.size() || source.size()>captured.size()-result.offset)return result;
    result.covered=true;double product=0,sourceEnergy=0,captureEnergy=0;
    for(std::size_t i=0;i<source.size();++i){const double a=source[i],b=captured[result.offset+i];product+=a*b;sourceEnergy+=a*a;captureEnergy+=b*b;}
    result.rms=std::sqrt(captureEnergy/static_cast<double>(source.size()));
    if(sourceEnergy<=0 || captureEnergy<=0)return result;
    result.correlation=product/std::sqrt(sourceEnergy*captureEnergy);
    const double gain=product/sourceEnergy;double residualEnergy=0;bool signs=true;
    for(std::size_t i=0;i<source.size();++i){const double expected=source[i]*gain,actual=captured[result.offset+i],error=actual-expected;signs=signs&&((source[i]>0)==(actual>0))&&actual!=0;result.maxError=std::max(result.maxError,std::abs(error));residualEnergy+=error*error;}
    // Fixed PCM16 transport plus shared-engine conversion can add quantization
    // noise. These explicit bounds are not permission to skip any later frame.
    constexpr double quantum=1.0/32768.0;
    result.valid=gain>0 && signs && result.correlation>=0.9999 && result.maxError<=4*quantum &&
        std::sqrt(residualEnergy/static_cast<double>(source.size()))<=2*quantum;
    return result;
}
Endpoint ReadEndpoint(IMMDevice* device, IMMDeviceEnumerator* enumerator) {
    Endpoint item; wchar_t* raw = nullptr;
    if (SUCCEEDED(device->GetId(&raw))) { CoMem<wchar_t> id(raw); item.id = id.get(); }
    device->GetState(&item.state);
    ComPtr<IMMEndpoint> endpoint;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&endpoint)))) { EDataFlow flow; if (SUCCEEDED(endpoint->GetDataFlow(&flow))) item.direction = flow == eRender ? Direction::Playback : Direction::Recording; }
    ComPtr<IPropertyStore> properties;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
        item.name = StringProperty(properties.Get(), PKEY_Device_FriendlyName);
        const auto marker = StringProperty(properties.Get(), ProductKey);
        if (marker == L"TitanAudioLink/1") item.adapter_instance = AdapterIdentity(device, enumerator);
        item.titan = marker == L"TitanAudioLink/1" && !item.adapter_instance.empty();
    }
    if (item.state != DEVICE_STATE_ACTIVE) return item;
    ComPtr<IAudioClient> client;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if (FAILED(hr)) { item.format_error = Fail(hr,L"endpoint_format"); return item; }
    WAVEFORMATEX* rawFormat = nullptr;
    hr = client->GetMixFormat(&rawFormat); CoMem<WAVEFORMATEX> format(rawFormat);
    if (SUCCEEDED(hr)) { item.sample_rate = format->nSamplesPerSec; item.channels = format->nChannels; item.bits_per_sample = format->wBitsPerSample; }
    else item.format_error = Fail(hr,L"mix_format");
    REFERENCE_TIME normal = 0, minimum = 0;
    if (SUCCEEDED(client->GetDevicePeriod(&normal,&minimum))) { item.default_period_ms = static_cast<double>(normal)/10000; item.minimum_period_ms = static_cast<double>(minimum)/10000; }
    return item;
}
struct Client {
    ComPtr<IAudioClient> audio;
    CoMem<WAVEFORMATEX> wave;
    Format format;
    Event event;
    UINT32 buffer = 0;
    bool started = false;
    ~Client() { if (started) audio->Stop(); }
    Error Open(IMMDevice* device) {
        HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audio);
        if (FAILED(hr)) return Fail(hr,L"activate_audio_client");
        WAVEFORMATEX* raw = nullptr; hr = audio->GetMixFormat(&raw); wave.reset(raw);
        if (FAILED(hr)) return Fail(hr,L"mix_format");
        if (!ParseFormat(*wave,format) || format.channels != 2 || format.rate != 48000)
            return Fail(AUDCLNT_E_UNSUPPORTED_FORMAT,L"test_format",L"The current cable test requires a 48 kHz stereo PCM or float shared-engine mix format on both endpoints.");
        if (!event.value) return Fail(HRESULT_FROM_WIN32(GetLastError()),L"create_audio_event");
        hr = audio->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                               0,0,wave.get(),nullptr);
        if (FAILED(hr)) return Fail(hr,L"initialize_audio_client");
        if (FAILED(hr = audio->SetEventHandle(event.value))) return Fail(hr,L"set_audio_event");
        if (FAILED(hr = audio->GetBufferSize(&buffer))) return Fail(hr,L"audio_buffer_size");
        if (!buffer || buffer > 48000) return Fail(E_UNEXPECTED,L"audio_buffer_size",L"The Windows audio engine returned an unsupported buffer size.");
        return {};
    }
    Error Start() { const HRESULT hr = audio->Start(); if (FAILED(hr)) return Fail(hr,L"start_audio_client"); started = true; return {}; }
};
struct PacketStamp { std::size_t frame; std::uint64_t qpc100ns; };
struct RenderLease {
    IAudioRenderClient* client;
    UINT32 frames;
    bool held=true;
    ~RenderLease(){if(held)client->ReleaseBuffer(frames,AUDCLNT_BUFFERFLAGS_SILENT);}
    HRESULT Release(DWORD flags=0) noexcept {held=false;return client->ReleaseBuffer(frames,flags);}
};
struct CaptureLease {
    IAudioCaptureClient* client;
    UINT32 frames;
    bool held=true;
    ~CaptureLease(){if(held)client->ReleaseBuffer(frames);}
    HRESULT Release() noexcept {held=false;return client->ReleaseBuffer(frames);}
};
Error MemoryFailure() {Error error;error.code=E_OUTOFMEMORY;error.stage=L"memory";return error;}
bool ValidPlaybackFrame(const Frame& frame,std::size_t expected) noexcept {
    return frame.samples.size()==expected;
}
} // Detail

std::wstring Describe(const Error& error) {
    if (!error) return L"No error.";
    wchar_t code[32]{}; swprintf_s(code,L"0x%08X",static_cast<unsigned>(error.code));
    const wchar_t* fallback=error.code==E_OUTOFMEMORY?L"Windows could not allocate the bounded audio buffers.":L"Windows audio operation failed.";
    return error.stage + L": " + (error.message.empty()?std::wstring(fallback):error.message) + L" (" + code + L")";
}
Inventory List(bool include_other_audio) try {
    using namespace Detail;
    Inventory result; ComScope com;
    if (!com.valid()) { result.error = Fail(com.hr,L"initialize_com"); return result; }
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { result.error = Fail(hr,L"audio_enumerator"); return result; }
    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eAll,DEVICE_STATEMASK_ALL,&collection);
    if (FAILED(hr)) { result.error = Fail(hr,L"enumerate_endpoints"); return result; }
    UINT count = 0;
    if (FAILED(hr = collection->GetCount(&count))) { result.error = Fail(hr,L"endpoint_count"); return result; }
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i,&device))) continue;
        auto item = ReadEndpoint(device.Get(),enumerator.Get());
        if (item.titan || include_other_audio) result.endpoints.push_back(std::move(item));
    }
    return result;
} catch(const std::bad_alloc&) {Inventory result;result.error=Detail::MemoryFailure();return result;}
  catch(...) {Inventory result;result.error=Detail::Fail(E_UNEXPECTED,L"audio_inventory",L"An unexpected SDK error interrupted endpoint enumeration.");return result;}
StreamResult Stream(const std::wstring& endpoint_id,const std::function<bool(Frame&)>& callback,
                    HANDLE stop_event,std::uint32_t max_duration_ms) try {
    using namespace Detail;
    StreamResult result;
    if (!callback || max_duration_ms>60000) {result.error=Fail(E_INVALIDARG,L"stream_arguments",L"Provide an audio callback and a maximum duration of 0..60000 milliseconds.");return result;}
    if (stop_event) {const auto stopped=WaitForSingleObject(stop_event,0);if(stopped==WAIT_OBJECT_0){result.cancelled=true;return result;}if(stopped==WAIT_FAILED){result.error=Fail(HRESULT_FROM_WIN32(GetLastError()),L"stop_event");return result;}}
    ComScope com;
    if(!com.valid()){result.error=Fail(com.hr,L"initialize_com");return result;}
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator));
    if(FAILED(hr)){result.error=Fail(hr,L"audio_enumerator");return result;}
    ComPtr<IMMDevice> device;
    if(FAILED(hr=enumerator->GetDevice(endpoint_id.c_str(),&device))){result.error=Fail(hr,L"get_stream_endpoint");return result;}
    const auto info=ReadEndpoint(device.Get(),enumerator.Get());
    if(!info.titan){result.error=Fail(E_ACCESSDENIED,L"verify_virtual_endpoint",L"Audio streaming is restricted to an endpoint with the Titan product marker and a Titan Audio Link root adapter. Physical microphones and unrelated endpoints are refused.");return result;}
    Client client;if((result.error=client.Open(device.Get())))return result;
    ComPtr<IAudioRenderClient> render;ComPtr<IAudioCaptureClient> capture;
    const bool recording=info.direction==Direction::Recording;
    hr=recording?client.audio->GetService(IID_PPV_ARGS(&capture)):client.audio->GetService(IID_PPV_ARGS(&render));
    if(FAILED(hr)){result.error=Fail(hr,L"get_stream_service");return result;}
    // A defensive bound protects both allocation and the callback contract.
    if(!client.buffer || client.buffer>48000){result.error=Fail(E_UNEXPECTED,L"stream_buffer",L"The Windows audio engine returned an unsupported buffer size.");return result;}
    Frame frame;frame.direction=info.direction;frame.samples.reserve(static_cast<std::size_t>(client.buffer)*2);
    if(!recording){BYTE* bytes=nullptr;if(FAILED(hr=render->GetBuffer(client.buffer,&bytes)) || FAILED(hr=render->ReleaseBuffer(client.buffer,AUDCLNT_BUFFERFLAGS_SILENT))){result.error=Fail(hr,L"prime_stream");return result;}}
    Mmcss mmcss;if((result.error=client.Start()))return result;
    const ULONGLONG deadline=max_duration_ms?GetTickCount64()+max_duration_ms:0;
    HANDLE events[]={client.event.value,stop_event};bool finished=false;
    while(!finished && (!deadline || GetTickCount64()<deadline)) {
        const DWORD waited=WaitForMultipleObjects(stop_event?2:1,events,FALSE,100);
        if(waited==WAIT_OBJECT_0+1){result.cancelled=true;break;}
        if(waited==WAIT_FAILED){result.error=Fail(HRESULT_FROM_WIN32(GetLastError()),L"wait_stream_event");break;}
        if(waited==WAIT_TIMEOUT)continue;
        if(recording)for(;;){
            if(stop_event && WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0){result.cancelled=true;finished=true;break;}
            if(deadline && GetTickCount64()>=deadline){finished=true;break;}
            UINT32 available=0;if(FAILED(hr=capture->GetNextPacketSize(&available))){result.error=Fail(hr,L"capture_packet_size");break;}
            if(!available)break;
            BYTE* bytes=nullptr;UINT32 frames=0;DWORD flags=0;UINT64 position=0,timestamp=0;
            if(FAILED(hr=capture->GetBuffer(&bytes,&frames,&flags,&position,&timestamp))){result.error=Fail(hr,L"capture_get_buffer");break;}
            if(hr==AUDCLNT_S_BUFFER_EMPTY || !frames)break;
            CaptureLease lease{capture.Get(),frames};
            if(frames>client.buffer){result.error=Fail(E_UNEXPECTED,L"capture_buffer_bound",L"Capture packet exceeded the negotiated audio buffer.");break;}
            frame.direction=Direction::Recording;frame.frames=frames;frame.position=position;frame.qpc_100ns=timestamp;frame.flags=flags;
            frame.samples.resize(static_cast<std::size_t>(frames)*2);
            for(UINT32 i=0;i<frames;++i)for(unsigned channel=0;channel<2;++channel)frame.samples[static_cast<std::size_t>(i)*2+channel]=(flags&AUDCLNT_BUFFERFLAGS_SILENT)?0:ReadSample(bytes+i*client.format.stride+channel*client.format.bytes,client.format);
            hr=lease.Release();
            if(FAILED(hr)){result.error=Fail(hr,L"capture_release_buffer");break;}
            result.frames+=frames;if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)++result.discontinuities;if(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)++result.timestamp_errors;
            try{finished=!callback(frame);}catch(...){result.error=Fail(E_UNEXPECTED,L"audio_callback",L"The audio callback threw an exception.");finished=true;}
            if(finished)break;
        } else {
            UINT32 padding=0;if(FAILED(hr=client.audio->GetCurrentPadding(&padding))){result.error=Fail(hr,L"render_padding");break;}
            const UINT32 frames=client.buffer-std::min(client.buffer,padding);if(!frames)continue;
            frame.direction=Direction::Playback;frame.frames=frames;frame.position=result.frames;frame.flags=0;
            LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
            frame.qpc_100ns=static_cast<std::uint64_t>(static_cast<double>(now.QuadPart)*10000000/static_cast<double>(frequency.QuadPart));
            frame.samples.assign(static_cast<std::size_t>(frames)*2,0);
            try{finished=!callback(frame);}catch(...){result.error=Fail(E_UNEXPECTED,L"audio_callback",L"The audio callback threw an exception.");finished=true;}
            if(finished)break;
            if(!ValidPlaybackFrame(frame,static_cast<std::size_t>(frames)*2)){result.error=Fail(E_INVALIDARG,L"audio_callback",L"The playback callback resized its sample buffer. Fill exactly the supplied stereo frames.");break;}
            BYTE* bytes=nullptr;if(FAILED(hr=render->GetBuffer(frames,&bytes))){result.error=Fail(hr,L"render_get_buffer");break;}
            RenderLease lease{render.Get(),frames};
            for(UINT32 i=0;i<frames;++i)for(unsigned channel=0;channel<2;++channel)WriteSample(bytes+i*client.format.stride+channel*client.format.bytes,client.format,frame.samples[static_cast<std::size_t>(i)*2+channel]);
            if(FAILED(hr=lease.Release())){result.error=Fail(hr,L"render_release_buffer");break;}
            result.frames+=frames;
        }
        if(result.error)break;
    }
    return result;
} catch(const std::bad_alloc&) {StreamResult result;result.error=Detail::MemoryFailure();return result;}
  catch(...) {StreamResult result;result.error=Detail::Fail(E_UNEXPECTED,L"audio_stream",L"An unexpected SDK error interrupted streaming.");return result;}
TestResult Test(const std::wstring& render_id, const std::wstring& capture_id,
                std::uint32_t duration_ms, HANDLE stop_event) try {
    using namespace Detail;
    TestResult result; result.render_id = render_id; result.capture_id = capture_id;
    if (duration_ms < 500 || duration_ms > 5000) { result.error = Fail(E_INVALIDARG,L"test_duration",L"Duration must be between 500 and 5000 milliseconds."); return result; }
    if (stop_event) {const auto stopped=WaitForSingleObject(stop_event,0);if(stopped==WAIT_OBJECT_0){result.cancelled=true;return result;}if(stopped==WAIT_FAILED){result.error=Fail(HRESULT_FROM_WIN32(GetLastError()),L"stop_event");return result;}}
    ComScope com;
    if (!com.valid()) { result.error = Fail(com.hr,L"initialize_com"); return result; }
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { result.error = Fail(hr,L"audio_enumerator"); return result; }
    ComPtr<IMMDevice> renderDevice, captureDevice;
    if (FAILED(hr = enumerator->GetDevice(render_id.c_str(),&renderDevice)) ||
        FAILED(hr = enumerator->GetDevice(capture_id.c_str(),&captureDevice))) { result.error = Fail(hr,L"get_test_endpoints"); return result; }
    const auto renderInfo = ReadEndpoint(renderDevice.Get(),enumerator.Get());
    const auto captureInfo = ReadEndpoint(captureDevice.Get(),enumerator.Get());
    if (!renderInfo.titan || !captureInfo.titan || renderInfo.direction != Direction::Playback || captureInfo.direction != Direction::Recording ||
        renderInfo.adapter_instance != captureInfo.adapter_instance) {
        result.error = Fail(E_ACCESSDENIED,L"verify_virtual_pair",L"Select the playback and recording endpoints of the same verified Titan Audio Link adapter. Physical microphones and unrelated endpoints are refused."); return result;
    }
    Client render, capture;
    if ((result.error = render.Open(renderDevice.Get())) || (result.error = capture.Open(captureDevice.Get()))) return result;
    ComPtr<IAudioRenderClient> output; ComPtr<IAudioCaptureClient> input;
    if (FAILED(hr = render.audio->GetService(IID_PPV_ARGS(&output))) || FAILED(hr = capture.audio->GetService(IID_PPV_ARGS(&input)))) { result.error = Fail(hr,L"get_audio_service"); return result; }
    const auto total = static_cast<std::size_t>(duration_ms) * 48;
    const auto signal = Signal(total);
    const auto rightSignal=Signal(total,0x52d371a9);
    const auto maxCaptured=total+72000;
    std::vector<float> received; received.reserve(maxCaptured);
    std::vector<float> receivedRight;receivedRight.reserve(maxCaptured);
    // At least one frame per packet/submission: reserve the strict worst case
    // before starting audio, so no timestamp/sample growth allocates while borrowed.
    std::vector<PacketStamp> captures, submissions; captures.reserve(maxCaptured); submissions.reserve(total);
    LARGE_INTEGER frequency{}, begin{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
    auto qpc100ns = [&]() { LARGE_INTEGER now{}; QueryPerformanceCounter(&now); return static_cast<std::uint64_t>((static_cast<double>(now.QuadPart)*10000000.0)/static_cast<double>(frequency.QuadPart)); };
    auto fill = [&](UINT32 frames) -> Error {
        BYTE* bytes = nullptr; HRESULT status = output->GetBuffer(frames,&bytes);
        if (FAILED(status)) return Fail(status,L"render_get_buffer");
        RenderLease lease{output.Get(),frames};
        const auto start = static_cast<std::size_t>(result.rendered_frames);
        submissions.push_back({start,qpc100ns()});
        for (UINT32 i = 0; i < frames; ++i) {
            const float left=start+i<signal.size()?signal[start+i]:0;
            const float right=start+i<rightSignal.size()?rightSignal[start+i]:0;
            WriteSample(bytes+i*render.format.stride,render.format,left);
            WriteSample(bytes+i*render.format.stride+render.format.bytes,render.format,right);
        }
        status = lease.Release();
        if (FAILED(status)) return Fail(status,L"render_release_buffer");
        result.rendered_frames += frames; return {};
    };
    // Silence is primed before starting either stream; test audio starts on a render event.
    BYTE* prime = nullptr;
    if (FAILED(hr = output->GetBuffer(render.buffer,&prime)) || FAILED(hr = output->ReleaseBuffer(render.buffer,AUDCLNT_BUFFERFLAGS_SILENT))) { result.error = Fail(hr,L"prime_render"); return result; }
    Mmcss mmcss;
    if ((result.error = capture.Start()) || (result.error = render.Start())) return result;
    const ULONGLONG deadline = GetTickCount64() + duration_ms + 1500;
    HANDLE events[] = {capture.event.value,render.event.value,stop_event};
    while (GetTickCount64() < deadline) {
        const DWORD wait = WaitForMultipleObjects(stop_event ? 3 : 2,events,FALSE,100);
        if (wait == WAIT_OBJECT_0 + 2) { result.cancelled = true; break; }
        if (wait == WAIT_FAILED) { result.error = Fail(HRESULT_FROM_WIN32(GetLastError()),L"wait_audio_events"); break; }
        if (wait == WAIT_OBJECT_0 + 1 && result.rendered_frames < total) {
            UINT32 padding = 0;
            if (FAILED(hr = render.audio->GetCurrentPadding(&padding))) { result.error = Fail(hr,L"render_padding"); break; }
            const auto frames = static_cast<UINT32>(std::min<std::size_t>(render.buffer - std::min(render.buffer,padding),total-static_cast<std::size_t>(result.rendered_frames)));
            if (frames && (result.error = fill(frames))) break;
        }
        // Drain every available capture packet, including on render notifications.
        for (;;) {
            if(GetTickCount64()>=deadline)break;
            if(stop_event && WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0){result.cancelled=true;break;}
            UINT32 packet = 0;
            if (FAILED(hr = input->GetNextPacketSize(&packet))) { result.error = Fail(hr,L"capture_packet_size"); break; }
            if (!packet) break;
            BYTE* bytes = nullptr; UINT32 frames = 0; DWORD flags = 0; UINT64 position = 0, timestamp = 0;
            if (FAILED(hr = input->GetBuffer(&bytes,&frames,&flags,&position,&timestamp))) { result.error = Fail(hr,L"capture_get_buffer"); break; }
            if(hr==AUDCLNT_S_BUFFER_EMPTY || !frames)break;
            CaptureLease lease{input.Get(),frames};
            if(frames>capture.buffer){result.error=Fail(E_UNEXPECTED,L"capture_buffer_bound",L"Capture packet exceeded the negotiated audio buffer.");break;}
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++result.discontinuities;
            if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ++result.timestamp_errors;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) ++result.silent_packets;
            if (received.size() + frames <= maxCaptured) {
                if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) captures.push_back({received.size(),timestamp});
                for (UINT32 i = 0; i < frames; ++i) {
                    received.push_back((flags&AUDCLNT_BUFFERFLAGS_SILENT)?0:ReadSample(bytes+i*capture.format.stride,capture.format));
                    receivedRight.push_back((flags&AUDCLNT_BUFFERFLAGS_SILENT)?0:ReadSample(bytes+i*capture.format.stride+capture.format.bytes,capture.format));
                }
            }
            result.captured_frames += frames;
            if (FAILED(hr = lease.Release())) { result.error = Fail(hr,L"capture_release_buffer"); break; }
        }
        if (result.error || result.cancelled) break;
        if (result.rendered_frames >= total && received.size() >= total + 24000) break;
    }
    render.audio->Stop(); render.started = false; capture.audio->Stop(); capture.started = false;
    LARGE_INTEGER end{}; QueryPerformanceCounter(&end);
    result.elapsed_ms = static_cast<double>(end.QuadPart-begin.QuadPart)*1000/static_cast<double>(frequency.QuadPart);
    const auto match = Analyze(signal,received);
    const auto rightMatch=Analyze(rightSignal,receivedRight);
    const auto full=ValidateFull(signal,received,match),rightFull=ValidateFull(rightSignal,receivedRight,rightMatch);
    result.matched_channels=static_cast<unsigned>(full.valid)+static_cast<unsigned>(rightFull.valid);
    result.matched=full.valid && rightFull.valid && full.offset==rightFull.offset;
    result.correlation=std::min(full.correlation,rightFull.correlation);
    result.capture_rms=std::sqrt((full.rms*full.rms+rightFull.rms*rightFull.rms)/2);
    result.max_sample_error=std::max(full.maxError,rightFull.maxError);
    if (result.matched) {
        result.verified_frames=total;
        result.sample_offset_frames = static_cast<std::int64_t>(full.offset);
        result.sample_offset_ms = static_cast<double>(result.sample_offset_frames)/48;
        const PacketStamp* submitted = nullptr; const PacketStamp* captured = nullptr;
        for (const auto& p : submissions) { if (p.frame == 0) {submitted=&p;break;} }
        for (const auto& p : captures) { if (p.frame <= full.offset) captured = &p; else break; }
        if (submitted && captured) {
            const double sampleQpc = static_cast<double>(captured->qpc100ns) + static_cast<double>(full.offset-captured->frame)*10000000.0/48000.0;
            result.observed_delivery_ms = (sampleQpc-static_cast<double>(submitted->qpc100ns))/10000;
        }
    } else if (!result.error && !result.cancelled) result.error = Fail(HRESULT_FROM_WIN32(ERROR_NO_DATA),L"validate_cable_signal",L"The paired virtual recording endpoint did not preserve both complete stereo fixtures at one matching sample offset. Every frame is required, with whole-sequence correlation, sign and PCM16 conversion-error bounds. Inspect dropped/shifted samples, channel routing, levels, and packet discontinuities.");
    return result;
} catch(const std::bad_alloc&) {TestResult result;result.error=Detail::MemoryFailure();return result;}
  catch(...) {TestResult result;result.error=Detail::Fail(E_UNEXPECTED,L"audio_test",L"An unexpected SDK error interrupted virtual cable verification.");return result;}
} // AudioLink
