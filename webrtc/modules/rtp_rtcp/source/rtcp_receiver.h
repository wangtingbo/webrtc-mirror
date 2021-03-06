/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_
#define WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_

#include <map>
#include <set>
#include <vector>

#include "webrtc/base/criticalsection.h"
#include "webrtc/base/thread_annotations.h"
#include "webrtc/modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "webrtc/modules/rtp_rtcp/source/rtcp_receiver_help.h"
#include "webrtc/modules/rtp_rtcp/source/rtcp_utility.h"
#include "webrtc/typedefs.h"

namespace webrtc {
namespace rtcp {
class CommonHeader;
struct ReceiveTimeInfo;
class ReportBlock;
class Rrtr;
class TmmbItem;
}  // namespace rtcp

class RTCPReceiver {
 public:
  class ModuleRtpRtcp {
   public:
    virtual void SetTmmbn(std::vector<rtcp::TmmbItem> bounding_set) = 0;
    virtual void OnRequestSendReport() = 0;
    virtual void OnReceivedNack(
        const std::vector<uint16_t>& nack_sequence_numbers) = 0;
    virtual void OnReceivedRtcpReportBlocks(
        const ReportBlockList& report_blocks) = 0;

   protected:
    virtual ~ModuleRtpRtcp() = default;
  };

  RTCPReceiver(Clock* clock,
               bool receiver_only,
               RtcpPacketTypeCounterObserver* packet_type_counter_observer,
               RtcpBandwidthObserver* rtcp_bandwidth_observer,
               RtcpIntraFrameObserver* rtcp_intra_frame_observer,
               TransportFeedbackObserver* transport_feedback_observer,
               ModuleRtpRtcp* owner);
  virtual ~RTCPReceiver();

  bool IncomingPacket(const uint8_t* packet, size_t packet_size);

  int64_t LastReceivedReceiverReport() const;

  void SetSsrcs(uint32_t main_ssrc, const std::set<uint32_t>& registered_ssrcs);
  void SetRemoteSSRC(uint32_t ssrc);
  uint32_t RemoteSSRC() const;

  // get received cname
  int32_t CNAME(uint32_t remoteSSRC, char cName[RTCP_CNAME_SIZE]) const;

  // get received NTP
  bool NTP(uint32_t* ReceivedNTPsecs,
           uint32_t* ReceivedNTPfrac,
           uint32_t* RTCPArrivalTimeSecs,
           uint32_t* RTCPArrivalTimeFrac,
           uint32_t* rtcp_timestamp) const;

  bool LastReceivedXrReferenceTimeInfo(RtcpReceiveTimeInfo* info) const;

  // get rtt
  int32_t RTT(uint32_t remoteSSRC,
              int64_t* RTT,
              int64_t* avgRTT,
              int64_t* minRTT,
              int64_t* maxRTT) const;

  int32_t SenderInfoReceived(RTCPSenderInfo* senderInfo) const;

  void SetRtcpXrRrtrStatus(bool enable);
  bool GetAndResetXrRrRtt(int64_t* rtt_ms);

  // get statistics
  int32_t StatisticsReceived(std::vector<RTCPReportBlock>* receiveBlocks) const;

  // Returns true if we haven't received an RTCP RR for several RTCP
  // intervals, but only triggers true once.
  bool RtcpRrTimeout(int64_t rtcp_interval_ms);

  // Returns true if we haven't received an RTCP RR telling the receive side
  // has not received RTP packets for too long, i.e. extended highest sequence
  // number hasn't increased for several RTCP intervals. The function only
  // returns true once until a new RR is received.
  bool RtcpRrSequenceNumberTimeout(int64_t rtcp_interval_ms);

  std::vector<rtcp::TmmbItem> TmmbrReceived() const;

  bool UpdateRTCPReceiveInformationTimers();

  std::vector<rtcp::TmmbItem> BoundingSet(bool* tmmbr_owner);

  void UpdateTmmbr();

  void RegisterRtcpStatisticsCallback(RtcpStatisticsCallback* callback);
  RtcpStatisticsCallback* GetRtcpStatisticsCallback();

 private:
  using ReceivedInfoMap = std::map<uint32_t, RTCPHelp::RTCPReceiveInformation*>;
  // RTCP report block information mapped by remote SSRC.
  using ReportBlockInfoMap =
      std::map<uint32_t, RTCPHelp::RTCPReportBlockInformation*>;
  // RTCP report block information map mapped by source SSRC.
  using ReportBlockMap = std::map<uint32_t, ReportBlockInfoMap>;

  bool ParseCompoundPacket(const uint8_t* packet_begin,
                           const uint8_t* packet_end,
                           RTCPHelp::RTCPPacketInformation* packet_information);

  void TriggerCallbacksFromRTCPPacket(
      RTCPHelp::RTCPPacketInformation& rtcpPacketInformation);

  RTCPUtility::RTCPCnameInformation* CreateCnameInformation(
      uint32_t remoteSSRC);
  RTCPUtility::RTCPCnameInformation* GetCnameInformation(
      uint32_t remoteSSRC) const;

