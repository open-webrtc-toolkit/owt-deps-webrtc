/*
 *  Intel License
 */

#include <string.h>

#include "common_video/h264/h264_common.h"
#include "common_video/h265/h265_common.h"
#include "common_video/h265/h265_pps_parser.h"
#include "common_video/h265/h265_sps_parser.h"
#include "common_video/h265/h265_vps_parser.h"
#include "modules/include/module_common_types.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/rtp_format_h265.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "rtc_base/logging.h"
#include "rtc_base/system/fallthrough.h"
using namespace rtc;

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

/*
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |    PayloadHdr (Type=49)       |   FU header   | DONL (cond)   |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
*/
// Unlike H.264, HEVC NAL header is 2-bytes.
static const size_t kHevcNalHeaderSize = 2;
// H.265's FU is constructed of 2-byte payload header, and 1-byte FU header
static const size_t kHevcFuHeaderSize = 1;
static const size_t kHevcLengthFieldSize = 2;
static const size_t kHevcApHeaderSize =
    kHevcNalHeaderSize + kHevcLengthFieldSize;

enum HevcNalHdrMasks {
  kHevcFBit = 0x80,
  kHevcTypeMask = 0x7E,
  kHevcLayerIDHMask = 0x1,
  kHevcLayerIDLMask = 0xF8,
  kHevcTIDMask = 0x7,
  kHevcTypeMaskN = 0x81,
  kHevcTypeMaskInFuHeader = 0x3F
};

// Bit masks for FU headers.
enum HevcFuDefs { kHevcSBit = 0x80, kHevcEBit = 0x40, kHevcFuTypeBit = 0x3F };

// TODO(pbos): Avoid parsing this here as well as inside the jitter buffer.
bool ParseApStartOffsets(const uint8_t* nalu_ptr,
                         size_t length_remaining,
                         std::vector<size_t>* offsets) {
  size_t offset = 0;
  while (length_remaining > 0) {
    // Buffer doesn't contain room for additional nalu length.
    if (length_remaining < sizeof(uint16_t))
      return false;
    uint16_t nalu_size = ByteReader<uint16_t>::ReadBigEndian(nalu_ptr);
    nalu_ptr += sizeof(uint16_t);
    length_remaining -= sizeof(uint16_t);
    if (nalu_size > length_remaining)
      return false;
    nalu_ptr += nalu_size;
    length_remaining -= nalu_size;

    offsets->push_back(offset + kHevcApHeaderSize);
    offset += kHevcLengthFieldSize + nalu_size;
  }
  return true;
}

}  // namespace

RtpPacketizerH265::RtpPacketizerH265(FrameType frame_type,
                                     size_t max_payload_len,
                                     size_t last_packet_reduction_len)
    : max_payload_len_(max_payload_len),
      last_packet_reduction_len_(last_packet_reduction_len),
      num_packets_left_(0) {
  RTC_CHECK_GT(max_payload_len, last_packet_reduction_len);
}

RtpPacketizerH265::~RtpPacketizerH265() {}

RtpPacketizerH265::Fragment::Fragment(const uint8_t* buffer, size_t length)
    : buffer(buffer), length(length) {}
RtpPacketizerH265::Fragment::Fragment(const Fragment& fragment)
    : buffer(fragment.buffer), length(fragment.length) {}

size_t RtpPacketizerH265::SetPayloadData(
    const uint8_t* payload_data,
    size_t payload_size,
    const RTPFragmentationHeader* fragmentation,
    bool /*end_of_frame*/) {
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
  return num_packets_left_;
}

void RtpPacketizerH265::GeneratePackets() {
  // For HEVC we follow non-interleaved mode for the packetization.
  for (size_t i = 0; i < input_fragments_.size();) {
    size_t fragment_len = input_fragments_[i].length;
    if (i + 1 == input_fragments_.size()) {
      // Pretend that last fragment is larger instead of making last packet
      // smaller.
      fragment_len += last_packet_reduction_len_;
    }
    if (fragment_len > max_payload_len_) {
      PacketizeFu(i);
      ++i;
    } else {
      PacketizeSingleNalu(i);
      ++i;
    }
  }
}

