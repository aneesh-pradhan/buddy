#include "protocol.hpp"

#include <algorithm>

namespace cmf {
namespace {

constexpr uint8_t kSof = 0x55;
constexpr uint16_t kControlRequest = 0x0120;
constexpr uint16_t kControlCrc = 0x20;
constexpr uint16_t kMaskRequest = 0x8000;
constexpr size_t kHeaderSize = 8;

uint16_t read_u16(const uint8_t* p) { return p[0] | (p[1] << 8); }

void put_u16(Bytes& out, uint16_t v) {
  out.push_back(v & 0xFF);
  out.push_back(v >> 8);
}

std::optional<AudioMode> to_audio_mode(uint8_t code) {
  switch (code) {
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x07:
      return static_cast<AudioMode>(code);
    default:
      return std::nullopt;
  }
}

}  // namespace

std::string_view label(AudioMode mode) {
  switch (mode) {
    case AudioMode::AncHigh: return "Noise cancelling: High";
    case AudioMode::AncMid: return "Noise cancelling: Mid";
    case AudioMode::AncLow: return "Noise cancelling: Low";
    case AudioMode::AncAdaptive: return "Noise cancelling: Adaptive";
    case AudioMode::Transparency: return "Transparency";
    case AudioMode::Off: return "Off";
  }
  return "?";
}

uint16_t crc16(std::span<const uint8_t> data) {
  uint16_t crc = 0xFFFF;
  for (uint8_t byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

Bytes encode(uint16_t command, std::span<const uint8_t> payload, uint8_t seq) {
  Bytes out{kSof};
  put_u16(out, kControlRequest);
  put_u16(out, command);
  put_u16(out, static_cast<uint16_t>(payload.size()));
  out.push_back(seq);
  out.insert(out.end(), payload.begin(), payload.end());
  put_u16(out, crc16(out));
  return out;
}

Bytes encode_set_audio_mode(AudioMode mode) {
  const uint8_t payload[] = {0x01, static_cast<uint8_t>(mode), 0x00};
  return encode(cmd::kSetAudioMode, payload);
}

Bytes encode_set_ultra_bass(bool enabled, int level) {
  level = std::clamp(level, 1, 5);
  const uint8_t payload[] = {static_cast<uint8_t>(enabled), static_cast<uint8_t>(level * 2)};
  return encode(cmd::kSetUltraBass, payload);
}

std::vector<Frame> FrameReader::feed(std::span<const uint8_t> data) {
  buf_.insert(buf_.end(), data.begin(), data.end());
  std::vector<Frame> frames;
  while (true) {
    auto start = std::find(buf_.begin(), buf_.end(), kSof);
    buf_.erase(buf_.begin(), start);
    if (buf_.size() < kHeaderSize) break;

    const uint16_t control = read_u16(&buf_[1]);
    const uint16_t command = read_u16(&buf_[3]);
    const uint16_t length = read_u16(&buf_[5]);
    const size_t total = kHeaderSize + length + ((control & kControlCrc) ? 2 : 0);
    if (buf_.size() < total) break;

    Bytes payload(buf_.begin() + kHeaderSize, buf_.begin() + kHeaderSize + length);
    frames.push_back({control, static_cast<uint16_t>(command | kMaskRequest), buf_[7],
                      std::move(payload)});
    buf_.erase(buf_.begin(), buf_.begin() + total);
  }
  return frames;
}

std::optional<AudioMode> parse_audio_mode(const Frame& frame) {
  const Bytes& p = frame.payload;
  if (frame.command == cmd::kAudioModeStatus && p.size() >= 5) {
    // 02 | <last anc level> 00 | 01 <mode> 00
    return to_audio_mode(p[4]);
  }
  if (frame.command == cmd::kAudioModePush && p.size() >= 2) {
    // 01 <mode> 00
    return to_audio_mode(p[1]);
  }
  return std::nullopt;
}

std::optional<UltraBass> parse_ultra_bass(const Frame& frame) {
  const Bytes& p = frame.payload;
  if (p.size() < 2) return std::nullopt;
  return UltraBass{p[0] == 0x01, std::clamp((p[1] + 1) / 2, 1, 5)};
}

Batteries parse_battery(const Frame& frame) {
  const Bytes& p = frame.payload;
  Batteries result;
  const size_t count = p.empty() ? 0 : p[0];
  for (size_t i = 0; i < count && 2 + 2 * i < p.size(); ++i) {
    const uint8_t slot = p[1 + 2 * i];
    const uint8_t value = p[2 + 2 * i];
    const char* name = slot == 0x02 ? "left" : slot == 0x03 ? "right" : slot == 0x04 ? "case" : nullptr;
    if (name) result[name] = {value & 0x7F, (value & 0x80) != 0};
  }
  return result;
}

}  // namespace cmf
