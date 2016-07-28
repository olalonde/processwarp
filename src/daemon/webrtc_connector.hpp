#pragma once

#include <webrtc/api/peerconnectioninterface.h>

#include <deque>
#include <string>

#include "lock.hpp"
#include "node_id.hpp"

namespace processwarp {
class WebrtcConnector;

class WebrtcConnectorDelegate {
 public:
  virtual ~WebrtcConnectorDelegate();
  virtual void webrtc_connector_on_change_stateus(WebrtcConnector& connector, bool is_connect) = 0;
  virtual void webrtc_connector_on_update_ice(WebrtcConnector& connector,
                                              const std::string& ice) = 0;
  virtual void webrtc_connector_on_recv(WebrtcConnector& connector, const std::string& data);
};

class WebrtcConnector {
 public:
  /// Opposide peer's node-id.
  NodeID nid;
  /// Connecting status.
  bool is_connected;
  /// Event handler.
  WebrtcConnectorDelegate* delegate;
  /// Received data retention pool (data is stored when the delegate is not defined).
  std::deque<std::string> retention_data;

  WebrtcConnector(
      rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory,
      webrtc::PeerConnectionInterface::RTCConfiguration pc_config,
      webrtc::DataChannelInit dc_config);
  virtual ~WebrtcConnector();

  const std::string& get_local_sdp();
  void send(const std::string& data);
  void set_remote_sdp(const std::string& sdp);
  void update_ice(const std::string& ice);

 private:
  class CSDO : public webrtc::CreateSessionDescriptionObserver {
   public:
    explicit CSDO(WebrtcConnector& parent_);

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
    void OnFailure(const std::string& error) override;
    int AddRef() const override;
    int Release() const override;

   private:
    WebrtcConnector& parent;
  };

  class DCO : public webrtc::DataChannelObserver {
   public:
    explicit DCO(WebrtcConnector& parent_);

    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer& buffer) override;
    void OnBufferedAmountChange(uint64_t previous_amount) override;

   private:
    WebrtcConnector& parent;
  };

  class PCO : public webrtc::PeerConnectionObserver {
   public:
    explicit PCO(WebrtcConnector& parent_);

    void OnAddStream(webrtc::MediaStreamInterface* stream) override;
    void OnDataChannel(webrtc::DataChannelInterface* data_channel) override;
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
    void OnIceConnectionChange(
        webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
    void OnIceGatheringChange(
        webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
    void OnRemoveStream(webrtc::MediaStreamInterface* stream) override;
    void OnRenegotiationNeeded() override;
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override;

   private:
    WebrtcConnector& parent;
  };

  class SSDO : public webrtc::SetSessionDescriptionObserver {
   public:
    explicit SSDO(WebrtcConnector& parent_);

    void OnSuccess() override;
    void OnFailure(const std::string& error) override;
    int AddRef() const override;
    int Release() const override;

   private:
    WebrtcConnector& parent;
  };

  CSDO csdo;
  DCO  dco;
  PCO  pco;
  SSDO ssdo;

  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection;
  rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel;

  Lock::Mutex mutex;
  Lock::Cond cond;
  bool is_remote_sdp_set;
  /// SDP of local peer.
  std::string local_sdp;

  void on_csd_success(webrtc::SessionDescriptionInterface* desc);
  void on_csd_failure(const std::string& error);
  void on_ice_candidate(const webrtc::IceCandidateInterface* candidate);
  void on_pco_connection_change(webrtc::PeerConnectionInterface::IceConnectionState status);
  void on_ssd_failure(const std::string& error);
  void on_state_change(webrtc::DataChannelInterface::DataState status);
  void on_message(const webrtc::DataBuffer& buffer);
};
}  // namespace processwarp