void RtpPacketizerH265::PacketizeFu(size_t fragment_index) {
  // Fragment payload into packets (FU).
  // Strip out the original header and leave room for the FU header.
  const Fragment& fragment = input_fragments_[fragment_index];
  bool is_last_fragment = fragment_index + 1 == input_fragments_.size();
  size_t payload_left = fragment.length - kHevcNalHeaderSize;
  size_t offset = kHevcNalHeaderSize;
  size_t per_packet_capacity =
      max_payload_len_ - (kHevcFuHeaderSize + kHevcNalHeaderSize);
  // Instead of making the last packet smaller we pretend that all packets are
  // of the same size but we write additional virtual payload to the last
  // packet.
  size_t extra_len = is_last_fragment ? last_packet_reduction_len_ : 0;

  // Integer divisions with rounding up. Minimal number of packets to fit all
  // payload and virtual payload.
  size_t num_packets = (payload_left + extra_len + (per_packet_capacity - 1)) /
                       per_packet_capacity;
  // Bytes per packet. Average rounded down.
  size_t payload_per_packet = (payload_left + extra_len) / num_packets;
  // We make several first packets to be 1 bytes smaller than the rest.
  // i.e 14 bytes splitted in 4 packets would be 3+3+4+4.
  size_t num_larger_packets = (payload_left + extra_len) % num_packets;

  num_packets_left_ += num_packets;
  while (payload_left > 0) {
    // Increase payload per packet at the right time.
    if (num_packets == num_larger_packets)
      ++payload_per_packet;
    size_t packet_length = payload_per_packet;
    if (payload_left <= packet_length) {  // Last portion of the payload
      packet_length = payload_left;
      // One additional packet may be used for extensions in the last packet.
      // Together with last payload packet there may be at most 2 of them.
      RTC_DCHECK_LE(num_packets, 2);
      if (num_packets == 2) {
        // Whole payload fits in the first num_packets-1 packets but extra
        // packet is used for virtual payload. Leave at least one byte of data
        // for the last packet.
        --packet_length;
      }
    }
    RTC_CHECK_GT(packet_length, 0);
    uint16_t header = (fragment.buffer[0] << 8) | fragment.buffer[1];
    packets_.push(PacketUnit(Fragment(fragment.buffer + offset, packet_length),
                             offset - kHevcNalHeaderSize == 0,
                             payload_left == packet_length, false, header));
    offset += packet_length;
    payload_left -= packet_length;
    --num_packets;
  }
  RTC_CHECK_EQ(0, payload_left);
}

bool RtpPacketizerH265::PacketizeSingleNalu(size_t fragment_index) {
  // Add a single NALU to the queue, no aggregation.
  size_t payload_size_left = max_payload_len_;
  if (fragment_index + 1 == input_fragments_.size())
    payload_size_left -= last_packet_reduction_len_;
  const Fragment* fragment = &input_fragments_[fragment_index];
  if (payload_size_left < fragment->length) {
    RTC_LOG(LS_ERROR) << "Failed to fit a fragment to packet in SingleNalu "
                         "packetization mode. Payload size left "
                      << payload_size_left << ", fragment length "
                      << fragment->length << ", packet capacity "
                      << max_payload_len_;
    return false;
  }
  RTC_CHECK_GT(fragment->length, 0u);
  packets_.push(PacketUnit(*fragment, true /* first */, true /* last */,
                           false /* aggregated */, fragment->buffer[0]));
  ++num_packets_left_;
  return true;
}

