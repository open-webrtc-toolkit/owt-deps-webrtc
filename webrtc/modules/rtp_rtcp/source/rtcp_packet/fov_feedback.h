/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_FOV_FEEDBACK_H_
#define WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_FOV_FEEDBACK_H_

#include <vector>

#include "webrtc/base/basictypes.h"
#include "webrtc/modules/rtp_rtcp/source/rtcp_packet/psfb.h"

namespace webrtc {
namespace rtcp {
class CommonHeader;
// FOV Feedback (RFC xxxx).
class FOVFeedback : public Psfb {
 public:
  static constexpr uint8_t kFeedbackMessageType = 8;

  FOVFeedback() {}
  ~FOVFeedback() override {}

  // Parse assumes header is already parsed and validated.
  bool Parse(const CommonHeader& packet);

  void SetSeqNr(uint16_t seq_nr) {seq_nr_ = seq_nr;}
  void SetFOV(uint16_t yaw, uint16_t pitch) {yaw_ = yaw; pitch_ = pitch;}

  uint16_t seq_nr_;
  uint16_t yaw_;
  uint16_t pitch_;

 protected:
  bool Create(uint8_t* packet,
              size_t* index,
              size_t max_length,
              RtcpPacket::PacketReadyCallback* callback) const override;

 private:
  static constexpr size_t kFciLength = 8;
  size_t BlockLength() const override {
    return kHeaderLength + kCommonFeedbackLength + kFciLength;
  }

};
}  // namespace rtcp
}  // namespace webrtc
#endif  // WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_FOV_FEEDBACK_H_
