#include "volume.hpp"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <regex>
#include <sstream>
#include <vector>

extern char** environ;

namespace buddy {
namespace {

// Runs a shell command and returns its stdout. Arguments must already be safe.
std::string run(const std::string& command) {
  FILE* pipe = ::popen((command + " 2>/dev/null").c_str(), "r");
  if (!pipe) return {};
  std::string out;
  std::array<char, 256> chunk;
  while (std::fgets(chunk.data(), chunk.size(), pipe)) out += chunk.data();
  ::pclose(pipe);
  return out;
}

struct Sink {
  int index;
  std::string name;
};

std::optional<Sink> find_sink(const std::string& prefix) {
  // index \t name \t driver \t spec \t state
  std::istringstream lines(run("pactl list short sinks"));
  for (std::string line; std::getline(lines, line);) {
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    std::string name = line.substr(tab + 1, line.find('\t', tab + 1) - tab - 1);
    if (name.starts_with(prefix)) return Sink{std::stoi(line.substr(0, tab)), std::move(name)};
  }
  return std::nullopt;
}

// Starts `pactl subscribe` with its stdout on a pipe. Returns {pid, read fd}.
std::optional<std::pair<pid_t, int>> spawn_subscribe() {
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC) < 0) return std::nullopt;
  posix_spawn_file_actions_t actions;
  ::posix_spawn_file_actions_init(&actions);
  ::posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
  ::posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  char arg0[] = "pactl", arg1[] = "subscribe";
  char* argv[] = {arg0, arg1, nullptr};
  pid_t pid;
  const int err = ::posix_spawnp(&pid, "pactl", &actions, nullptr, argv, environ);
  ::posix_spawn_file_actions_destroy(&actions);
  ::close(fds[1]);
  if (err) {
    ::close(fds[0]);
    return std::nullopt;
  }
  return std::pair{pid, fds[0]};
}

// PipeWire names Bluetooth sinks like "bluez_output.AA_BB_CC_DD_EE_FF.1".
std::string sink_prefix(std::string address) {
  std::ranges::replace(address, ':', '_');
  return "bluez_output." + address;
}

}  // namespace

Volume::Volume(const std::string& address, Listener listener)
    : prefix_(sink_prefix(address)),
      listener_(std::move(listener)),
      watcher_([this] { watch(); }),
      setter_([this] { apply(); }) {}

Volume::~Volume() {
  {
    std::lock_guard lock(mu_);
    stop_ = true;
    if (subscribe_pid_ > 0) ::kill(subscribe_pid_, SIGTERM);  // ends watch()'s read
  }
  cv_.notify_all();
  watcher_.join();
  setter_.join();
}

void Volume::set(int percent) {
  {
    std::lock_guard lock(mu_);
    pending_ = std::clamp(percent, 0, 100);
  }
  cv_.notify_all();
}

int Volume::refresh() {
  const auto sink = find_sink(prefix_);
  {
    std::lock_guard lock(mu_);
    sink_ = sink ? sink->name : "";
  }
  if (!sink) {
    listener_(std::nullopt);
    return -1;
  }

  const std::string out = run(std::format("pactl get-sink-volume {0}; pactl get-sink-mute {0}",
                                          sink->name));
  static const std::regex percent(R"((\d+)%)");
  std::smatch match;
  if (!std::regex_search(out, match, percent)) {
    listener_(std::nullopt);
    return sink->index;
  }
  listener_(VolumeState{std::min(std::stoi(match[1].str()), 100),
                        out.find("Mute: yes") != std::string::npos});
  return sink->index;
}

void Volume::watch() {
  auto child = spawn_subscribe();
  {
    std::lock_guard lock(mu_);
    if (child) subscribe_pid_ = child->first;
    if (stop_ && child) ::kill(child->first, SIGTERM);
  }
  int index = refresh();
  if (!child) return;

  // "Event 'change' on sink #51"; sinks come and go as the buds (dis)connect
  static const std::regex event(R"(Event '(\w+)' on sink #(\d+))");
  FILE* in = ::fdopen(child->second, "r");
  std::array<char, 256> line;
  while (std::fgets(line.data(), line.size(), in)) {
    std::cmatch match;
    if (!std::regex_search(line.data(), match, event)) continue;
    if (match[1] != "change" || std::stoi(match[2].str()) == index) index = refresh();
  }
  std::fclose(in);
  ::waitpid(child->first, nullptr, 0);
}

void Volume::apply() {
  std::unique_lock lock(mu_);
  while (true) {
    cv_.wait(lock, [this] { return stop_ || pending_; });
    if (stop_) return;
    const int percent = *std::exchange(pending_, std::nullopt);
    const std::string sink = sink_;
    lock.unlock();
    // turning the dial while muted should be audible, as with the buds' own controls
    if (!sink.empty()) {
      run(std::format("pactl set-sink-volume {0} {1}% && pactl set-sink-mute {0} 0", sink, percent));
    }
    lock.lock();
  }
}

bool play_pause() {
  // only names that are safe to pass through the shell
  static const std::regex player(R"(^(org\.mpris\.MediaPlayer2\.[A-Za-z0-9_.-]+)\s)");
  std::vector<std::string> players;
  std::istringstream lines(run("busctl --user list --acquired --no-legend --no-pager"));
  for (std::string line; std::getline(lines, line);) {
    std::smatch match;
    if (std::regex_search(line, match, player)) players.push_back(match[1].str());
  }
  if (players.empty()) return false;

  constexpr const char* kPath = "/org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player";
  auto target = std::ranges::find_if(players, [&](const std::string& name) {
    return run(std::format("busctl --user get-property {} {} PlaybackStatus", name, kPath))
        .contains("Playing");
  });
  const std::string& name = target != players.end() ? *target : players.front();
  run(std::format("busctl --user call {} {} PlayPause", name, kPath));
  return true;
}

}  // namespace buddy