int RtpPacketizerH265::PacketizeAp(size_t fragment_index) {
  // Aggregate fragments into one packet (AP).
  size_t payload_size_left = max_payload_len_;
  int aggregated_fragments = 0;
  size_t fragment_headers_length = 0;
  const Fragment* fragment = &input_fragments_[fragment_index];
  RTC_CHECK_GE(payload_size_left, fragment->length);
  ++num_packets_left_;
  while (payload_size_left >= fragment->length + fragment_headers_length &&
         (fragment_index + 1 < input_fragments_.size() ||
          payload_size_left >= fragment->length + fragment_headers_length +
                                   last_packet_reduction_len_)) {
    RTC_CHECK_GE(fragment->length, 0);
    uint16_t header = (fragment->buffer[0] << 8) | fragment->buffer[1];
    packets_.push(
        PacketUnit(*fragment, aggregated_fragments == 0, false, true, header));
    payload_size_left -= fragment->length;
    payload_size_left -= fragment_headers_length;

    fragment_headers_length = kHevcLengthFieldSize;
    // If we are going to try to aggregate more fragments into this packet
    // we need to add the AP NALU header and a length field for the first
    // NALU of this packet.
    if (aggregated_fragments == 0)
      fragment_headers_length += kHevcNalHeaderSize + kHevcLengthFieldSize;
    ++aggregated_fragments;

    // Next fragment.
    ++fragment_index;
    if (fragment_index == input_fragments_.size())
      break;
    fragment = &input_fragments_[fragment_index];
  }
  RTC_CHECK_GT(aggregated_fragments, 0);
  packets_.back().last_fragment = true;
  return fragment_index;
}

bool RtpPacketizerH265::NextPacket(RtpPacketToSend* rtp_packet) {
  RTC_DCHECK(rtp_packet);

  if (packets_.empty()) {
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
    bool is_last_packet = num_packets_left_ == 1;
    NextAggregatePacket(rtp_packet, is_last_packet);
  } else {
    NextFragmentPacket(rtp_packet);
  }
  if (packets_.empty()) {
    RTC_DCHECK_LE(rtp_packet->payload_size(),
                  max_payload_len_ - last_packet_reduction_len_);
  }
  rtp_packet->SetMarker(packets_.empty());
  --num_packets_left_;
  return true;
}

void RtpPacketizerH265::NextAggregatePacket(RtpPacketToSend* rtp_packet,
                                            bool last) {
  uint8_t* buffer = rtp_packet->AllocatePayload(
      last ? max_payload_len_ - last_packet_reduction_len_ : max_payload_len_);
  RTC_DCHECK(buffer);

  PacketUnit* packet = &packets_.front();
  RTC_CHECK(packet->first_fragment);
  uint8_t payload_hdr_h = packet->header >> 8;
  uint8_t payload_hdr_l = packet->header & 0xFF;
  uint8_t layer_id_h = payload_hdr_h & kHevcLayerIDHMask;

  payload_hdr_h =
      (payload_hdr_h & kHevcTypeMaskN) | (kHevcAp << 1) | layer_id_h;

  buffer[0] = payload_hdr_h;
  buffer[1] = payload_hdr_l;
  int index = kHevcNalHeaderSize;
  bool is_last_fragment = packet->last_fragment;
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
    if (is_last_fragment)
      break;
    packet = &packets_.front();
    is_last_fragment = packet->last_fragment;
  }
  RTC_CHECK(is_last_fragment);
  rtp_packet->SetPayloadSize(index);
}

