#pragma once

#include <functional>
#include <memory>
#include <string>

#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "json/value.h"
#include "pc/video_track_source.h"
#include "rtc_base/thread.h"

namespace webrtc {
class TaskQueueFactory;
}

struct VideoSourceConfig {
  std::string source = "synthetic";
  int width = 640;
  int height = 480;
  int fps = 30;
};

class PeerConnectionApp : public webrtc::PeerConnectionObserver,
                          public webrtc::CreateSessionDescriptionObserver {
 public:
  using SendCallback = std::function<void(const std::string&)>;

  PeerConnectionApp(const webrtc::Environment& env,
                    std::string room,
                    VideoSourceConfig video_config,
                    SendCallback send);

  bool Initialize();
  void Close();
  void HandleSignalingMessage(const std::string& text);

  void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnAddTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                  const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override {}
  void OnRemoveTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}
  void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
  void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override;
  void OnIceConnectionReceivingChange(bool receiving) override {}
  void OnIceCandidateRemoved(const webrtc::IceCandidate* candidate) override {}

  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  void OnFailure(webrtc::RTCError error) override;

 protected:
  ~PeerConnectionApp() override;

 private:
  bool CreatePeerConnection();
  bool AddVideoTrack();
  void HandleOffer(const Json::Value& message);
  void HandleCandidate(const Json::Value& message);
  void SendAnswer(webrtc::SessionDescriptionInterface* desc);

  const webrtc::Environment env_;
  std::string room_;
  VideoSourceConfig video_config_;
  std::string target_peer_id_;
  SendCallback send_;
  std::unique_ptr<webrtc::Thread> signaling_thread_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_;
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
};