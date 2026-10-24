// Host-side media control for the buds: the volume of their audio sink (via
// pactl, so it works on PipeWire and PulseAudio) and MPRIS play/pause (via busctl).
//
// The control channel has no volume command; over A2DP the buds' volume *is* the
// sink volume (AVRCP absolute volume), so this is what their own buttons change too.
#pragma once

#include <sys/types.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace buddy {

struct VolumeState {
  int percent;  // 0..100
  bool muted;
  bool operator==(const VolumeState&) const = default;
};

class Volume {
 public:
  // nullopt when the buds have no sink (disconnected, or pactl unavailable).
  using Listener = std::function<void(std::optional<VolumeState>)>;

  // `address` is the buds' MAC; `listener` runs on a background thread with the
  // current state at startup and after every change, whoever made it.
  Volume(const std::string& address, Listener listener);
  ~Volume();
  Volume(const Volume&) = delete;
  Volume& operator=(const Volume&) = delete;

  // Queues a change; rapid calls coalesce so only the latest is applied.
  void set(int percent);

 private:
  void watch();
  void apply();
  int refresh();  // reports the current state; returns the sink index or -1

  const std::string prefix_;  // sink name prefix, e.g. "bluez_output.AA_BB_CC_DD_EE_FF"
  Listener listener_;

  std::mutex mu_;
  std::condition_variable cv_;
  std::optional<int> pending_;
  bool stop_ = false;
  std::string sink_;  // current sink name, empty if none
  pid_t subscribe_pid_ = -1;

  std::thread watcher_;  // last, so they start after the members they use
  std::thread setter_;
};

// Toggles playback on the active MPRIS player (the playing one if any, else the
// first found). Blocks briefly; returns false if there is no player.
bool play_pause();

}  // namespace buddy
