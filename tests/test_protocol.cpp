#include <cstdio>
#include <string>
#include <string_view>

#include "protocol.hpp"

using namespace cmf;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

Bytes hex(std::string_view s) {
  Bytes out;
  for (size_t i = 0; i + 1 < s.size(); i += 2) {
    out.push_back(static_cast<uint8_t>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16)));
  }
  return out;
}

Frame parse_one(const Bytes& data) {
  auto frames = FrameReader().feed(data);
  CHECK(frames.size() == 1);
  return frames.at(0);
}

// Frames captured from a CMF Buds Pro 2 (fw 1.0.1.74)
const Bytes kFirmware = hex("5520014240080000312e302e312e3734d835");
const Bytes kAudioMode = hex("5520011e400600000201000101006c69");
const Bytes kUltraBass = hex("5520014e40020000010a8027");
const Bytes kBattery = hex("5520010740050000020264035f5d6f");
const Bytes kAckThenPush = hex("5520010f70000000ffff55000103e0030000010700");

void test_encode_firmware_request() {
  // request the buds accepted during capture
  Bytes expected = hex("55200142c0000000");
  const uint16_t crc = crc16(expected);
  expected.push_back(crc & 0xFF);
  expected.push_back(crc >> 8);
  CHECK(encode(cmd::kFirmware) == expected);
}

void test_setters() {
  Frame frame = parse_one(encode_set_audio_mode(AudioMode::Transparency));
  CHECK(frame.command == cmd::kSetAudioMode);
  CHECK(frame.payload == hex("010700"));
  frame = parse_one(encode_set_ultra_bass(true, 4));
  CHECK(frame.command == cmd::kSetUltraBass);
  CHECK(frame.payload == hex("0108"));
  CHECK(parse_one(encode_set_ultra_bass(false, 9)).payload == hex("000a"));
}

void test_parse_status_frames() {
  const Frame fw = parse_one(kFirmware);
  CHECK(std::string(fw.payload.begin(), fw.payload.end()) == "1.0.1.74");
  CHECK(parse_audio_mode(parse_one(kAudioMode)) == AudioMode::AncHigh);
  CHECK(parse_ultra_bass(parse_one(kUltraBass)) == (UltraBass{true, 5}));
  const Batteries batteries = parse_battery(parse_one(kBattery));
  CHECK(batteries.size() == 2);
  CHECK(batteries.at("left") == (Battery{100, false}));
  CHECK(batteries.at("right") == (Battery{95, false}));
}

void test_reader_splits_ack_and_crcless_push() {
  auto frames = FrameReader().feed(kAckThenPush);
  CHECK(frames.size() == 2);
  CHECK(frames[0].command == cmd::kSetAudioMode);
  CHECK(frames[1].command == cmd::kAudioModePush);
  CHECK(parse_audio_mode(frames[1]) == AudioMode::Transparency);
}

void test_reader_handles_partial_reads() {
  FrameReader reader;
  const std::span<const uint8_t> all(kAudioMode);
  CHECK(reader.feed(all.first(5)).empty());
  auto frames = reader.feed(all.subspan(5));
  CHECK(frames.size() == 1);
  CHECK(frames.at(0).command == cmd::kAudioModeStatus);
}

}  // namespace

int main() {
  test_encode_firmware_request();
  test_setters();
  test_parse_status_frames();
  test_reader_splits_ack_and_crcless_push();
  test_reader_handles_partial_reads();
  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("all protocol tests passed");
  return 0;
}
