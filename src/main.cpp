// FTXUI front end for the CMF Buds Pro 2.
//
// Threads: the UI thread owns all view state. Device calls run on a single worker
// thread, and results (plus pushes from the Buds reader thread) are handed back to
// the UI thread with ScreenInteractive::Post.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "device.hpp"
#include "logo.hpp"
#include "protocol.hpp"

using namespace ftxui;
using cmf::AudioMode;

namespace {

constexpr AudioMode kModeOrder[] = {
    AudioMode::AncHigh,      AudioMode::AncMid,       AudioMode::AncLow,
    AudioMode::AncAdaptive,  AudioMode::Transparency, AudioMode::Off,
};

int mode_index(AudioMode mode) {
  return static_cast<int>(std::ranges::find(kModeOrder, mode) - std::begin(kModeOrder));
}

// Runs jobs one at a time off the UI thread; calls `idle` when nothing was queued
// for `idle_every`.
class Worker {
 public:
  Worker(std::function<void()> idle, std::chrono::seconds idle_every)
      : idle_(std::move(idle)), idle_every_(idle_every), thread_([this] { run(); }) {}

  ~Worker() {
    {
      std::lock_guard lock(mu_);
      stop_ = true;
    }
    cv_.notify_one();
    thread_.join();
  }

  void post(std::function<void()> job) {
    {
      std::lock_guard lock(mu_);
      jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
  }

 private:
  void run() {
    std::unique_lock lock(mu_);
    while (!stop_) {
      if (!cv_.wait_for(lock, idle_every_, [this] { return stop_ || !jobs_.empty(); })) {
        lock.unlock();
        idle_();
        lock.lock();
        continue;
      }
      if (stop_) break;
      auto job = std::move(jobs_.front());
      jobs_.pop_front();
      lock.unlock();
      job();
      lock.lock();
    }
  }

  std::function<void()> idle_;
  std::chrono::seconds idle_every_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  bool stop_ = false;
  std::thread thread_;  // last, so it starts after the members it uses
};

struct Args {
  std::string address;
  int channel = cmf::kDefaultChannel;
};

[[noreturn]] void usage(const char* argv0, int code) {
  (code ? std::cerr : std::cout)
      << "usage: " << argv0 << " [--address MAC] [--channel N]\n\n"
      << "  --address MAC  Bluetooth MAC (default: auto-detect paired buds)\n"
      << "  --channel N    RFCOMM channel (default: " << cmf::kDefaultChannel << ")\n";
  std::exit(code);
}

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") usage(argv[0], 0);
    if (i + 1 >= argc) usage(argv[0], 2);
    if (arg == "--address") {
      args.address = argv[++i];
    } else if (arg == "--channel") {
      args.channel = std::atoi(argv[++i]);
    } else {
      usage(argv[0], 2);
    }
  }
  if (args.address.empty()) {
    auto found = cmf::find_paired_address();
    if (!found) {
      std::cerr << "no paired 'CMF Buds Pro 2' found; pair them first or pass --address\n";
      std::exit(1);
    }
    args.address = *found;
  }
  return args;
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parse_args(argc, argv);
  auto screen = ScreenInteractive::Fullscreen();

  // -- view state (UI thread only) --------------------------------------
  bool connected = false;
  std::string status = std::format("Connecting to {} (channel {})…", args.address, args.channel);
  std::string firmware;
  cmf::Batteries batteries;
  std::optional<AudioMode> mode;
  std::optional<cmf::UltraBass> bass;
  int selected_mode = 0;
  bool bass_enabled = false;
  int bass_level = 4;  // index into level_entries

  // Runs `fn` on the UI thread and redraws.
  auto on_ui = [&screen](std::function<void()> fn) {
    screen.Post(std::move(fn));
    screen.PostEvent(Event::Custom);
  };

  auto sync_widgets = [&] {
    if (mode) selected_mode = mode_index(*mode);
    if (bass) {
      bass_enabled = bass->enabled;
      bass_level = bass->level - 1;
    }
  };

  cmf::Buds buds(args.address, args.channel);
  buds.on_frame([&](const cmf::Frame& frame) {
    // pushes the buds send on their own (e.g. ANC changed by a tap)
    if (frame.command == cmf::cmd::kAudioModePush) {
      if (auto pushed = cmf::parse_audio_mode(frame)) {
        on_ui([&, pushed] {
          mode = pushed;
          sync_widgets();
        });
      }
    } else if (frame.command == cmf::cmd::kBatteryPush) {
      on_ui([&, update = cmf::parse_battery(frame)] {
        for (const auto& [slot, battery] : update) batteries[slot] = battery;
      });
    }
  });

  std::atomic<bool> link_up = false;
  auto poll_battery = [&] {
    if (!link_up) return;
    try {
      auto latest = buds.battery();
      on_ui([&, latest] { batteries = latest; });
    } catch (const std::exception&) {
    }
  };
  Worker worker(poll_battery, std::chrono::seconds(60));

  auto fail = [&](std::string what, const std::exception& e) {
    on_ui([&, msg = std::format("{}: {}", what, e.what())] { status = msg; });
  };

