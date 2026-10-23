// Wire protocol for Nothing / CMF earbuds (RFCOMM control channel).
//
// Frame layout (little endian):
//     0x55 | control:u16 | command:u16 | length:u16 | seq:u8 | payload | [crc16]
//
// A CRC16 (poly 0xA001, init 0xFFFF, i.e. CRC-16/MODBUS) over the whole frame is
// appended when bit 0x20 of the control word is set. Protocol details come from
// Gadgetbridge's Ear1Support plus captures from a CMF Buds Pro 2 on fw 1.0.1.74.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cmf {

using Bytes = std::vector<uint8_t>;

namespace cmd {
// requests; replies and acks arrive with bit 0x8000 cleared, which
// FrameReader restores so they match the ids below
constexpr uint16_t kFirmware = 0xC042;
constexpr uint16_t kBattery = 0xC007;
constexpr uint16_t kAudioModeStatus = 0xC01E;
constexpr uint16_t kUltraBassStatus = 0xC04E;
// writes
constexpr uint16_t kSetAudioMode = 0xF00F;
constexpr uint16_t kSetUltraBass = 0xF051;
// unsolicited
constexpr uint16_t kBatteryPush = 0xE001;
constexpr uint16_t kAudioModePush = 0xE003;
}  // namespace cmd

enum class AudioMode : uint8_t {
  AncHigh = 0x01,
  AncMid = 0x02,
  AncLow = 0x03,
  AncAdaptive = 0x04,
  Off = 0x05,
  Transparency = 0x07,
};

std::string_view label(AudioMode mode);

struct Frame {
  uint16_t control;
  uint16_t command;
  uint8_t seq;
  Bytes payload;
};

struct Battery {
  int level;
  bool charging;
  bool operator==(const Battery&) const = default;
};

struct UltraBass {
  bool enabled;
  int level;  // 1..5
  bool operator==(const UltraBass&) const = default;
};

// keyed by "left", "right", "case"
using Batteries = std::map<std::string, Battery>;

uint16_t crc16(std::span<const uint8_t> data);
Bytes encode(uint16_t command, std::span<const uint8_t> payload = {}, uint8_t seq = 0);
Bytes encode_set_audio_mode(AudioMode mode);
Bytes encode_set_ultra_bass(bool enabled, int level);

// Reassembles frames from an RFCOMM byte stream (several may share a read).
class FrameReader {
 public:
  std::vector<Frame> feed(std::span<const uint8_t> data);

 private:
  Bytes buf_;
};

std::optional<AudioMode> parse_audio_mode(const Frame& frame);
std::optional<UltraBass> parse_ultra_bass(const Frame& frame);
Batteries parse_battery(const Frame& frame);

}  // namespace cmf
