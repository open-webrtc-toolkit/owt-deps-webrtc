/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/rtp_rtcp/source/rtcp_packet/fov_feedback.h"

#include "webrtc/base/checks.h"
#include "webrtc/base/logging.h"
#include "webrtc/modules/rtp_rtcp/source/byte_io.h"
#include "webrtc/modules/rtp_rtcp/source/rtcp_packet/common_header.h"

namespace webrtc {
namespace rtcp {
constexpr uint8_t FOVFeedback::kFeedbackMessageType;
// RFC 4585: Feedback format.
// Common packet format:
//
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |V=2|P|   FMT   |       PT      |          length               |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                  SSRC of packet sender                        |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |             SSRC of media source                              |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  :            Feedback Control Information (FCI)                 :
//  :                                                               :
// FOV Feedback (RFC xxxx).
// The Feedback Control Information (FCI) for the FOV Feedback
// FCI:
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                              SSRC                             |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  | Seq nr.                       | yaw                           |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  | pitch                         | Reserved = 0                  |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
bool FOVFeedback::Parse(const CommonHeader& packet) {
  RTC_DCHECK_EQ(packet.type(), kPacketType);
  RTC_DCHECK_EQ(packet.fmt(), kFeedbackMessageType);

  if ((packet.payload_size_bytes() - kCommonFeedbackLength) != kFciLength) {
    LOG(LS_WARNING) << "Invalid size for a valid FOVFeedback packet.";
    return false;
  }

  ParseCommonFeedback(packet.payload());

  const uint8_t* next_fci = packet.payload() + kCommonFeedbackLength;

  seq_nr_ = ByteReader<uint16_t>::ReadBigEndian(next_fci);
  yaw_ = ByteReader<uint16_t>::ReadBigEndian(next_fci + 2);
  pitch_ = ByteReader<uint16_t>::ReadBigEndian(next_fci + 4);

  return true;
}

bool FOVFeedback::Create(uint8_t* packet,
                 size_t* index,
                 size_t max_length,
                 RtcpPacket::PacketReadyCallback* callback) const {
  while (*index + BlockLength() > max_length) {
    if (!OnBufferFull(packet, index, callback))
      return false;
  }
  size_t index_end = *index + BlockLength();
  CreateHeader(kFeedbackMessageType, kPacketType, HeaderLength(), packet,
               index);
  RTC_DCHECK_EQ(Psfb::media_ssrc(), 0);
  CreateCommonFeedback(packet + *index);
  *index += kCommonFeedbackLength;

  constexpr uint32_t kReserved = 0;

  ByteWriter<uint16_t>::WriteBigEndian(packet + *index, seq_nr_);
  ByteWriter<uint16_t>::WriteBigEndian(packet + *index + 2, yaw_);
  ByteWriter<uint16_t>::WriteBigEndian(packet + *index + 4, pitch_);
  ByteWriter<uint16_t>::WriteBigEndian(packet + *index + 6, kReserved);

  RTC_CHECK_EQ(*index, index_end);
  return true;
}
}  // namespace rtcp
}  // namespace webrtc
