#include "examples/low_latency_sender/peer_connection_app.h"

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/make_ref_counted.h"
#include "api/rtp_sender_interface.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "json/reader.h"
#include "json/value.h"
#include "json/writer.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/json.h"
#include "system_wrappers/include/clock.h"
#include "test/frame_generator_capturer.h"
#include "api/test/create_frame_generator.h"

namespace {

const char kStreamId[] = "native_stream";
const char kVideoLabel[] = "native_video";

class DummySetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
 public:
  static webrtc::scoped_refptr<DummySetSessionDescriptionObserver> Create() {
    return webrtc::make_ref_counted<DummySetSessionDescriptionObserver>();
  }
  void OnSuccess() override { std::cout << "SetSessionDescription success\n"; }
  void OnFailure(webrtc::RTCError error) override {
    std::cerr << "SetSessionDescription failed: " << error.message() << "\n";
  }
};

class SyntheticTrackSource : public webrtc::VideoTrackSource {
 public:
  static webrtc::scoped_refptr<SyntheticTrackSource> Create(webrtc::TaskQueueFactory& task_queue_factory,
                                                             const VideoSourceConfig& config) {
    auto generator = webrtc::test::CreateSquareFrameGenerator(config.width, config.height, std::nullopt, std::nullopt);
    auto capturer = std::make_unique<webrtc::test::FrameGeneratorCapturer>(
        webrtc::Clock::GetRealTimeClock(), std::move(generator), config.fps, task_queue_factory);
    capturer->Start();
    return webrtc::make_ref_counted<SyntheticTrackSource>(std::move(capturer));
  }

 protected:
  explicit SyntheticTrackSource(std::unique_ptr<webrtc::test::FrameGeneratorCapturer> capturer)
      : VideoTrackSource(false), capturer_(std::move(capturer)) {}
  ~SyntheticTrackSource() override = default;

 private:
  webrtc::VideoSourceInterface<webrtc::VideoFrame>* source() override { return capturer_.get(); }
  std::unique_ptr<webrtc::test::FrameGeneratorCapturer> capturer_;
};

bool ParseJson(const std::string& text, Json::Value* value) {
  Json::CharReaderBuilder factory;
  std::unique_ptr<Json::CharReader> reader = absl::WrapUnique(factory.newCharReader());
  std::string errors;
  return reader->parse(text.data(), text.data() + text.size(), value, &errors);
}

std::string WriteJson(const Json::Value& value) {
  Json::StreamWriterBuilder factory;
  factory["indentation"] = "";
  return Json::writeString(factory, value);
}

}  // namespace

PeerConnectionApp::PeerConnectionApp(const webrtc::Environment& env,
                                     std::string room,
                                     VideoSourceConfig video_config,
                                     SendCallback send)
    : env_(env), room_(std::move(room)), video_config_(std::move(video_config)), send_(std::move(send)) {}

PeerConnectionApp::~PeerConnectionApp() { Close(); }

bool PeerConnectionApp::Initialize() {
  signaling_thread_ = webrtc::Thread::CreateWithSocketServer();
  signaling_thread_->Start();

  webrtc::PeerConnectionFactoryDependencies deps;
  deps.signaling_thread = signaling_thread_.get();
  deps.env = env_;
  deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
  deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
  deps.video_encoder_factory = std::make_unique<webrtc::VideoEncoderFactoryTemplate<
      webrtc::LibvpxVp8EncoderTemplateAdapter,
      webrtc::LibvpxVp9EncoderTemplateAdapter,
      webrtc::OpenH264EncoderTemplateAdapter,
      webrtc::LibaomAv1EncoderTemplateAdapter>>();
  deps.video_decoder_factory = std::make_unique<webrtc::VideoDecoderFactoryTemplate<
      webrtc::LibvpxVp8DecoderTemplateAdapter,
      webrtc::LibvpxVp9DecoderTemplateAdapter,
      webrtc::OpenH264DecoderTemplateAdapter,
      webrtc::Dav1dDecoderTemplateAdapter>>();
  webrtc::EnableMedia(deps);
  factory_ = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
  if (!factory_) {
    std::cerr << "CreateModularPeerConnectionFactory failed\n";
    return false;
  }
  return CreatePeerConnection();
}

void PeerConnectionApp::Close() {
  peer_ = nullptr;
  video_source_ = nullptr;
  factory_ = nullptr;
  if (signaling_thread_) {
    signaling_thread_->Stop();
    signaling_thread_.reset();
  }
}

bool PeerConnectionApp::CreatePeerConnection() {
  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  webrtc::PeerConnectionInterface::IceServer stun;
  stun.uri = "stun:stun.l.google.com:19302";
  config.servers.push_back(stun);

  webrtc::PeerConnectionDependencies deps(this);
  auto result = factory_->CreatePeerConnectionOrError(config, std::move(deps));
  if (!result.ok()) {
    std::cerr << "CreatePeerConnection failed: " << result.error().message() << "\n";
    return false;
  }
  peer_ = std::move(result.value());
  return AddVideoTrack();
}