  auto refresh_all = [&] {
    try {
      auto b = buds.battery();
      auto m = buds.audio_mode();
      auto u = buds.ultra_bass();
      on_ui([&, b, m, u] {
        batteries = b;
        mode = m;
        bass = u;
        connected = true;
        status.clear();
        sync_widgets();
      });
    } catch (const std::exception& e) {
      fail("Read failed (press c to reconnect)", e);
    }
  };

  auto reconnect = [&] {
    link_up = false;
    on_ui([&] {
      connected = false;
      status = std::format("Connecting to {} (channel {})…", buds.address(), buds.channel());
    });
    try {
      buds.connect();
      auto fw = buds.firmware();
      on_ui([&, fw] { firmware = fw; });
    } catch (const std::exception& e) {
      fail("Connection failed (press c to retry)", e);
      return;
    }
    link_up = true;
    refresh_all();
  };

  // -- widgets ----------------------------------------------------------
  std::vector<std::string> mode_entries;
  for (AudioMode m : kModeOrder) mode_entries.emplace_back(cmf::label(m));
  RadioboxOption mode_opt;
  mode_opt.entries = &mode_entries;
  mode_opt.selected = &selected_mode;
  mode_opt.on_change = [&] {
    const AudioMode wanted = kModeOrder[selected_mode];
    if (mode == wanted) return;
    worker.post([&, wanted] {
      try {
        buds.set_audio_mode(wanted);
        auto now = buds.audio_mode();
        on_ui([&, now] { mode = now; sync_widgets(); });
      } catch (const std::exception& e) {
        fail("Failed to set noise control", e);
        on_ui(sync_widgets);
      }
    });
  };
  auto mode_box = Radiobox(mode_opt);

  auto apply_bass = [&](cmf::UltraBass wanted) {
    worker.post([&, wanted] {
      try {
        buds.set_ultra_bass(wanted.enabled, wanted.level);
        auto now = buds.ultra_bass();
        on_ui([&, now] { bass = now; sync_widgets(); });
      } catch (const std::exception& e) {
        fail("Failed to set Ultra Bass", e);
        on_ui(sync_widgets);
      }
    });
  };

  CheckboxOption bass_opt = CheckboxOption::Simple();
  bass_opt.label = "Enabled";
  bass_opt.checked = &bass_enabled;
  bass_opt.on_change = [&] {
    if (bass && bass_enabled != bass->enabled) apply_bass({bass_enabled, bass->level});
  };
  auto bass_box = Checkbox(bass_opt);

  std::vector<std::string> level_entries = {"1", "2", "3", "4", "5"};
  MenuOption level_opt = MenuOption::Horizontal();
  level_opt.entries = &level_entries;
  level_opt.selected = &bass_level;
  level_opt.on_change = [&] {
    if (bass && bass_level + 1 != bass->level) apply_bass({bass->enabled, bass_level + 1});
  };
  auto level_menu = Menu(level_opt);

  auto controls = Container::Vertical({mode_box, bass_box, level_menu});

  using Clock = std::chrono::steady_clock;
  const auto started = Clock::now();
  auto view = Renderer(controls, [&] {
    Elements header;
    if (connected) {
      std::string battery_text;
      for (const char* slot : {"left", "right", "case"}) {
        if (auto it = batteries.find(slot); it != batteries.end()) {
          std::string name = slot;
          name[0] = static_cast<char>(std::toupper(name[0]));
          battery_text += std::format("{} {}%{}  ", name, it->second.level,
                                      it->second.charging ? "⚡" : "");
        }
      }
      header.push_back(hbox(text("● ") | color(Color::Green), text("Connected · fw " + firmware)));
      header.push_back(text(battery_text.empty() ? "battery unknown" : battery_text) | dim);
    }
    if (!status.empty()) header.push_back(paragraph(status) | color(connected ? Color(Color::Red) : Color(Color::Default)));

    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    header.insert(header.begin(), buddy::wordmark());
    Elements body = {
        hbox(buddy::dial(buddy::dial_angle(seconds)), text("   "), vbox(header) | vcenter),
        separatorEmpty(),
    };
    if (connected) {
      body.push_back(window(text(" Noise control ") | bold, mode_box->Render()));
      body.push_back(window(text(" Ultra Bass ") | bold,
                            vbox(bass_box->Render(),
                                 hbox(text("Level  "), level_menu->Render()))));
    }
    return vbox({
               separatorEmpty(),
               vbox(body) | size(WIDTH, EQUAL, 60) | center,
               filler(),
               separator(),
               text(" r refresh   c reconnect   q quit") | dim,
           });
  });

  auto app = CatchEvent(view, [&](const Event& event) {
    if (event == Event::Custom) return false;  // redraw ticks and posted updates
    if (event == Event::Character('q') || event == Event::Escape) {
      screen.Exit();
      return true;
    }
    if (event == Event::Character('r')) {
      worker.post(refresh_all);
      return true;
    }
    if (event == Event::Character('c')) {
      worker.post(reconnect);
      return true;
    }
    // ignore input aimed at controls until device state is known
    return !connected && !event.is_mouse();
  });

  // drives the dial animation
  std::jthread ticker([&screen](std::stop_token stop) {
    while (!stop.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
      screen.PostEvent(Event::Custom);
    }
  });

  worker.post(reconnect);
  screen.Loop(app);
  return 0;
}
