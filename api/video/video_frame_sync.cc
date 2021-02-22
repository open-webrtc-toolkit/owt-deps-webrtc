/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video/video_frame_sync.h"

namespace webrtc {

FrameSync::FrameSync() = default;
FrameSync::FrameSync(const FrameSync& other) = default;
FrameSync::FrameSync(FrameSync&& other) = default;
FrameSync& FrameSync::operator=(const FrameSync& other) = default;

}  // namespace webrtc