void RtpPacketizerH265::NextFragmentPacket(RtpPacketToSend* rtp_packet) {
  PacketUnit* packet = &packets_.front();
  // NAL unit fragmented over multiple packets (FU).
  // We do not send original NALU header, so it will be replaced by the
  // PayloadHdr of the first packet.
  uint8_t payload_hdr_h =
      packet->header >> 8;  // 1-bit F, 6-bit type, 1-bit layerID highest-bit
  uint8_t payload_hdr_l = packet->header & 0xFF;
  uint8_t layer_id_h = payload_hdr_h & kHevcLayerIDHMask;
  uint8_t fu_header = 0;
  // S | E |6 bit type.
  fu_header |= (packet->first_fragment ? kHevcSBit : 0);
  fu_header |= (packet->last_fragment ? kHevcEBit : 0);
  uint8_t type = (payload_hdr_h & kHevcTypeMask) >> 1;
  fu_header |= type;
  // Now update payload_hdr_h with FU type.
  payload_hdr_h =
      (payload_hdr_h & kHevcTypeMaskN) | (kHevcFu << 1) | layer_id_h;
  const Fragment& fragment = packet->source_fragment;
  uint8_t* buffer = rtp_packet->AllocatePayload(
      kHevcFuHeaderSize + kHevcNalHeaderSize + fragment.length);
  buffer[0] = payload_hdr_h;
  buffer[1] = payload_hdr_l;
  buffer[2] = fu_header;

  if (packet->last_fragment) {
    memcpy(buffer + kHevcFuHeaderSize + kHevcNalHeaderSize, fragment.buffer,
           fragment.length);
  } else {
    memcpy(buffer + kHevcFuHeaderSize + kHevcNalHeaderSize, fragment.buffer,
           fragment.length);
  }
  packets_.pop();
}

std::string RtpPacketizerH265::ToString() {
  return "RtpPacketizerH265";
}

bool RtpDepacketizerH265::Parse(ParsedPayload* parsed_payload,
                                const uint8_t* payload_data,
                                size_t payload_data_length) {
  RTC_CHECK(parsed_payload != nullptr);
  if (payload_data_length == 0) {
    RTC_LOG(LS_ERROR) << "Empty payload.";
    return false;
  }

  offset_ = 0;
  length_ = payload_data_length;
  modified_buffer_.reset();

  uint8_t nal_type = (payload_data[0] & kHevcTypeMask) >> 1;
  parsed_payload->video_header()
      .video_type_header.emplace<RTPVideoHeaderH265>();

  if (nal_type == H265::NaluType::kFU) {
    // Fragmented NAL units (FU-A).
    if (!ParseFuNalu(parsed_payload, payload_data))
      return false;
  } else {
    // We handle STAP-A and single NALU's the same way here. The jitter buffer
    // will depacketize the STAP-A into NAL units later.
    // TODO(sprang): Parse STAP-A offsets here and store in fragmentation vec.
    if (!ProcessApOrSingleNalu(parsed_payload, payload_data))
      return false;
  }

  const uint8_t* payload =
      modified_buffer_ ? modified_buffer_->data() : payload_data;

  parsed_payload->payload = payload + offset_;
  parsed_payload->payload_length = length_;
  return true;
}

