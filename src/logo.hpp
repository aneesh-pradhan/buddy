// Animated header art: a pixel-art take on the CMF Buds Pro 2 case's Smart Dial
// that spins up on launch and then idles, next to a pixel "BUDDY" wordmark.
//
// Pixels are drawn two per terminal cell with half blocks (▀/▄), which makes them
// roughly square in most fonts.
#pragma once

#include <ftxui/dom/elements.hpp>

namespace buddy {

// Dial rotation in radians `seconds` after launch: a fast spin that eases off
// into a slow idle turn.
double dial_angle(double seconds);

// The dial at the given rotation.
ftxui::Element dial(double angle);

// "BUDDY" in a 5x7 pixel font, drawn in the terminal's foreground color.
ftxui::Element wordmark();

}  // namespace buddy
