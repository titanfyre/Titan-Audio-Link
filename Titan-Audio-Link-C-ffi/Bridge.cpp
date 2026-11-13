#include "SDK/AudioLink.h"
#include "WireTypes.h"
#include <map>
struct ProductSession{

Json Error(const AudioLink::Error& e){return {{"code",static_cast<std::int32_t>(e.code)},{"stage",Utf8(e.stage)},{"message",Utf8(e.message)}};}
void Check(const AudioLink::Error& e){if(e)throw SdkError(static_cast<unsigned>(e.code),Utf8(AudioLink::Describe(e)));}

Json Invoke(const std::string& op,const Json& a){

if(op=="check"){Require(a.contains("frames"),"Expected frames");auto frames=Number(a,"frames",0,48000);auto channels=Number(a,"channels",2,32);Require(channels>0,"Channels must be positive");auto samples=a.at("samples");Require(samples.is_array() && samples.size()<=48000*32,"Too many samples");for(const auto& value:samples)Require(value.is_number() && std::isfinite(value.get<double>()),"Audio samples must be finite");return {{"valid",samples.size()==static_cast<std::size_t>(frames)*channels}};}
if(op=="list"){auto inventory=AudioLink::List(a.value("include_other_audio",false));Check(inventory.error);Json endpoints=Json::array();for(const auto& e:inventory.endpoints)endpoints.push_back({{"id",Utf8(e.id)},{"name",Utf8(e.name)},{"adapter_instance",Utf8(e.adapter_instance)},{"direction",static_cast<unsigned>(e.direction)},{"state",e.state},{"titan",e.titan},{"sample_rate",e.sample_rate},{"channels",e.channels},{"bits_per_sample",e.bits_per_sample},{"default_period_ms",e.default_period_ms},{"minimum_period_ms",e.minimum_period_ms},{"format_error",Error(e.format_error)}});return endpoints;}
if(op=="test"){auto result=AudioLink::Test(Wide(a.at("render_id").get<std::string>()),Wide(a.at("capture_id").get<std::string>()),Number(a,"duration_ms",1500,5000));Check(result.error);return {{"cancelled",result.cancelled},{"matched",result.matched},{"matched_channels",result.matched_channels},{"verified_frames",std::to_string(result.verified_frames)},{"rendered_frames",std::to_string(result.rendered_frames)},{"captured_frames",std::to_string(result.captured_frames)},{"elapsed_ms",result.elapsed_ms},{"capture_rms",result.capture_rms},{"correlation",result.correlation},{"max_sample_error",result.max_sample_error},{"sample_offset_frames",std::to_string(result.sample_offset_frames)},{"sample_offset_ms",result.sample_offset_ms},{"observed_delivery_ms",result.observed_delivery_ms},{"render_id",Utf8(result.render_id)},{"capture_id",Utf8(result.capture_id)}};}
if(op=="stream_chunk"){
 auto samples=a.value("samples",std::vector<float>());Require(samples.size()<=48000*32,"Too many playback samples");for(float s:samples)Require(std::isfinite(s),"Audio samples must be finite");std::size_t offset=0;Json captured=Json::array();
 auto result=AudioLink::Stream(Wide(a.at("endpoint_id").get<std::string>()),[&](AudioLink::Frame& frame){if(frame.direction==AudioLink::Direction::Playback){for(auto& sample:frame.samples)sample=offset<samples.size()?samples[offset++]:0.f;}else{if(captured.size()>=128)return false;captured.push_back({{"frames",frame.frames},{"position",std::to_string(frame.position)},{"qpc_100ns",std::to_string(frame.qpc_100ns)},{"flags",frame.flags},{"samples",frame.samples}});}return true;},nullptr,Number(a,"duration_ms",100,100));Check(result.error);return {{"frames",std::to_string(result.frames)},{"cancelled",result.cancelled},{"discontinuities",result.discontinuities},{"timestamp_errors",result.timestamp_errors},{"captured",captured}};
}

throw SdkError(50,"Unknown operation");}
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned titan_audio_link_json_open(std::uint64_t* id)noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned titan_audio_link_json_close(std::uint64_t id)noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* titan_audio_link_json_call(std::uint64_t id,const char* request,std::uint32_t length)noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void titan_audio_link_json_free(char* response)noexcept{delete[] response;}
