/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_VIDEO_VIDEO_FRAME_SYNC_H_
#define API_VIDEO_VIDEO_FRAME_SYNC_H_

#include <stdint.h>
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

// This class represents frame sync point information needed by SFU/MCU
// to sync frames from different streams.
//
// Currently the |sync_point| is an application defined 2-byte identfier
// that is supposed mono increase and wrap around when maximum value is
// reached. one byte is reserved here for future extension.
class RTC_EXPORT FrameSync {
 public:
  FrameSync();
  FrameSync(const FrameSync& other);
  FrameSync(FrameSync&& other);
  FrameSync& operator=(const FrameSync& other);

  friend bool operator==(const FrameSync& lhs, const FrameSync& rhs) {
    return lhs.sync_point_ == rhs.sync_point_;
  }
  friend bool operator!=(const FrameSync& lhs, const FrameSync& rhs) {
    return lhs.sync_point_ != rhs.sync_point_;
  }

  void set_sync_point(uint16_t sync_point) {
      sync_point_ = sync_point;
  }

  uint16_t SyncPoint() const {
    return sync_point_;
  }

 private:
  uint16_t sync_point_;
  uint8_t reserved_one_byte_;
};
}  // namespace webrtc
#endif  // API_VIDEO_VIDEO_FRAME_SYNC_H_