bool RtpDepacketizerH265::ProcessApOrSingleNalu(
    RtpDepacketizer::ParsedPayload* parsed_payload,
    const uint8_t* payload_data) {
  parsed_payload->video_header().width = 0;
  parsed_payload->video_header().height = 0;
  parsed_payload->video_header().codec = kVideoCodecH265;
  parsed_payload->video_header().is_first_packet_in_frame = true;
  auto& h265_header = absl::get<RTPVideoHeaderH265>(
      parsed_payload->video_header().video_type_header);

  const uint8_t* nalu_start = payload_data + kHevcNalHeaderSize;
  const size_t nalu_length = length_ - kHevcNalHeaderSize;
  uint8_t nal_type = (payload_data[0] & kHevcTypeMask) >> 1;
  std::vector<size_t> nalu_start_offsets;
  if (nal_type == H265::NaluType::kAP) {
    // Skip the StapA header (StapA NAL type + length).
    if (length_ <= kHevcApHeaderSize) {
      RTC_LOG(LS_ERROR) << "AP header truncated.";
      return false;
    }

    if (!ParseApStartOffsets(nalu_start, nalu_length, &nalu_start_offsets)) {
      RTC_LOG(LS_ERROR) << "AP packet with incorrect NALU packet lengths.";
      return false;
    }

    h265_header.packetization_type = kH265AP;
    // nal_type = (payload_data[kHevcApHeaderSize] & kHevcTypeMask) >> 1;
  } else {
    h265_header.packetization_type = kH265SingleNalu;
    nalu_start_offsets.push_back(0);
  }
  h265_header.nalu_type = nal_type;
  parsed_payload->frame_type = kVideoFrameDelta;

  nalu_start_offsets.push_back(length_ + kHevcLengthFieldSize);  // End offset.
  for (size_t i = 0; i < nalu_start_offsets.size() - 1; ++i) {
    size_t start_offset = nalu_start_offsets[i];
    // End offset is actually start offset for next unit, excluding length field
    // so remove that from this units length.
    size_t end_offset = nalu_start_offsets[i + 1] - kHevcLengthFieldSize;
    if (end_offset - start_offset < kHevcNalHeaderSize) {  // Same as H.264.
      RTC_LOG(LS_ERROR) << "AP packet too short";
      return false;
    }

    H265NaluInfo nalu;
    nalu.type = (payload_data[start_offset] & kHevcTypeMask) >> 1;
    nalu.vps_id = -1;
    nalu.sps_id = -1;
    nalu.pps_id = -1;
    start_offset += kHevcNalHeaderSize;

    switch (nalu.type) {
      case H265::NaluType::kVps: {
        absl::optional<H265VpsParser::VpsState> vps = H265VpsParser::ParseVps(
            &payload_data[start_offset], end_offset - start_offset);
        if (vps) {
          nalu.vps_id = vps->id;
        } else {
          RTC_LOG(LS_WARNING) << "Failed to parse VPS id from VPS slice.";
        }
        break;
      }
      case H265::NaluType::kSps: {
        // Check if VUI is present in SPS and if it needs to be modified to
        // avoid excessive decoder latency.

        // Copy any previous data first (likely just the first header).
        std::unique_ptr<rtc::Buffer> output_buffer(new rtc::Buffer());
        if (start_offset)
          output_buffer->AppendData(payload_data, start_offset);

        absl::optional<H265SpsParser::SpsState> sps = H265SpsParser::ParseSps(
            &payload_data[start_offset], end_offset - start_offset);

        if (sps) {
          parsed_payload->video_header().width = sps->width;
          parsed_payload->video_header().height = sps->height;
          nalu.sps_id = sps->id;
          nalu.vps_id = sps->vps_id;
        } else {
          RTC_LOG(LS_WARNING)
              << "Failed to parse SPS and VPS id from SPS slice.";
        }
        parsed_payload->frame_type = kVideoFrameKey;
        break;
      }
      case H265::NaluType::kPps: {
        uint32_t pps_id;
        uint32_t sps_id;
        if (H265PpsParser::ParsePpsIds(&payload_data[start_offset],
                                       end_offset - start_offset, &pps_id,
                                       &sps_id)) {
          nalu.pps_id = pps_id;
          nalu.sps_id = sps_id;
        } else {
          RTC_LOG(LS_WARNING)
              << "Failed to parse PPS id and SPS id from PPS slice.";
        }
        break;
      }
      case H265::NaluType::kIdrWRadl:
      case H265::NaluType::kIdrNlp:
      case H265::NaluType::kCra:
        parsed_payload->frame_type = kVideoFrameKey;
        RTC_FALLTHROUGH();
      case H265::NaluType::kTrailN:
      case H265::NaluType::kTrailR: {
        absl::optional<uint32_t> pps_id =
            H265PpsParser::ParsePpsIdFromSliceSegmentLayerRbsp(
                &payload_data[start_offset], end_offset - start_offset,
                nalu.type);
        if (pps_id) {
          nalu.pps_id = *pps_id;
        } else {
          RTC_LOG(LS_WARNING) << "Failed to parse PPS id from slice of type: "
                              << static_cast<int>(nalu.type);
        }
        break;
      }
      // Slices below don't contain SPS or PPS ids.
      case H265::NaluType::kAud:
      case H265::NaluType::kTsaN:
      case H265::NaluType::kTsaR:
      case H265::NaluType::kStsaN:
      case H265::NaluType::kStsaR:
      case H265::NaluType::kRadlN:
      case H265::NaluType::kRadlR:
      case H265::NaluType::kBlaWLp:
      case H265::NaluType::kBlaWRadl:
      case H265::NaluType::kPrefixSei:
      case H265::NaluType::kSuffixSei:
        break;
      case H265::NaluType::kAP:
      case H265::NaluType::kFU:
        RTC_LOG(LS_WARNING) << "Unexpected AP or FU received.";
        return false;
    }

    if (h265_header.nalus_length == kMaxNalusPerPacket) {
      RTC_LOG(LS_WARNING)
          << "Received packet containing more than " << kMaxNalusPerPacket
          << " NAL units. Will not keep track sps and pps ids for all of them.";
    } else {
      h265_header.nalus[h265_header.nalus_length++] = nalu;
    }
  }
  return true;
}

