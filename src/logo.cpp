#include "logo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>

using namespace ftxui;

namespace buddy {
namespace {

using Pixel = std::optional<Color>;  // nullopt = transparent
using Pixels = std::vector<std::vector<Pixel>>;

constexpr double kPi = std::numbers::pi;
constexpr int kDialSize = 16;  // pixels across
constexpr int kRidges = 24;    // knurls around the rim

constexpr double kBoostSpeed = 16.0;  // rad/s at launch
constexpr double kBoostDecay = 0.9;   // s

Color mix(std::array<uint8_t, 3> a, std::array<uint8_t, 3> b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  auto ch = [&](int i) { return static_cast<uint8_t>(a[i] + (b[i] - a[i]) * t); };
  return Color::RGB(ch(0), ch(1), ch(2));
}

// Two pixel rows per cell: fg paints the top half, bg the bottom.
Element render(const Pixels& px) {
  Elements rows;
  for (size_t y = 0; y < px.size(); y += 2) {
    Elements cells;
    for (size_t x = 0; x < px[y].size(); ++x) {
      const Pixel& top = px[y][x];
      const Pixel bottom = y + 1 < px.size() ? px[y + 1][x] : std::nullopt;
      if (top && bottom && *top == *bottom) {
        cells.push_back(text("█") | color(*top));
      } else if (top && bottom) {
        cells.push_back(text("▀") | color(*top) | bgcolor(*bottom));
      } else if (top) {
        cells.push_back(text("▀") | color(*top));
      } else if (bottom) {
        cells.push_back(text("▄") | color(*bottom));
      } else {
        cells.push_back(text(" "));
      }
    }
    rows.push_back(hbox(std::move(cells)));
  }
  return vbox(std::move(rows));
}

// 5x7 glyphs; '#' is ink.
constexpr std::array<std::string_view, 7> kB = {
    "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."};
constexpr std::array<std::string_view, 7> kU = {
    "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."};
constexpr std::array<std::string_view, 7> kD = {
    "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."};
constexpr std::array<std::string_view, 7> kY = {
    "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."};

}  // namespace

double dial_angle(double seconds) {
  return kBoostSpeed * kBoostDecay * (1 - std::exp(-seconds / kBoostDecay));
}

Element dial(double angle, bool pressed) {
  Pixels px(kDialSize, std::vector<Pixel>(kDialSize));
  const double half = kDialSize / 2.0;
  // indicator dot riding near the edge of the face
  const double dot_x = 0.55 * std::cos(angle);
  const double dot_y = 0.55 * std::sin(angle);

  for (int y = 0; y < kDialSize; ++y) {
    for (int x = 0; x < kDialSize; ++x) {
      const double u = (x + 0.5 - half) / half;
      const double v = (y + 0.5 - half) / half;
      const double r = std::hypot(u, v);
      if (r > 1.0) continue;

      if (r > 0.8) {
        // knurled rim: alternating ridges that turn with the dial
        double phase = (std::atan2(v, u) - angle) / (2 * kPi) * kRidges;
        const bool ridge = static_cast<long>(std::floor(phase)) % 2 == 0;
        px[y][x] = ridge ? Color::RGB(0x9a, 0x9a, 0x9a) : Color::RGB(0x4a, 0x4a, 0x4a);
      } else if (std::hypot(u - dot_x, v - dot_y) < 0.21) {
        px[y][x] = Color::RGB(0xf5, 0xf5, 0xf0);
      } else {
        // orange face lit from the top left
        const double light = (pressed ? 0.2 : 0.5) + 0.6 * (-u - v) / 2;
        px[y][x] = mix({0xc2, 0x40, 0x0c}, {0xff, 0x8a, 0x3d}, light);
      }
    }
  }
  return render(px);
}

Element wordmark() {
  constexpr std::array glyphs = {kB, kU, kD, kD, kY};
  Pixels px(8);  // 7 rows of font plus one blank to fill the last cell row
  for (size_t g = 0; g < glyphs.size(); ++g) {
    for (int y = 0; y < 7; ++y) {
      for (char c : glyphs[g][y]) {
        px[y].push_back(c == '#' ? Pixel(Color::Default) : std::nullopt);
      }
      if (g + 1 < glyphs.size()) px[y].push_back(std::nullopt);
    }
  }
  px[7].resize(px[0].size());
  return render(px) | bold;
}

}  // namespace buddy
