// RFCOMM connection to the buds. Requests block the calling thread; a background
// thread reads frames, completes pending requests and notifies listeners.
#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "protocol.hpp"

namespace cmf {

constexpr int kDefaultChannel = 15;

// Look up the buds' MAC among devices paired with BlueZ.
std::optional<std::string> find_paired_address();

class Buds {
 public:
  using Listener = std::function<void(const Frame&)>;

  Buds(std::string address, int channel);
  ~Buds();
  Buds(const Buds&) = delete;
  Buds& operator=(const Buds&) = delete;

  const std::string& address() const { return address_; }
  int channel() const { return channel_; }

  // Called on the reader thread for every frame, including unsolicited pushes.
  void on_frame(Listener listener);

  // All of these throw std::runtime_error (or std::system_error) on failure.
  void connect(std::chrono::seconds timeout = std::chrono::seconds(10));
  void close();
  Frame request(const Bytes& data, uint16_t reply,
                std::chrono::milliseconds timeout = std::chrono::seconds(3));

  std::string firmware();
  Batteries battery();
  std::optional<AudioMode> audio_mode();
  std::optional<UltraBass> ultra_bass();
  void set_audio_mode(AudioMode mode);
  void set_ultra_bass(bool enabled, int level);

 private:
  using Waiter = std::shared_ptr<std::promise<Frame>>;

  void read_loop(int fd);
  void fail_waiters();

  std::string address_;
  int channel_;
  int fd_ = -1;
  std::thread reader_;
  std::mutex mu_;
  std::map<uint16_t, std::vector<Waiter>> waiters_;
  std::vector<Listener> listeners_;
};

}  // namespace cmf
