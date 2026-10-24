// White-box verification keeps the distributed SDK in exactly two source files.
#include "../SDK/AudioLink.cpp"
#include <cstdio>
#include <random>
#include <stdexcept>

namespace {
unsigned checks=0;
void Check(bool condition) { ++checks; if(!condition) throw std::runtime_error("Audio invariant failed"); }
WAVEFORMATEX Pcm(WORD channels,WORD bits,DWORD rate=48000) {
    return {WAVE_FORMAT_PCM,channels,rate,rate*channels*(bits/8),static_cast<WORD>(channels*(bits/8)),bits,0};
}
}
int main() {
    using namespace AudioLink::Detail;
    try {
        constexpr wchar_t owned[]=L"ROOT\\TitanAudioLink\0";
        constexpr wchar_t other[]=L"ROOT\\OtherAudio\0";
        constexpr wchar_t list[]=L"ROOT\\OtherAudio\0ROOT\\TitanAudioLink\0";
        constexpr wchar_t prefix[]=L"ROOT\\TitanAudioLinkEvil\0";
        Check(RootProof(L"ROOT\\MEDIA\\0000",owned,std::size(owned),L"TitanAudioLink"));
        Check(RootProof(L"root\\titAnAudIoLink\\0001",list,std::size(list),L"TITANAUDIOLINK"));
        Check(!RootProof(L"USB\\MEDIA\\0000",owned,std::size(owned),L"TitanAudioLink"));
        Check(!RootProof(L"ROOT\\MEDIA\\0000",other,std::size(other),L"TitanAudioLink"));
        Check(!RootProof(L"ROOT\\MEDIA\\0000",prefix,std::size(prefix),L"TitanAudioLink"));
        Check(!RootProof(L"ROOT\\MEDIA\\0000",owned,std::size(owned),L"OtherService"));
        Check(!RootProof(L"ROOT\\MEDIA\\0000",owned,std::size(owned)-1,L"TitanAudioLink"));
        Check(!RootProof(nullptr,owned,std::size(owned),L"TitanAudioLink"));
        Check(!RootProof(L"ROOT\\MEDIA\\0000",nullptr,0,L"TitanAudioLink"));
        Check(!RootProof(L"ROOT\\MEDIA\\0000",owned,std::size(owned),nullptr));
        Format f;
        auto wave=Pcm(2,16); Check(ParseFormat(wave,f)); Check(f.stride==4 && f.rate==48000 && !f.floating);
        for(WORD channels=0;channels<18;++channels) {
            for(WORD bits=0;bits<=64;++bits) {
                wave=Pcm(channels,bits); const bool accepted=ParseFormat(wave,f);
                const bool expected=channels>=1 && channels<=8 && (bits==16 || bits==24 || bits==32);
                Check(accepted==expected);
            }
        }
        wave=Pcm(2,16); wave.nBlockAlign=1; Check(!ParseFormat(wave,f));
        wave=Pcm(2,16); wave.nAvgBytesPerSec=1; Check(!ParseFormat(wave,f));
        wave=Pcm(2,16,0); Check(!ParseFormat(wave,f));
        WAVEFORMATEXTENSIBLE ext{}; ext.Format=Pcm(2,32); ext.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;
        ext.Format.cbSize=sizeof(ext)-sizeof(WAVEFORMATEX); ext.SubFormat=KSDATAFORMAT_SUBTYPE_PCM;
        ext.Samples.wValidBitsPerSample=24; Check(ParseFormat(ext.Format,f));
        Check(f.valid_bits==24);
        BYTE bytes[4]{}; WriteSample(bytes,f,-0.5f); Check(std::abs(ReadSample(bytes,f)+0.5f)<0.000001f); Check(bytes[0]==0);
        ext.Samples.wValidBitsPerSample=0; Check(!ParseFormat(ext.Format,f));
        ext.Samples.wValidBitsPerSample=33; Check(!ParseFormat(ext.Format,f));
        ext.Samples.wValidBitsPerSample=32; ext.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT; Check(ParseFormat(ext.Format,f));
        ext.Format.cbSize=0; Check(!ParseFormat(ext.Format,f));
        std::mt19937 random(67319); std::uniform_real_distribution<float> range(-1,1);
        for(const WORD bits:{WORD{16},WORD{24},WORD{32}}) {
            wave=Pcm(2,bits); Check(ParseFormat(wave,f));
            for(unsigned i=0;i<20000;++i) { const float sample=range(random); WriteSample(bytes,f,sample); Check(std::abs(ReadSample(bytes,f)-sample)<0.00004f); }
        }
        wave=Pcm(2,32); wave.wFormatTag=WAVE_FORMAT_IEEE_FLOAT; Check(ParseFormat(wave,f));
        for(unsigned i=0;i<20000;++i) { const float sample=range(random); WriteSample(bytes,f,sample); Check(ReadSample(bytes,f)==sample); }
        const float nan=std::numeric_limits<float>::quiet_NaN(); std::memcpy(bytes,&nan,4); Check(ReadSample(bytes,f)==0);
        WriteSample(bytes,f,nan);Check(ReadSample(bytes,f)==0);
        WriteSample(bytes,f,99);Check(ReadSample(bytes,f)==1);
        WriteSample(bytes,f,-99);Check(ReadSample(bytes,f)==-1);
        const auto source=Signal(48000); Check(source==Signal(48000)); Check(source.size()==48000);
        const auto rightSource=Signal(48000,0x52d371a9);Check(source!=rightSource);
        Check(Analyze(rightSource,rightSource).found);Check(!Analyze(rightSource,source).found);Check(!Analyze(source,rightSource).found);
        auto complete=source;complete.insert(complete.begin(),480,0);const auto completeAnchor=Analyze(source,complete);const auto completeFull=ValidateFull(source,complete,completeAnchor);Check(completeFull.valid&&completeFull.covered&&completeFull.offset==480);
        auto lateDrop=complete;lateDrop.erase(lateDrop.begin()+12000);lateDrop.push_back(0);const auto dropAnchor=Analyze(source,lateDrop);Check(dropAnchor.found&&dropAnchor.correlation>0.9999);Check(!ValidateFull(source,lateDrop,dropAnchor).valid);
        auto lateShift=complete;lateShift.insert(lateShift.begin()+20000,lateShift[19999]);lateShift.pop_back();Check(Analyze(source,lateShift).found);Check(!ValidateFull(source,lateShift,Analyze(source,lateShift)).valid);
        auto lateCorrupt=complete;lateCorrupt[35000]=-lateCorrupt[35000];Check(Analyze(source,lateCorrupt).found);Check(!ValidateFull(source,lateCorrupt,Analyze(source,lateCorrupt)).valid);
        auto lateGain=complete;lateGain[35000]*=0.5f;Check(!ValidateFull(source,lateGain,Analyze(source,lateGain)).valid);
        auto shortCapture=complete;shortCapture.resize(47000);Check(Analyze(source,shortCapture).found);Check(!ValidateFull(source,shortCapture,Analyze(source,shortCapture)).covered);
        auto missingStart=source;missingStart.erase(missingStart.begin(),missingStart.begin()+480);Check(Analyze(source,missingStart).found);Check(!ValidateFull(source,missingStart,Analyze(source,missingStart)).covered);
        auto constantGain=complete;for(auto& sample:constantGain)sample*=0.25f;Check(ValidateFull(source,constantGain,Analyze(source,constantGain)).valid);
        auto noisy=complete;for(std::size_t i=480;i<noisy.size();++i)noisy[i]+=(i&1)?1.0f/32768:-1.0f/32768;Check(ValidateFull(source,noisy,Analyze(source,noisy)).valid);
        std::uint32_t hash=2166136261u;
        for(float sample:source) { Check(std::abs(sample)==0.12f); hash=(hash^static_cast<unsigned>(sample>0))*16777619u; }
        Check(hash==0x14fc36c8u); // Independently pinned with a Python uint32 reference.
        for(std::size_t offset:{std::size_t{0},std::size_t{1},std::size_t{31},std::size_t{480},std::size_t{9001}}) {
            std::vector<float> captured(offset,0); captured.insert(captured.end(),source.begin(),source.end());
            const auto match=Analyze(source,captured); Check(match.found); Check(match.captured-match.source==offset); Check(match.correlation>0.9999);
        }
        auto gain=source; for(auto& sample:gain) sample*=0.25f;
        Check(Analyze(source,gain).found);
        std::vector<float> silence(source.size(),0); Check(!Analyze(source,silence).found);
        auto inverted=source; for(auto& sample:inverted) sample=-sample; Check(!Analyze(source,inverted).found);
        Check(!Analyze({},source).found); Check(!Analyze(source,{}).found);
        std::vector<float> truncated(source.begin()+8100,source.begin()+12000);
        const auto partial=Analyze(source,truncated); Check(partial.found); Check(partial.source==8100 && partial.captured==0);
        auto unrelated=Signal(48000); for(auto& sample:unrelated) sample=range(random); Check(!Analyze(source,unrelated).found);
        const auto invalid=AudioLink::Test(L"invalid",L"invalid",0); Check(invalid.error.code==E_INVALIDARG);
        const auto tooLong=AudioLink::Test(L"invalid",L"invalid",5001); Check(tooLong.error.code==E_INVALIDARG);
        Check(AudioLink::Stream(L"invalid",{}).error.code==E_INVALIDARG);
        Check(AudioLink::Stream(L"invalid",[](AudioLink::Frame&){return true;},nullptr,60001).error.code==E_INVALIDARG);
        AudioLink::Frame frame;frame.samples.resize(4);Check(ValidPlaybackFrame(frame,4));Check(!ValidPlaybackFrame(frame,6));frame.samples.clear();Check(!ValidPlaybackFrame(frame,4));
        HANDLE stop=CreateEventW(nullptr,TRUE,TRUE,nullptr);Check(stop!=nullptr);
        Check(AudioLink::Test(L"not-opened",L"not-opened",500,stop).cancelled);
        Check(AudioLink::Stream(L"not-opened",[](AudioLink::Frame&){return true;},stop).cancelled);CloseHandle(stop);
        Check(!AudioLink::Describe(invalid.error).empty());
        std::printf("%u PCM, deterministic signal, correlation and guard checks passed. No audio endpoints opened.\n",checks); return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"After %u checks: %s\n",checks,error.what()); return 1; }
}
