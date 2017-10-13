/*
 *  Intel License
 */

#ifndef WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_H265_H_
#define WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_H265_H_

#include <queue>
#include <string>

#include "webrtc/base/buffer.h"
#include "webrtc/modules/rtp_rtcp/source/rtp_format.h"

namespace webrtc {

class RtpPacketizerH265 : public RtpPacketizer {
 public:
  // Initialize with payload from encoder.
  // The payload_data must be exactly one encoded H265 frame.
  RtpPacketizerH265(FrameType frame_type, size_t max_payload_len);

  virtual ~RtpPacketizerH265();

  void SetPayloadData(const uint8_t* payload_data,
                      size_t payload_size,
                      const RTPFragmentationHeader* fragmentation) override;

  // Get the next payload with H265 payload header.
  // buffer is a pointer to where the output will be written.
  // bytes_to_send is an output variable that will contain number of bytes
  // written to buffer. The parameter last_packet is true for the last packet of
  // the frame, false otherwise (i.e., call the function again to get the
  // next packet).
  // Returns true on success or false if there was no payload to packetize.
  bool NextPacket(RtpPacketToSend* rtp_packet,
                  bool* last_packet) override;

  ProtectionType GetProtectionType() override;

  StorageType GetStorageType(uint32_t retransmission_settings) override;

  std::string ToString() override;

 private:
  struct Packet {
    Packet(size_t offset,
           size_t size,
           bool first_fragment,
           bool last_fragment,
           bool aggregated,
           uint16_t header)
        : offset(offset),
          size(size),
          first_fragment(first_fragment),
          last_fragment(last_fragment),
          aggregated(aggregated),
          header(header) {}

    size_t offset;
    size_t size;
    bool first_fragment;
    bool last_fragment;
    bool aggregated;
    uint16_t header;   //Different from H264
  };
  struct Fragment {
      Fragment(const uint8_t* buffer, size_t length);
      explicit Fragment(const Fragment& fragment);
      const uint8_t* buffer = nullptr;
      size_t length = 0;
      std::unique_ptr<rtc::Buffer> tmp_buffer;
  };
  struct PacketUnit {
      PacketUnit(const Fragment& source_fragment,
          bool first_fragment,
          bool last_fragment,
          bool aggregated,
          uint16_t header)
          : source_fragment(source_fragment),
          first_fragment(first_fragment),
          last_fragment(last_fragment),
          aggregated(aggregated),
          header(header) {}

      const Fragment source_fragment;
      bool first_fragment;
      bool last_fragment;
      bool aggregated;
      uint16_t header;
  };
  typedef std::queue<Packet> PacketQueue;
  std::deque<Fragment> input_fragments_;
  std::queue<PacketUnit> packets_;

  void GeneratePackets();
  void PacketizeFu(size_t fragment_index);
  int PacketizeAp(size_t fragment_index);

  void NextAggregatePacket(RtpPacketToSend* rtp_packet);
  void NextFragmentPacket(RtpPacketToSend* rtp_packet);

  const uint8_t* payload_data_;
  size_t payload_size_;
  const size_t max_payload_len_;
  RTPFragmentationHeader fragmentation_;
  //PacketQueue packets_;

  RTC_DISALLOW_COPY_AND_ASSIGN(RtpPacketizerH265);
};

// Depacketizer for H265.
class RtpDepacketizerH265 : public RtpDepacketizer {
 public:
  virtual ~RtpDepacketizerH265() {}

  bool Parse(ParsedPayload* parsed_payload,
             const uint8_t* payload_data,
             size_t payload_data_length) override;
};
}  // namespace webrtc
#endif  // WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_H265_H_
