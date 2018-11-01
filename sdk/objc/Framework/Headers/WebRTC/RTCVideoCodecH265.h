/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/* This file is borrowed from webrtc/sdk/objc/Framework/Headers/WebRTC/RTCVideoCodecH264.h */

#import <Foundation/Foundation.h>

#import <WebRTC/RTCMacros.h>
#import <WebRTC/RTCVideoCodecFactory.h>

RTC_EXPORT
@interface RTCCodecSpecificInfoH265 : NSObject <RTCCodecSpecificInfo>
@end

/** Encoder. */
RTC_EXPORT
API_AVAILABLE(ios(11.0))
@interface RTCVideoEncoderH265 : NSObject<RTCVideoEncoder>

- (instancetype)initWithCodecInfo:(RTCVideoCodecInfo *)codecInfo;

@end

/** Decoder. */
RTC_EXPORT
API_AVAILABLE(ios(11.0))
@interface RTCVideoDecoderH265 : NSObject<RTCVideoDecoder>
@end

/** Encoder factory. */
RTC_EXPORT
API_AVAILABLE(ios(11.0))
@interface RTCVideoEncoderFactoryH265 : NSObject<RTCVideoEncoderFactory>
@end

/** Decoder factory. */
RTC_EXPORT
API_AVAILABLE(ios(11.0))
@interface RTCVideoDecoderFactoryH265 : NSObject<RTCVideoDecoderFactory>
@end