bool RtpDepacketizerH265::ParseFuNalu(
    RtpDepacketizer::ParsedPayload* parsed_payload,
    const uint8_t* payload_data) {
  if (length_ < kHevcFuHeaderSize + kHevcNalHeaderSize) {
    RTC_LOG(LS_ERROR) << "FU NAL units truncated.";
    return false;
  }
  uint8_t f = payload_data[0] & kHevcFBit;
  uint8_t layer_id_h = payload_data[0] & kHevcLayerIDHMask;
  uint8_t layer_id_l_unshifted = payload_data[1] & kHevcLayerIDLMask;
  uint8_t tid = payload_data[1] & kHevcTIDMask;

  uint8_t original_nal_type = payload_data[2] & kHevcTypeMaskInFuHeader;
  bool first_fragment = payload_data[2] & kHevcSBit;
  H265NaluInfo nalu;
  nalu.type = original_nal_type;
  nalu.vps_id = -1;
  nalu.sps_id = -1;
  nalu.pps_id = -1;
  if (first_fragment) {
    offset_ = 1;
    length_ -= 1;
    absl::optional<uint32_t> pps_id =
        H265PpsParser::ParsePpsIdFromSliceSegmentLayerRbsp(
            payload_data + kHevcNalHeaderSize + kHevcFuHeaderSize,
            length_ - kHevcFuHeaderSize, nalu.type);
    if (pps_id) {
      nalu.pps_id = *pps_id;
    } else {
      RTC_LOG(LS_WARNING)
          << "Failed to parse PPS from first fragment of FU NAL "
             "unit with original type: "
          << static_cast<int>(nalu.type);
    }
    uint8_t* payload = const_cast<uint8_t*>(payload_data + offset_);
    payload[0] = f | original_nal_type << 1 | layer_id_h;
    payload[1] = layer_id_l_unshifted | tid;
  } else {
    offset_ = kHevcNalHeaderSize + kHevcFuHeaderSize;
    length_ -= (kHevcNalHeaderSize + kHevcFuHeaderSize);
  }

  if (original_nal_type == H265::NaluType::kIdrWRadl ||
      original_nal_type == H265::NaluType::kIdrNlp ||
      original_nal_type == H265::NaluType::kCra) {
    parsed_payload->frame_type = kVideoFrameKey;
  } else {
    parsed_payload->frame_type = kVideoFrameDelta;
  }
  parsed_payload->video_header().width = 0;
  parsed_payload->video_header().height = 0;
  parsed_payload->video_header().codec = kVideoCodecH265;
  parsed_payload->video_header().is_first_packet_in_frame = first_fragment;
  auto& h265_header = absl::get<RTPVideoHeaderH265>(
      parsed_payload->video_header().video_type_header);
  h265_header.packetization_type = kH265FU;
  h265_header.nalu_type = original_nal_type;
  if (first_fragment) {
    h265_header.nalus[h265_header.nalus_length] = nalu;
    h265_header.nalus_length = 1;
  }
  return true;
}
}  // namespace webrtc