  RTCPHelp::RTCPReceiveInformation* CreateReceiveInformation(
      uint32_t remoteSSRC);
  RTCPHelp::RTCPReceiveInformation* GetReceiveInformation(uint32_t remoteSSRC);

  void HandleSenderReport(
      const rtcp::CommonHeader& rtcp_block,
      RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleReceiverReport(
      const rtcp::CommonHeader& rtcp_block,
      RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleReportBlock(const rtcp::ReportBlock& report_block,
                         RTCPHelp::RTCPPacketInformation& rtcpPacketInformation,
                         uint32_t remoteSSRC)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleSDES(const rtcp::CommonHeader& rtcp_block,
                  RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleXr(const rtcp::CommonHeader& rtcp_block,
                RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleXrReceiveReferenceTime(
      const rtcp::Rrtr& rrtr,
      RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleXrDlrrReportBlock(
      const rtcp::ReceiveTimeInfo& rti,
      RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleNACK(const rtcp::CommonHeader& rtcp_block,
                  RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleBYE(const rtcp::CommonHeader& rtcp_block)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandlePLI(const rtcp::CommonHeader& rtcp_block,
                 RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleSLI(const rtcp::CommonHeader& rtcp_block,
                 RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleRPSI(const rtcp::CommonHeader& rtcp_block,
                  RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandlePsfbApp(const rtcp::CommonHeader& rtcp_block,
                     RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleTMMBR(const rtcp::CommonHeader& rtcp_block,
                   RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleTMMBN(const rtcp::CommonHeader& rtcp_block,
                   RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleSR_REQ(const rtcp::CommonHeader& rtcp_block,
                    RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleFIR(const rtcp::CommonHeader& rtcp_block,
                 RTCPHelp::RTCPPacketInformation& rtcpPacketInformation)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  void HandleTransportFeedback(
      const rtcp::CommonHeader& rtcp_block,
      RTCPHelp::RTCPPacketInformation* rtcp_packet_information)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  RTCPHelp::RTCPReportBlockInformation* CreateOrGetReportBlockInformation(
      uint32_t remote_ssrc,
      uint32_t source_ssrc)
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);
  RTCPHelp::RTCPReportBlockInformation* GetReportBlockInformation(
      uint32_t remote_ssrc,
      uint32_t source_ssrc) const
      EXCLUSIVE_LOCKS_REQUIRED(_criticalSectionRTCPReceiver);

  Clock* const _clock;
  const bool receiver_only_;
  ModuleRtpRtcp& _rtpRtcp;

  rtc::CriticalSection _criticalSectionFeedbacks;
  RtcpBandwidthObserver* const _cbRtcpBandwidthObserver;
  RtcpIntraFrameObserver* const _cbRtcpIntraFrameObserver;
  TransportFeedbackObserver* const _cbTransportFeedbackObserver;

  rtc::CriticalSection _criticalSectionRTCPReceiver;
  uint32_t main_ssrc_ GUARDED_BY(_criticalSectionRTCPReceiver);
  uint32_t _remoteSSRC GUARDED_BY(_criticalSectionRTCPReceiver);
  std::set<uint32_t> registered_ssrcs_ GUARDED_BY(_criticalSectionRTCPReceiver);

  // Received send report
  RTCPSenderInfo _remoteSenderInfo;
  // when did we receive the last send report
  uint32_t _lastReceivedSRNTPsecs;
  uint32_t _lastReceivedSRNTPfrac;

  // Received XR receive time report.
  RtcpReceiveTimeInfo _remoteXRReceiveTimeInfo;
  // Time when the report was received.
  uint32_t _lastReceivedXRNTPsecs;
  uint32_t _lastReceivedXRNTPfrac;
  // Estimated rtt, zero when there is no valid estimate.
  bool xr_rrtr_status_ GUARDED_BY(_criticalSectionRTCPReceiver);
  int64_t xr_rr_rtt_ms_;

  // Received report blocks.
  ReportBlockMap _receivedReportBlockMap
      GUARDED_BY(_criticalSectionRTCPReceiver);
  ReceivedInfoMap _receivedInfoMap;
  std::map<uint32_t, RTCPUtility::RTCPCnameInformation*> _receivedCnameMap;

  // The last time we received an RTCP RR.
  int64_t _lastReceivedRrMs;

  // The time we last received an RTCP RR telling we have successfully
  // delivered RTP packet to the remote side.
  int64_t _lastIncreasedSequenceNumberMs;

  RtcpStatisticsCallback* stats_callback_ GUARDED_BY(_criticalSectionFeedbacks);

  RtcpPacketTypeCounterObserver* const packet_type_counter_observer_;
  RtcpPacketTypeCounter packet_type_counter_;

  RTCPUtility::NackStats nack_stats_;

  size_t num_skipped_packets_;
  int64_t last_skipped_packets_warning_;
};
}  // namespace webrtc
#endif  // WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_
