/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/* This file is borrowed from
 * webrtc/sdk/objc/Framework/Classes/PeerConnection/RTCVideoCodecH265.mm */

#import "WebRTC/RTCVideoCodec.h"
#import "WebRTC/RTCVideoCodecH265.h"

#include <vector>
#include "media/base/media_constants.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/field_trial.h"

NSString *const kRTCVideoCodecH265Name = @(cricket::kH265CodecName);

static NSString* kH265CodecName = @"H265";
// TODO(jianjunz): This is value is not correct.
static NSString* kLevel31Main = @"4d001f";

// Encoder factory.
@implementation RTCVideoEncoderFactoryH265

- (NSArray<RTCVideoCodecInfo*>*)supportedCodecs {
  NSMutableArray<RTCVideoCodecInfo*>* codecs = [NSMutableArray array];
  NSString* codecName = kH265CodecName;

  NSDictionary<NSString*, NSString*>* mainParams = @{
    @"profile-level-id" : kLevel31Main,
    @"level-asymmetry-allowed" : @"1",
    @"packetization-mode" : @"1",
  };
  RTCVideoCodecInfo* constrainedBaselineInfo =
      [[RTCVideoCodecInfo alloc] initWithName:codecName parameters:mainParams];
  [codecs addObject:constrainedBaselineInfo];

  return [codecs copy];
}

- (id<RTCVideoEncoder>)createEncoder:(RTCVideoCodecInfo*)info {
  return [[RTCVideoEncoderH265 alloc] initWithCodecInfo:info];
}

@end

// Decoder factory.
@implementation RTCVideoDecoderFactoryH265

- (id<RTCVideoDecoder>)createDecoder:(RTCVideoCodecInfo*)info {
  return [[RTCVideoDecoderH265 alloc] init];
}

- (NSArray<RTCVideoCodecInfo*>*)supportedCodecs {
  NSString* codecName = kH265CodecName;
  return @[ [[RTCVideoCodecInfo alloc] initWithName:codecName parameters:nil] ];
}

@end
