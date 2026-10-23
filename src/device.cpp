#include "device.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <regex>
#include <stdexcept>
#include <system_error>

namespace cmf {
namespace {

// From <bluetooth/rfcomm.h>; declared here so BlueZ headers aren't a build dependency.
constexpr int kBtProtoRfcomm = 3;
struct SockaddrRc {
  sa_family_t family;
  uint8_t bdaddr[6];  // little endian, i.e. reversed from the printed form
  uint8_t channel;
};

std::system_error sys_error(const char* what) {
  return std::system_error(errno, std::generic_category(), what);
}

SockaddrRc make_addr(const std::string& address, int channel) {
  unsigned b[6];
  if (std::sscanf(address.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &b[5], &b[4], &b[3], &b[2], &b[1],
                  &b[0]) != 6) {
    throw std::runtime_error("invalid Bluetooth address: " + address);
  }
  SockaddrRc addr{};
  addr.family = AF_BLUETOOTH;
  for (int i = 0; i < 6; ++i) addr.bdaddr[i] = static_cast<uint8_t>(b[i]);
  addr.channel = static_cast<uint8_t>(channel);
  return addr;
}

}  // namespace

std::optional<std::string> find_paired_address() {
  FILE* pipe = ::popen("bluetoothctl devices Paired 2>/dev/null", "r");
  if (!pipe) return std::nullopt;
  std::string out;
  std::array<char, 256> chunk;
  while (std::fgets(chunk.data(), chunk.size(), pipe)) out += chunk.data();
  ::pclose(pipe);

  static const std::regex line(R"(Device ([0-9A-F:]{17}) CMF Buds Pro 2\n)");
  std::smatch match;
  if (std::regex_search(out, match, line)) return match[1].str();
  return std::nullopt;
}

Buds::Buds(std::string address, int channel) : address_(std::move(address)), channel_(channel) {}

Buds::~Buds() { close(); }

void Buds::on_frame(Listener listener) {
  std::lock_guard lock(mu_);
  listeners_.push_back(std::move(listener));
}

void Buds::connect(std::chrono::seconds timeout) {
  close();
  const SockaddrRc addr = make_addr(address_, channel_);
  const int fd = ::socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_CLOEXEC, kBtProtoRfcomm);
  if (fd < 0) throw sys_error("socket");

  // Non-blocking connect so an absent device can't hang us for the full page timeout.
  ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0 &&
      errno != EINPROGRESS) {
    auto err = sys_error("connect");
    ::close(fd);
    throw err;
  }
  pollfd pfd{fd, POLLOUT, 0};
  const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count() * 1000));
  int so_error = 0;
  socklen_t len = sizeof so_error;
  ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
  if (ready <= 0 || so_error) {
    ::close(fd);
    if (ready == 0) throw std::runtime_error("connect: timed out");
    errno = so_error ? so_error : errno;
    throw sys_error("connect");
  }
  ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) & ~O_NONBLOCK);

  fd_ = fd;
  reader_ = std::thread(&Buds::read_loop, this, fd);
}

void Buds::close() {
  if (fd_ < 0) return;
  ::shutdown(fd_, SHUT_RDWR);  // wakes the reader's recv()
  if (reader_.joinable()) reader_.join();
  ::close(fd_);
  fd_ = -1;
}

void Buds::read_loop(int fd) {
  FrameReader reader;
  std::array<uint8_t, 1024> buf;
  while (true) {
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) break;
    for (const Frame& frame : reader.feed(std::span(buf.data(), static_cast<size_t>(n)))) {
      std::vector<Waiter> ready;
      std::vector<Listener> listeners;
      {
        std::lock_guard lock(mu_);
        if (auto it = waiters_.find(frame.command); it != waiters_.end()) {
          ready = std::move(it->second);
          waiters_.erase(it);
        }
        listeners = listeners_;
      }
      for (auto& waiter : ready) waiter->set_value(frame);
      for (auto& listener : listeners) listener(frame);
    }
  }
  fail_waiters();
}

void Buds::fail_waiters() {
  std::lock_guard lock(mu_);
  for (auto& [_, waiters] : waiters_) {
    for (auto& waiter : waiters) {
      waiter->set_exception(std::make_exception_ptr(std::runtime_error("disconnected")));
    }
  }
  waiters_.clear();
}

Frame Buds::request(const Bytes& data, uint16_t reply, std::chrono::milliseconds timeout) {
  if (fd_ < 0) throw std::runtime_error("not connected");
  auto waiter = std::make_shared<std::promise<Frame>>();
  auto future = waiter->get_future();
  {
    std::lock_guard lock(mu_);
    waiters_[reply].push_back(waiter);
  }
  if (::send(fd_, data.data(), data.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(data.size())) {
    throw sys_error("send");
  }
  if (future.wait_for(timeout) != std::future_status::ready) {
    std::lock_guard lock(mu_);
    if (auto it = waiters_.find(reply); it != waiters_.end()) std::erase(it->second, waiter);
    throw std::runtime_error("timed out waiting for reply");
  }
  return future.get();
}

std::string Buds::firmware() {
  const Frame frame = request(encode(cmd::kFirmware), cmd::kFirmware);
  return {frame.payload.begin(), frame.payload.end()};
}

Batteries Buds::battery() {
  return parse_battery(request(encode(cmd::kBattery), cmd::kBattery));
}

std::optional<AudioMode> Buds::audio_mode() {
  return parse_audio_mode(request(encode(cmd::kAudioModeStatus), cmd::kAudioModeStatus));
}

std::optional<UltraBass> Buds::ultra_bass() {
  return parse_ultra_bass(request(encode(cmd::kUltraBassStatus), cmd::kUltraBassStatus));
}

void Buds::set_audio_mode(AudioMode mode) {
  request(encode_set_audio_mode(mode), cmd::kSetAudioMode);
}

void Buds::set_ultra_bass(bool enabled, int level) {
  request(encode_set_ultra_bass(enabled, level), cmd::kSetUltraBass);
}

}  // namespace cmf