bool PeerConnectionApp::AddVideoTrack() {
  if (video_config_.source != "synthetic") {
    std::cerr << "Unsupported video source: " << video_config_.source
              << ". Current build supports --source synthetic.\n";
    return false;
  }

  video_source_ = SyntheticTrackSource::Create(env_.task_queue_factory(), video_config_);
  auto video_track = factory_->CreateVideoTrack(video_source_, kVideoLabel);
  auto result = peer_->AddTrack(video_track, {kStreamId});
  if (!result.ok()) {
    std::cerr << "AddTrack failed: " << result.error().message() << "\n";
    return false;
  }

  std::cout << "Synthetic video track added: " << video_config_.width << "x" << video_config_.height
            << "@" << video_config_.fps << "fps\n" << std::flush;
  return true;
}

void PeerConnectionApp::HandleSignalingMessage(const std::string& text) {
  Json::Value message;
  if (!ParseJson(text, &message)) {
    std::cerr << "Invalid signaling JSON: " << text << "\n";
    return;
  }
  const std::string type = message.get("type", "").asString();
  if (type == "hello") {
    std::cout << "signaling hello: " << message.get("clientId", "").asString() << "\n" << std::flush;
  } else if (type == "joined") {
    std::cout << "joined room: " << message.get("room", "").asString() << "\n" << std::flush;
  } else if (type == "offer") {
    HandleOffer(message);
  } else if (type == "candidate") {
    HandleCandidate(message);
  } else if (type == "peer-left") {
    std::cout << "peer left\n";
  } else if (type == "error") {
    std::cerr << "signaling error: " << message.get("message", "").asString() << "\n";
  }
}

void PeerConnectionApp::HandleOffer(const Json::Value& message) {
  target_peer_id_ = message.get("from", "").asString();
  const Json::Value sdp_object = message["sdp"];
  const std::string sdp = sdp_object.get("sdp", "").asString();
  if (sdp.empty()) {
    std::cerr << "offer without sdp\n";
    return;
  }

  webrtc::SdpParseError error;
  std::unique_ptr<webrtc::SessionDescriptionInterface> desc =
      webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp, &error);
  if (!desc) {
    std::cerr << "CreateSessionDescription failed: " << error.description << "\n";
    return;
  }
  std::cout << "offer received from " << target_peer_id_ << ", creating answer\n" << std::flush;
  peer_->SetRemoteDescription(DummySetSessionDescriptionObserver::Create().get(), desc.release());
  peer_->CreateAnswer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
}

void PeerConnectionApp::HandleCandidate(const Json::Value& message) {
  const Json::Value c = message["candidate"];
  const std::string candidate_sdp = c.get("candidate", "").asString();
  if (candidate_sdp.empty()) return;
  const std::string sdp_mid = c.get("sdpMid", "0").asString();
  const int sdp_mline_index = c.get("sdpMLineIndex", 0).asInt();
  webrtc::SdpParseError error;
  std::unique_ptr<webrtc::IceCandidateInterface> candidate(
      webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate_sdp, &error));
  if (!candidate) {
    std::cerr << "CreateIceCandidate failed: " << error.description << "\n";
    return;
  }
  if (!peer_->AddIceCandidate(candidate.get())) {
    std::cerr << "AddIceCandidate failed\n";
  }
}

void PeerConnectionApp::OnIceCandidate(const webrtc::IceCandidate* candidate) {
  Json::Value c;
  c["candidate"] = candidate->ToString();
  c["sdpMid"] = candidate->sdp_mid();
  c["sdpMLineIndex"] = candidate->sdp_mline_index();

  Json::Value message;
  message["type"] = "candidate";
  message["room"] = room_;
  if (!target_peer_id_.empty()) message["target"] = target_peer_id_;
  message["candidate"] = c;
  send_(WriteJson(message));
  std::cout << "candidate sent\n" << std::flush;
}

void PeerConnectionApp::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state) {
  std::cout << "ice connection state: " << state << "\n" << std::flush;
}

void PeerConnectionApp::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState state) {
  std::cout << "ice gathering state: " << state << "\n" << std::flush;
}

void PeerConnectionApp::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
  peer_->SetLocalDescription(DummySetSessionDescriptionObserver::Create().get(), desc);
  SendAnswer(desc);
}

void PeerConnectionApp::OnFailure(webrtc::RTCError error) {
  std::cerr << "Create answer failed: " << error.message() << "\n";
}

void PeerConnectionApp::SendAnswer(webrtc::SessionDescriptionInterface* desc) {
  std::string sdp;
  desc->ToString(&sdp);
  Json::Value sdp_object;
  sdp_object["type"] = "answer";
  sdp_object["sdp"] = sdp;

  Json::Value message;
  message["type"] = "answer";
  message["room"] = room_;
  if (!target_peer_id_.empty()) message["target"] = target_peer_id_;
  message["sdp"] = sdp_object;
  send_(WriteJson(message));
  std::cout << "answer sent\n" << std::flush;
}