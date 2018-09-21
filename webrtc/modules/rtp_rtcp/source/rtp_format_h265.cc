/*
 *  Intel License
 */

#include <string.h>

#include "webrtc/base/logging.h"
#include "webrtc/modules/include/module_common_types.h"
#include "webrtc/modules/rtp_rtcp/source/byte_io.h"
#include "webrtc/modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "webrtc/modules/rtp_rtcp/source/h265_sps_parser.h"
#include "webrtc/modules/rtp_rtcp/source/rtp_format_h265.h"

namespace webrtc {
namespace {

enum NaluType {
  kTrailN = 0,
  kTrailR = 1,
  kTsaN = 2,
  kTsaR = 3,
  kStsaN = 4,
  kStsaR = 5,
  kRadlN = 6,
  kRadlR = 7,
  kBlaWLp = 16,
  kBlaWRadl = 17,
  kBlaNLp = 18,
  kIdrWRadl = 19,
  kIdrNLp = 20,
  kCra = 21,
  kVps = 32,
  kHevcSps = 33,
  kHevcPps = 34,
  kHevcAud = 35,
  kPrefixSei = 39,
  kSuffixSei = 40,
  kHevcAp = 48,
  kHevcFu = 49
};

static const size_t kHevcNalHeaderSize = 2;   // unlike H264, HEVC NAL header is 2-bytes
static const size_t kHevcFuHeaderSize = 1;    // HEVC's FU is constructed of 2-byte payloadHdr, and
                                              // 1-byte FU header
static const size_t kHevcLengthFieldSize = 2;
static const size_t kHevcApHeaderSize = kHevcNalHeaderSize + kHevcLengthFieldSize;


enum HevcNalHdrMasks {
    kHevcFbit = 0x80,
    kHevcTypeMask= 0x7E,
    kHevcLayerIDHMask = 0x1,
    kHevcLayerIDLMask = 0xF8,
    kHevcTIDMask = 0x7,
    kHevcTypeMaskN = 0x81,
    kHevcTypeMaskInFuHeader=0x3F
};

// Bit masks for FU headers.
enum HevcFuDefs {
    kHevcSBit = 0x80,
    kHevcEBit = 0x40,
    kHevcFuTypeBit = 0x3F
};

bool VerifyApNaluLengths(const uint8_t* nalu_ptr, size_t length_remaining) {
  while (length_remaining > 0) {
    // Buffer doesn't contain room for additional nalu length.
    if (length_remaining < sizeof(uint16_t))
      return false;
    uint16_t nalu_size = nalu_ptr[0] << 8 | nalu_ptr[1];
    nalu_ptr += sizeof(uint16_t);
    length_remaining -= sizeof(uint16_t);
    if (nalu_size > length_remaining)
      return false;
    nalu_ptr += nalu_size;
    length_remaining -= nalu_size;
  }
  return true;
}

bool ParseHevcSingleNalu(RtpDepacketizer::ParsedPayload* parsed_payload,
                     const uint8_t* payload_data,
                     size_t payload_data_length) {
  parsed_payload->type.Video.width = 0;
  parsed_payload->type.Video.height = 0;
  parsed_payload->type.Video.codec = kRtpVideoH265;
  RTPVideoHeaderH265* h265_header =
      &parsed_payload->type.Video.codecHeader.H265;

  if (payload_data_length <= 4)
       return false;

  const uint8_t* nalu_start = payload_data  /*kHevcNalHeaderSize*/;
  size_t nalu_length = payload_data_length /*- kHevcNalHeaderSize*/;
  //nal type is at bit 6:1
  uint8_t nal_type = (payload_data[0] >>1) & 0x7F;
  if (nal_type == kHevcAp) {
    // Skip the PayloadHdr. Also for simplicity, we don't support the DONL/DOND inclusion
    // in aggregation packet.
    // Refer to RFC 7798 section 4.4.2 for more details about sprop-max-don-diff and AP.
    if (payload_data_length <= kHevcApHeaderSize) {
      LOG(LS_ERROR) << "AP header truncated.";
      return false;
    }
    if (!VerifyApNaluLengths(nalu_start, nalu_length)) {
      LOG(LS_ERROR) << "AP packet with incorrect NALU packet lengths.";
      return false;
    }

    nal_type = payload_data[kHevcApHeaderSize] & kHevcTypeMask;
    nalu_start += kHevcApHeaderSize;
    nalu_length -= kHevcApHeaderSize;
    h265_header->packetization_type = kH265AP;
  } else {
    parsed_payload->type.Video.is_first_packet_in_frame = true;
    h265_header->packetization_type = kH265SingleNalu;
  }
  h265_header->nalu_type = nal_type;

  // We can read resolution out of sps packets. Using our packetization mode, it's not likely
  // SpS is at the beginning of the whole nale
  if (nal_type == kHevcSps) {
    H265SpsParser parser(nalu_start, nalu_length);
    if (parser.Parse()) {
      parsed_payload->type.Video.width = parser.width();
      parsed_payload->type.Video.height = parser.height();
    }
  }
  switch (nal_type) {
    case kVps:
    case kHevcSps:
    case kHevcPps:
    case kPrefixSei:
    case kSuffixSei:
    case kIdrNLp:
    case kIdrWRadl:
      parsed_payload->frame_type = kVideoFrameKey;
      break;
    default:
      parsed_payload->frame_type = kVideoFrameDelta;
      break;
  }
  return true;
}

bool ParseHevcFuNalu(RtpDepacketizer::ParsedPayload* parsed_payload,
                  const uint8_t* payload_data,
                  size_t payload_data_length,
                  size_t* offset) {
  if (payload_data_length < (kHevcFuHeaderSize + kHevcNalHeaderSize)) {
    LOG(LS_ERROR) << "FU NAL units truncated.";
    return false;
  }
  uint8_t f = payload_data[0] & kHevcFbit;
  uint8_t layer_id_h = payload_data[0] & kHevcLayerIDHMask;
  uint8_t layer_id_l_unshifted = payload_data[1] & kHevcLayerIDLMask;
  uint8_t tid = payload_data[1] & kHevcTIDMask;

  uint8_t original_nal_type = payload_data[2] & kHevcTypeMaskInFuHeader;
  bool first_fragment = (payload_data[2] & kHevcSBit) > 0;

  if (first_fragment) {
    // Unlike H264, when parsing the FuNal, since payload header is 2-bytes and
    // FU header is 1 byte. we need to set offset to 1 and replace in place the
    // next 2 bytes to be original NAL header.
    *offset = 1;
    uint8_t* payload = const_cast<uint8_t*>(payload_data + *offset);
    payload[0] = f | original_nal_type << 1 | layer_id_h;
    payload[1] = layer_id_l_unshifted | tid;
  } else {
      *offset = kHevcNalHeaderSize + kHevcFuHeaderSize;
  }

  if (original_nal_type == kIdrNLp || original_nal_type == kIdrWRadl) {
    parsed_payload->frame_type = kVideoFrameKey;
  } else {
    parsed_payload->frame_type = kVideoFrameDelta;
  }
  parsed_payload->type.Video.width = 0;
  parsed_payload->type.Video.height = 0;
  parsed_payload->type.Video.codec = kRtpVideoH265;
  parsed_payload->type.Video.is_first_packet_in_frame = first_fragment;
  RTPVideoHeaderH265* h265_header =
      &parsed_payload->type.Video.codecHeader.H265;
  h265_header->packetization_type = kH265FU;
  h265_header->nalu_type = original_nal_type;
  return true;
}
}  // namespace

RtpPacketizerH265::RtpPacketizerH265(FrameType frame_type,
                                     size_t max_payload_len)
    : payload_data_(NULL),
      payload_size_(0),
      max_payload_len_(max_payload_len) {
}

RtpPacketizerH265::~RtpPacketizerH265() {
}

RtpPacketizerH265::Fragment::Fragment(const uint8_t* buffer, size_t length)
    : buffer(buffer), length(length) {}
RtpPacketizerH265::Fragment::Fragment(const Fragment& fragment)
    : buffer(fragment.buffer), length(fragment.length) {}

void RtpPacketizerH265::SetPayloadData(
    const uint8_t* payload_data,
    size_t payload_size,
    const RTPFragmentationHeader* fragmentation) {
  RTC_DCHECK(packets_.empty());
  RTC_DCHECK(input_fragments_.empty());
  RTC_DCHECK(fragmentation);

  for (int i = 0; i < fragmentation->fragmentationVectorSize; ++i) {
      const uint8_t* buffer =
          &payload_data[fragmentation->fragmentationOffset[i]];
      size_t length = fragmentation->fragmentationLength[i];

      input_fragments_.push_back(Fragment(buffer, length));
  }
  GeneratePackets();
}

void RtpPacketizerH265::GeneratePackets() {
// For HEVC we follow non-interleaved mode for the packetization
    for (size_t i = 0; i < input_fragments_.size();) {
      if (input_fragments_[i].length > max_payload_len_) {
        PacketizeFu(i);
        ++i;
      } else {
        i = PacketizeAp(i);
      }
    }
}

void RtpPacketizerH265::PacketizeFu(size_t fragment_index) {
  // Fragment payload into packets (FU).
  // Strip out the original header and leave room for the FU header.
  const Fragment& fragment = input_fragments_[fragment_index];
  size_t fragment_length = fragment.length - kHevcNalHeaderSize;
  size_t offset = kHevcNalHeaderSize;
  //TODO(jianlin): check if it's OK to not calculate in the NAL header size into bytes_available
  size_t bytes_available = max_payload_len_ - (kHevcFuHeaderSize + kHevcNalHeaderSize);
  size_t fragments =
      (fragment_length + (bytes_available - 1)) / bytes_available;
  size_t avg_size = (fragment_length + fragments - 1) / fragments;
  while (fragment_length > 0) {
    size_t packet_length = avg_size;
    if (fragment_length < avg_size)
      packet_length = fragment_length;
    uint16_t header = (fragment.buffer[0] << 8) | fragment.buffer[1];
    packets_.push(PacketUnit(Fragment(fragment.buffer + offset, packet_length),
                         offset - kHevcNalHeaderSize == 0,
                         fragment_length == packet_length,
                         false,
                         header));
    offset += packet_length;
    fragment_length -= packet_length;
  }
}

int RtpPacketizerH265::PacketizeAp(size_t fragment_index) {
  // Aggregate fragments into one packet (AP).
  size_t payload_size_left = max_payload_len_;
  int aggregated_fragments = 0;
  size_t fragment_headers_length = 0;
  const Fragment* fragment = &input_fragments_[fragment_index];
  RTC_CHECK_GE(payload_size_left, fragment->length);
  while (payload_size_left >= fragment->length + fragment_headers_length) {
    RTC_CHECK_GE(fragment->length, 0);
    uint16_t header = (fragment->buffer[0] << 8) | fragment->buffer[1];
    packets_.push(PacketUnit(*fragment,
                         aggregated_fragments == 0,
                         false,
                         true,
                         header));
    payload_size_left -= fragment->length;
    payload_size_left -= fragment_headers_length;

    // Next fragment.
    ++fragment_index;
    //if (fragment_index == input_fragments_.size())
    if (true)
      break;
    fragment = &input_fragments_[fragment_index];

    fragment_headers_length = kHevcLengthFieldSize;
    // If we are going to try to aggregate more fragments into this packet
    // we need to add the AP NALU header and a length field for the first
    // NALU of this packet.
    if (aggregated_fragments == 0)
      fragment_headers_length += kHevcNalHeaderSize + kHevcLengthFieldSize;
    ++aggregated_fragments;
  }
  packets_.back().last_fragment = true;
  return fragment_index;
}

bool RtpPacketizerH265::NextPacket(RtpPacketToSend* rtp_packet,
                                   bool* last_packet) {
  RTC_DCHECK(rtp_packet);
  RTC_DCHECK(last_packet);

  if (packets_.empty()) {
    *last_packet = true;
    return false;
  }

  PacketUnit packet = packets_.front();

  if (packet.first_fragment && packet.last_fragment) {
    // Single NAL unit packet.
    size_t bytes_to_send = packet.source_fragment.length;
    uint8_t* buffer = rtp_packet->AllocatePayload(bytes_to_send);
    memcpy(buffer, packet.source_fragment.buffer, bytes_to_send);
    packets_.pop();
    input_fragments_.pop_front();
  } else if (packet.aggregated) {
    NextAggregatePacket(rtp_packet);
  } else {
    NextFragmentPacket(rtp_packet);
  }
  *last_packet = packets_.empty();
  rtp_packet->SetMarker(*last_packet);
  return true;
}

void RtpPacketizerH265::NextAggregatePacket(RtpPacketToSend* rtp_packet) {
  uint8_t* buffer = rtp_packet->AllocatePayload(max_payload_len_);
  RTC_DCHECK(buffer);

  PacketUnit* packet = &packets_.front();
  RTC_CHECK(packet->first_fragment);
  uint8_t payload_hdr_h = packet->header >> 8;
  uint8_t payload_hdr_l = packet->header & 0xFF;
  uint8_t layer_id_h = payload_hdr_h & kHevcLayerIDHMask;

  payload_hdr_h = (payload_hdr_h & kHevcTypeMaskN) | (kHevcAp << 1) | layer_id_h;

  buffer[0] = payload_hdr_h;
  buffer[1] = payload_hdr_l;
  int index = kHevcNalHeaderSize;
  while (packet->aggregated) {
    // Add NAL unit length field.
    const Fragment& fragment = packet->source_fragment;
    ByteWriter<uint16_t>::WriteBigEndian(&buffer[index], fragment.length);
    index += kHevcLengthFieldSize;
    // Add NAL unit.
    memcpy(&buffer[index], fragment.buffer, fragment.length);
    index += fragment.length;
    packets_.pop();
    input_fragments_.pop_front();
    if (packet->last_fragment)
      break;
    packet = &packets_.front();
  }
  RTC_CHECK(packet->last_fragment);
  rtp_packet->SetPayloadSize(index);
}

void RtpPacketizerH265::NextFragmentPacket(RtpPacketToSend* rtp_packet) {
  PacketUnit* packet = &packets_.front();
  // NAL unit fragmented over multiple packets (FU).
  // We do not send original NALU header, so it will be replaced by the
  // PayloadHdr of the first packet.
  uint8_t payload_hdr_h = packet->header >> 8;   //1-bit F, 6-bit type, 1-bit layerID highest-bit
  uint8_t payload_hdr_l = packet->header & 0xFF;
  uint8_t layer_id_h = payload_hdr_h & kHevcLayerIDHMask;
  uint8_t fu_header = 0;
  // S | E |6 bit type.
  fu_header |= (packet->first_fragment ? kHevcSBit : 0);
  fu_header |= (packet->last_fragment ? kHevcEBit : 0);
  uint8_t type = (payload_hdr_h & kHevcTypeMask) >> 1;
  fu_header |= type;
  //Now update payload_hdr_h with FU type.
  payload_hdr_h = (payload_hdr_h & kHevcTypeMaskN) | (kHevcFu << 1) | layer_id_h;
  const Fragment& fragment = packet->source_fragment;
  uint8_t* buffer = rtp_packet->AllocatePayload(kHevcFuHeaderSize + kHevcNalHeaderSize + fragment.length);
  buffer[0] = payload_hdr_h;
  buffer[1] = payload_hdr_l;
  buffer[2] = fu_header;

  if (packet->last_fragment) {
    memcpy(buffer + kHevcFuHeaderSize + kHevcNalHeaderSize, fragment.buffer, fragment.length);
  } else {
    memcpy(buffer + kHevcFuHeaderSize + kHevcNalHeaderSize, fragment.buffer, fragment.length);
  }
  packets_.pop();
}

ProtectionType RtpPacketizerH265::GetProtectionType() {
  return kProtectedPacket;
}

StorageType RtpPacketizerH265::GetStorageType(
    uint32_t retransmission_settings) {
  return kAllowRetransmission;
}

std::string RtpPacketizerH265::ToString() {
  return "RtpPacketizerH265";
}

bool RtpDepacketizerH265::Parse(ParsedPayload* parsed_payload,
                                const uint8_t* payload_data,
                                size_t payload_data_length) {
  assert(parsed_payload != NULL);
  if (payload_data_length == 0) {
    LOG(LS_ERROR) << "Empty payload.";
    return false;
  }

  uint8_t nal_type = (payload_data[0] & kHevcTypeMask) >> 1;
  size_t offset = 0;
  if (nal_type == kHevcFu) {
    // Fragmented NAL units
    if (!ParseHevcFuNalu(
            parsed_payload, payload_data, payload_data_length, &offset)) {
      return false;
    }
  } else {
    // We handle AP and single NALU's the same way here. The jitter buffer
    // will depacketize APs into NAL units later.
      if (!ParseHevcSingleNalu(parsed_payload, payload_data, payload_data_length)) {
          return false;
      }
  }

  parsed_payload->payload = payload_data + offset;
  parsed_payload->payload_length = payload_data_length - offset;
  return true;
}
}  // namespace webrtc
